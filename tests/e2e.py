"""Run against a started server: Python 3.11+: python tests/e2e.py [port]."""

import asyncio
import json
import sys
from urllib.request import Request, urlopen

from websockets.asyncio.client import connect

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
BASE = f"127.0.0.1:{PORT}"


def select(name):
    with urlopen(Request(f"http://{BASE}/scenario/{name}", method="POST"), timeout=3) as response:
        assert response.status == 200


async def main():
    async with connect(f"ws://{BASE}/ws") as ws:
        async def wait(predicate, timeout=14):
            async with asyncio.timeout(timeout):
                while True:
                    sample = json.loads(await ws.recv())
                    if predicate(sample):
                        return sample

        def command(sample, frame):
            return any(event["route"] == "МК 1 → МК 2" and event["frame"] == frame
                       for event in sample["journal"])

        initial = await wait(lambda s: s["link"]["fresh"] and len(s["sensors"]) == 7)
        print("Связь двух программ, семь показаний:", initial["mode"])

        await asyncio.to_thread(select, "heat")
        heat = await wait(lambda s: s["scenario"] == "heat" and s["state"]["angle"] == 90
                          and command(s, "SERV:090;") and s["sensors"]["air"] < 29)
        print("Форточка 90°, температура снизилась до", heat["sensors"]["air"])

        await asyncio.to_thread(select, "sun")
        sun = await wait(lambda s: s["scenario"] == "sun" and s["state"]["steps"] == 200
                         and command(s, "STEP:FWD,200;") and s["sensors"]["inside_light"] < 30)
        print("Штора 200 шагов, свет снаружи и внутри:",
              sun["sensors"]["outside_light"], sun["sensors"]["inside_light"])

        await asyncio.to_thread(select, "dry")
        await wait(lambda s: s["scenario"] == "dry" and command(s, "STEP:REV,200;")
                   and s["state"]["steps"] == 0 and s["state"]["pump"])
        wet = await wait(lambda s: s["scenario"] == "dry" and not s["state"]["pump"]
                         and command(s, "PUMP:003;") and s["sensors"]["soil"] > 32
                         and s["sensors"]["water"] < 72)
        print("Полив завершён, грунт и бак:", wet["sensors"]["soil"], wet["sensors"]["water"])

        for scenario, block in (("empty", "water"), ("fault", "sensor")):
            await asyncio.to_thread(select, scenario)
            await wait(lambda s: s["scenario"] == scenario and s["irrigation_block"] == block
                       and not s["state"]["pump"])
            print("Блокировка:", block)

        await asyncio.to_thread(select, "morning")
        morning = await wait(lambda s: s["scenario"] == "morning" and s["condensation"])
        print("Стекло и точка росы:", morning["sensors"]["glass"], morning["dew_point"])


if __name__ == "__main__":
    asyncio.run(main())
