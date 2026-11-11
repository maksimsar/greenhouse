"""Local greenhouse server. The C controllers are the only actuator authority."""

import argparse
import asyncio
from collections import deque
from contextlib import asynccontextmanager
from pathlib import Path
from time import monotonic, time

from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from bridge import NativePort, WokwiPort
from experiment import Experiment, TITLES
from model import Greenhouse, SCENARIOS

WEB = Path(__file__).parent / "web"


class Session:
    def __init__(self, mode):
        self.mode = mode
        self.house = Greenhouse()
        self.state = None
        self.state_at = -float("inf")
        self.link = {"controller": False, "actuator": False}
        self.journal = deque(maxlen=100)
        self.history = deque(maxlen=120)
        self.pump_accounted_at = monotonic()
        self.pump_deadline = 0
        self.pump_seconds = 0
        self.ports = {}
        self.experiment = None
        self.pending_motion = False
        self.last_pump_stop = -float("inf")

    @property
    def fresh(self):
        return all(self.link.values()) and monotonic() - self.state_at < 7

    def log(self, route, frame):
        message = frame.decode("ascii")
        self.journal.append({"time": time(), "route": route, "frame": message})
        if self.experiment:
            self.experiment.frame(route, message)

    def send(self, port, route, frame):
        if self.ports[port].send(frame):
            self.log(route, frame)
            return True
        return False

    def accrue_pump(self, now):
        if self.state and self.state[2] and all(self.link.values()):
            self.pump_seconds += max(0, min(now, self.pump_deadline) - self.pump_accounted_at)
        self.pump_accounted_at = now

    def connection(self, port, connected):
        self.accrue_pump(monotonic())
        self.link[port] = connected
        if not connected:
            self.state_at = -float("inf")
            self.pump_deadline = 0
            self.pending_motion = False
            if self.experiment and self.experiment.phase == "running":
                self.experiment.fail("Связь с одной из плат прервалась.")

    def receive(self, port, frame):
        if port == "controller":
            if frame.startswith(b"INFO:"):
                self.log("МК 1", frame)
            elif self.send("actuator", "МК 1 → МК 2", frame) and frame.startswith((b"SERV:", b"STEP:", b"PUMP:")):
                self.pending_motion = True
            return

        if frame.startswith(b"STATE:"):
            try:
                angle, steps, pump, busy = map(int, frame[6:].split(b","))
                if not (0 <= angle <= 180 and 0 <= steps <= 200 and pump in (0, 1) and busy in (0, 1)):
                    raise ValueError
            except ValueError:
                self.log("МК 2 · ошибка состояния", frame)
                return
            now = monotonic()
            self.accrue_pump(now)
            if pump and (not self.state or not self.state[2]):
                self.pump_deadline = now + 3
            if not pump and self.state and self.state[2]:
                self.last_pump_stop = now
            self.state = (angle, steps, pump, busy)
            self.state_at = now

        if self.link["controller"]:
            self.send("controller", "МК 2 → МК 1", frame)
        else:
            self.log("МК 2 → мост", frame)
        if frame in (b"OK", b"ERROR"):
            self.pending_motion = False

    def set_scenario(self, name, experiment=None):
        self.house.scenario(name)
        if experiment:
            experiment.begin(self.house.readings(), monotonic())
        self.accrue_pump(monotonic())
        self.pump_seconds = 0
        self.history.clear()
        self.history.append({"time": time(), **self.house.readings()})
        self.send("controller", "Модель → МК 1", self.house.frame().rstrip(b"\n"))

    def start_experiment(self, name):
        self.experiment = Experiment(name, monotonic())
        self.set_scenario("normal")

    def advance_experiment(self):
        experiment = self.experiment
        if not experiment or experiment.phase in ("passed", "failed"):
            return
        now = monotonic()
        if now >= experiment.deadline:
            if not self.fresh:
                experiment.fail("Проверьте связь с обеими платами и повторите опыт.")
            else:
                experiment.fail("Ожидаемый кадр или результат не получен. Проверьте журнал USART.")
            return
        if experiment.phase == "preparing":
            if not self.fresh:
                experiment.message = "Ждём актуальное состояние от обеих плат."
            elif self.pending_motion or self.state != (0, 0, 0, 0):
                experiment.message = "Возвращаем приводы в исходное положение."
            elif experiment.name == "dry" and now - self.last_pump_stop < 15.5:
                experiment.message = "Ждём окончания паузы между поливами."
            else:
                if experiment.name == "invalid":
                    experiment.begin(self.house.readings(), now)
                    self.send("controller", "Стенд → МК 1", b"DIAG:ANGLE")
                else:
                    self.set_scenario(experiment.name, experiment)
        elif self.fresh:
            experiment.check(self.house, self.state)

    async def tick(self):
        while True:
            await asyncio.sleep(1)
            self.accrue_pump(monotonic())
            self.house.tick(self.state or (0, 0, 0, 0), self.pump_seconds)
            self.pump_seconds = 0
            self.history.append({"time": time(), **self.house.readings()})
            self.send("controller", "Модель → МК 1", self.house.frame().rstrip(b"\n"))
            self.advance_experiment()

    def snapshot(self):
        sensors = self.house.readings()
        irrigation_block = None
        if sensors["soil"] is None or sensors["water"] is None:
            irrigation_block = "sensor"
        elif sensors["water"] <= 10:
            irrigation_block = "water"
        return {
            "mode": self.mode,
            "scenario": self.house.name,
            "sensors": sensors,
            "dew_point": round(self.house.dew_point, 1),
            "condensation": self.house.condensation,
            "state": None if self.state is None else dict(zip(
                ("angle", "steps", "pump", "busy"), (*self.state[:2], bool(self.state[2]), bool(self.state[3]))
            )),
            "link": {**self.link, "fresh": self.fresh},
            "irrigation_block": irrigation_block,
            "experiment": self.experiment.snapshot() if self.experiment else None,
            "history": list(self.history),
            "journal": list(self.journal),
        }


def create_app(mode):
    session = Session(mode)

    @asynccontextmanager
    async def lifespan(_app):
        loop = asyncio.get_running_loop()
        port_type = NativePort if mode == "demo" else WokwiPort
        session.ports = {
            name: port_type(name, loop, session.receive, session.connection)
            for name in session.link
        }
        tick = None
        try:
            for port in session.ports.values():
                await port.start()
            session.set_scenario("normal")
            tick = asyncio.create_task(session.tick())
            yield
        finally:
            if tick:
                tick.cancel()
                await asyncio.gather(tick, return_exceptions=True)
            await asyncio.gather(*(port.close() for port in session.ports.values()))

    app = FastAPI(lifespan=lifespan, docs_url=None, redoc_url=None, openapi_url=None)
    app.mount("/static", StaticFiles(directory=WEB), name="static")

    @app.get("/")
    async def index():
        return FileResponse(WEB / "index.html")

    def local_origin(connection):
        origin = connection.headers.get("origin")
        host = connection.headers.get("host")
        return origin is None or origin in (f"http://{host}", f"https://{host}")

    @app.post("/scenario/{name}")
    async def scenario(name: str, request: Request):
        if not local_origin(request):
            raise HTTPException(403)
        if name not in SCENARIOS:
            raise HTTPException(404, "Сценарий не найден")
        session.experiment = None
        session.set_scenario(name)
        return {"scenario": name}

    @app.post("/experiment/{name}")
    async def experiment(name: str, request: Request):
        if not local_origin(request):
            raise HTTPException(403)
        if name not in TITLES:
            raise HTTPException(404, "Опыт не найден")
        session.start_experiment(name)
        return {"experiment": name}

    @app.websocket("/ws")
    async def stream(socket: WebSocket):
        if not local_origin(socket):
            await socket.close(code=1008)
            return
        await socket.accept()
        try:
            while True:
                await socket.send_json(session.snapshot())
                await asyncio.sleep(0.25)
        except (WebSocketDisconnect, OSError):
            pass

    return app


if __name__ == "__main__":
    import uvicorn

    parser = argparse.ArgumentParser(description="Цифровая теплица")
    parser.add_argument("--demo", action="store_true", help="те же программы C в локальных процессах без Wokwi")
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    uvicorn.run(create_app("demo" if args.demo else "wokwi"), host="127.0.0.1", port=args.port)
