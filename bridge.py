"""Two independent byte streams with line framing and bounded write queues."""

import asyncio
from pathlib import Path
from queue import Empty, Full, Queue
from threading import Event, Thread

PORTS = {"controller": 4001, "actuator": 4002}
MAX_FRAME = 128


class Frames:
    def __init__(self):
        self.buffer = bytearray()
        self.dropping = False

    def feed(self, data):
        for byte in data:
            if byte == 10:
                if not self.dropping and self.buffer:
                    frame = bytes(self.buffer).removesuffix(b"\r")
                    if frame and all(32 <= char <= 126 for char in frame):
                        yield frame
                self.buffer.clear()
                self.dropping = False
            elif not self.dropping:
                if len(self.buffer) == MAX_FRAME:
                    self.dropping = True
                    self.buffer.clear()
                else:
                    self.buffer.append(byte)


class WokwiPort:
    def __init__(self, name, loop, on_frame, on_link):
        self.name = name
        self.loop = loop
        self.on_frame = on_frame
        self.on_link = on_link
        self.stopping = Event()
        self.outbox = None
        self.thread = Thread(target=self.run, name=name, daemon=True)

    async def start(self):
        self.thread.start()

    def send(self, frame):
        outbox = self.outbox
        if outbox is not None:
            try:
                outbox.put_nowait(frame + b"\n")
                return True
            except Full:
                pass
        return False

    def run(self):
        import serial

        address = f"rfc2217://127.0.0.1:{PORTS[self.name]}"
        while not self.stopping.is_set():
            try:
                with serial.serial_for_url(address, baudrate=115200, timeout=0.08) as device:
                    self.outbox = Queue(maxsize=16)
                    self.loop.call_soon_threadsafe(self.on_link, self.name, True)
                    frames = Frames()
                    while not self.stopping.is_set():
                        try:
                            data = self.outbox.get_nowait()
                        except Empty:
                            pass
                        else:
                            device.write(data)
                        data = device.read(min(256, max(1, device.in_waiting)))
                        for frame in frames.feed(data):
                            self.loop.call_soon_threadsafe(self.on_frame, self.name, frame)
            except (OSError, ValueError):
                pass
            finally:
                if self.outbox is not None:
                    self.outbox = None
                    self.loop.call_soon_threadsafe(self.on_link, self.name, False)
            self.stopping.wait(1)

    async def close(self):
        self.stopping.set()
        await asyncio.to_thread(self.thread.join, 3)


class NativePort:
    def __init__(self, name, loop, on_frame, on_link):
        self.name = name
        self.on_frame = on_frame
        self.on_link = on_link
        self.process = None
        self.task = None

    async def start(self):
        binary = Path(__file__).parent / "firmware" / "build" / "native" / self.name
        if not binary.is_file():
            raise RuntimeError("Сначала выполните make -C firmware native")
        self.process = await asyncio.create_subprocess_exec(
            str(binary), stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE
        )
        self.on_link(self.name, True)
        self.task = asyncio.create_task(self.read())

    async def read(self):
        frames = Frames()
        try:
            while data := await self.process.stdout.read(256):
                for frame in frames.feed(data):
                    self.on_frame(self.name, frame)
        finally:
            self.on_link(self.name, False)

    def send(self, frame):
        if self.process and self.process.returncode is None and not self.process.stdin.is_closing():
            self.process.stdin.write(frame + b"\n")
            return True
        return False

    async def close(self):
        if self.process is not None and self.process.returncode is None:
            self.process.terminate()
            await self.process.wait()
        if self.task is not None:
            await self.task
