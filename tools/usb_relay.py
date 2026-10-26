#!/usr/bin/env python3
"""Bridge an ESP32-S3 Hermes Gadget to the plugin over USB, without Wi-Fi.

The device sends SLIP frames on its USB Serial/JTAG port; the same port also
carries the console log, so frames are told apart by the 0xC0 delimiter:

    frame = 0xC0 | type | payload | 0xC0
    inside the payload: 0xC0 -> 0xDB 0xDC, 0xDB -> 0xDB 0xDD

    type 0x01 text      payload is UTF-8 JSON
    type 0x02 binary    payload is audio
    type 0x03 close     no payload
    type 0x04 ping      no payload, answer with the same
    type 0x05 hello     {"v":1,"sub":"hermes-gadget.v1","path":"/gadget"}
    type 0x06 hello_ack {"ok":1} or {"ok":0,"err":"..."}

Run on Windows (it needs the COM port):

    C:\\Users\\<you>\\miniconda3\\envs\\gadget-relay\\python.exe tools\\usb_relay.py

The relay owns the port while it runs: close the Web Serial page and any serial
monitor first, or opening the port fails with "Access is denied".
"""
from __future__ import annotations

import argparse
import asyncio
import json
import logging
import signal
import sys
import time

LOG = logging.getLogger("relay")

SLIP_END = 0xC0
SLIP_ESC = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD

T_TEXT = 0x01
T_BINARY = 0x02
T_CLOSE = 0x03
T_PING = 0x04
T_HELLO = 0x05
T_HELLO_ACK = 0x06

MAX_FRAME = 16 * 1024
PING_EVERY = 10.0
IDLE_TIMEOUT = 30.0
# The board answers a bridge ping every few seconds, so this much silence means its
# USB side has wedged (seen twice in one evening). The relay then reopens the port
# and rescans for the device instead of waiting for someone to replug the cable.
DEVICE_SILENT_S = 60.0
REOPEN_EVERY_S = 5.0
# A port that stays missing is usually not a flapping cable: the board is unplugged, or Windows
# has stopped the port altogether (device code 43 — see README_zh.md). Retrying every 5 s forever
# floods the log and keeps poking the USB hub, so each failure doubles the wait up to REOPEN_MAX_S.
REOPEN_MAX_S = 60.0
# After this long with no port at all, say once what the fix is: a hand on the cable, not a retry.
PORT_MISSING_HINT_S = 120.0
WS_PATH_DEFAULT = "/gadget"
SUB_DEFAULT = "hermes-gadget.v1"


def reopen_delay(attempt: int) -> float:
    """Seconds to wait before reopen attempt ``attempt`` (1-based): 5, 10, 20, 40, then 60."""
    if attempt <= 1:
        return REOPEN_EVERY_S
    return min(REOPEN_EVERY_S * (2 ** (attempt - 1)), REOPEN_MAX_S)


def reopen_is_worth_logging(attempt: int) -> bool:
    """True on a backoff stage change (or the first attempt) and on every tenth retry after that.

    430 identical "port missing" lines in one afternoon is how a wedged port hides: nobody reads
    the log any more. One line per stage keeps the shape visible, the rest go to DEBUG.
    """
    return attempt <= 1 or attempt % 10 == 0 or reopen_delay(attempt) != reopen_delay(attempt - 1)


def encode_frame(ftype: int, payload: bytes = b"") -> bytes:
    """SLIP-encode one frame, escaping the payload only."""
    out = bytearray((SLIP_END, ftype))
    for byte in payload:
        if byte == SLIP_END:
            out += bytes((SLIP_ESC, SLIP_ESC_END))
        elif byte == SLIP_ESC:
            out += bytes((SLIP_ESC, SLIP_ESC_ESC))
        else:
            out.append(byte)
    out.append(SLIP_END)
    return bytes(out)


class SlipDecoder:
    """Feeds serial bytes in, yields (type, payload) frames.

    Bytes outside a frame are console output; they accumulate into lines the
    caller can log, so a device log line can never be mistaken for a frame.
    """

    def __init__(self) -> None:
        self._in_frame = False
        self._need_type = False
        self._escaped = False
        self._overflow = False
        self._ftype = 0
        self._payload = bytearray()
        self._line = bytearray()

    def feed(self, data: bytes) -> tuple[list[tuple[int, bytes]], list[str]]:
        frames: list[tuple[int, bytes]] = []
        lines: list[str] = []
        for byte in data:
            if byte == SLIP_END:
                if self._in_frame:
                    if not self._overflow and not self._need_type:
                        frames.append((self._ftype, bytes(self._payload)))
                    elif self._overflow:
                        LOG.warning("dropped an oversized frame")
                    self._reset_frame()
                else:
                    if self._line:
                        lines.append(self._line.decode("utf-8", "replace"))
                    self._line.clear()
                    self._in_frame = True
                    self._need_type = True
                continue
            if self._in_frame:
                if self._need_type:
                    self._ftype = byte
                    self._need_type = False
                elif self._escaped:
                    self._escaped = False
                    self._payload.append(SLIP_END if byte == SLIP_ESC_END else SLIP_ESC if byte == SLIP_ESC_ESC else byte)
                elif byte == SLIP_ESC:
                    self._escaped = True
                elif len(self._payload) < MAX_FRAME:
                    self._payload.append(byte)
                else:
                    self._overflow = True
                continue
            if byte in (0x0D,):
                continue
            if byte == 0x0A:
                if self._line:
                    lines.append(self._line.decode("utf-8", "replace"))
                self._line.clear()
            elif len(self._line) < 512:
                self._line.append(byte)
            else:
                self._line.clear()
        return frames, lines

    def _reset_frame(self) -> None:
        self._in_frame = False
        self._need_type = False
        self._escaped = False
        self._overflow = False
        self._payload.clear()


def find_port(vid: int, pid: int):
    from serial.tools import list_ports

    candidates = [p for p in list_ports.comports() if p.vid == vid and p.pid == pid]
    if not candidates:
        seen = ", ".join(f"{p.device}({p.vid:04X}:{p.pid:04X})" for p in list_ports.comports() if p.vid) or "none"
        # A RuntimeError, not SystemExit: the caller retries until the board shows up.
        raise RuntimeError(f"no device with VID:PID {vid:04X}:{pid:04X}; serial devices: {seen}")
    if len(candidates) > 1:
        LOG.warning("several matching ports, using %s", candidates[0].device)
    return candidates[0].device


class Relay:
    def __init__(self, args) -> None:
        self.args = args
        self.ser = None
        self.decoder = SlipDecoder()
        self.ws = None
        self.hello: dict | None = None
        self.last_rx = time.monotonic()
        self.last_tx = time.monotonic()
        self.last_device = time.monotonic()  # last byte read from the board
        self.write_lock = asyncio.Lock()
        self.reopen_attempts = 0             # consecutive failed opens since the port was last up
        self.reopen_since: float | None = None
        self.port_missing_hinted = False

    # ---- serial -----------------------------------------------------------
    def open_serial(self) -> None:
        import serial

        port = self.args.port or find_port(self.args.vid, self.args.pid)
        self.ser = serial.Serial()
        self.ser.port = port
        self.ser.baudrate = 115200
        self.ser.timeout = 0.05
        self.ser.dtr = False  # must precede open: DTR/RTS reset the board
        self.ser.rts = False
        self.ser.open()
        self.ser.dtr = False
        self.ser.rts = False
        LOG.info("serial %s open (VID:PID %04X:%04X)", port, self.args.vid, self.args.pid)
        if self.args.reset:
            # The device sends its hello once, at start-up. Pulse the reset line
            # so it starts again with this reader already open.
            self.ser.rts = True
            time.sleep(0.3)
            self.ser.rts = False
            LOG.info("board reset; waiting for its hello")

    async def recover_serial(self, why: str) -> None:
        """Close the port, reopen it (rescans by VID:PID) and forget any half frame."""
        old, self.ser = self.ser, None
        if old is not None:
            try:
                old.close()
            except Exception:  # noqa: BLE001 - already gone
                pass
        try:
            await asyncio.to_thread(self.open_serial)
        except Exception as exc:  # noqa: BLE001 - the board may be gone for a while
            await self.wait_before_reopen(f"serial reopen failed ({why})", exc)
            return
        self.decoder = SlipDecoder()  # a fresh reader: drop any half frame
        self.last_device = self.last_rx = time.monotonic()
        self.port_came_back(why)

    def port_came_back(self, why: str) -> None:
        """The port is open again: forget the backoff and say how long it took."""
        if self.reopen_attempts:
            LOG.info("serial recovered (%s) after %d attempt(s)", why, self.reopen_attempts)
        else:
            LOG.info("serial recovered (%s)", why)
        self.reopen_attempts = 0
        self.reopen_since = None
        self.port_missing_hinted = False

    async def wait_before_reopen(self, why: str, exc: Exception) -> None:
        """Count a failed open, log it once per backoff stage, and wait longer each time.

        A port that is gone for minutes is a hand-on-the-cable problem: Windows may have stopped
        it as device code 43, where no reopen succeeds until the hub or the PC is reset. Say that
        once rather than repeating the same warning twice a minute.
        """
        self.reopen_attempts += 1
        if self.reopen_since is None:
            self.reopen_since = time.monotonic()
        delay = reopen_delay(self.reopen_attempts)
        line = "%s: %s (attempt %d, next try in %.0f s)"
        if reopen_is_worth_logging(self.reopen_attempts):
            LOG.warning(line, why, exc, self.reopen_attempts, delay)
        else:
            LOG.debug(line, why, exc, self.reopen_attempts, delay)
        missing = time.monotonic() - self.reopen_since
        if not self.port_missing_hinted and missing >= PORT_MISSING_HINT_S:
            self.port_missing_hinted = True
            LOG.warning("no board on the bus for %.0f s: if the cable is connected, Windows may have "
                        "stopped the port (device code 43) - unplug the hub's upstream cable for 3 s, "
                        "or see the USB section of README_zh.md", missing)
        await asyncio.sleep(delay)

    async def write_frame(self, ftype: int, payload: bytes = b"") -> None:
        if not self.ser:
            return
        async with self.write_lock:
            await asyncio.to_thread(self.ser.write, encode_frame(ftype, payload))
            self.last_tx = time.monotonic()
        LOG.debug("dev <- 0x%02X %d B", ftype, len(payload))

    # ---- websocket --------------------------------------------------------
    async def connect_ws(self) -> bool:
        from websockets.asyncio.client import connect

        if not self.hello:
            return False
        path = self.hello.get("path") or self.args.path
        sub = self.hello.get("sub") or SUB_DEFAULT
        url = self.args.target.rstrip("/") + path
        for attempt in range(3):
            try:
                self.ws = await connect(
                    url, subprotocols=[sub],  # type: ignore[arg-type]  # websockets types this as Subprotocol
                    max_size=None, ping_interval=None, open_timeout=10)
                LOG.info("ws %s connected (subprotocol %s)", url, self.ws.subprotocol)
                self.last_rx = self.last_tx = time.monotonic()
                await self.write_frame(T_HELLO_ACK, b'{"ok":1}')
                return True
            except Exception as exc:  # noqa: BLE001 - any failure means retry
                LOG.warning("ws %s failed (%d/3): %s", url, attempt + 1, exc)
                await asyncio.sleep(2)
        await self.write_frame(T_HELLO_ACK, b'{"ok":0,"err":"no plugin"}')
        LOG.error("the plugin is not reachable at %s; is the gadget platform running?", url)
        return False

    async def drop_ws(self, reason: str) -> None:
        if self.ws:
            LOG.info("closing ws: %s", reason)
            try:
                await self.ws.close()
            except Exception:  # noqa: BLE001
                pass
            self.ws = None

    # ---- pumps ------------------------------------------------------------
    async def serial_reader(self) -> None:
        while True:
            ser = self.ser
            if ser is None:
                await asyncio.sleep(0.2)  # reopen comes from watchdog or recover_serial
                continue
            try:
                data = await asyncio.to_thread(ser.read, 512)
            except Exception as exc:  # noqa: BLE001 - a closed or vanished port reads like this
                LOG.warning("serial read failed: %s", exc)
                await self.recover_serial("read failed")
                continue
            if not data:
                continue
            self.last_device = time.monotonic()
            self.last_rx = time.monotonic()
            frames, lines = self.decoder.feed(data)
            for line in lines:
                LOG.info("[dev] %s", line)
            for ftype, payload in frames:
                await self.on_frame(ftype, payload)

    async def on_frame(self, ftype: int, payload: bytes) -> None:
        if ftype == T_TEXT:
            text = payload.decode("utf-8", "replace")
            LOG.debug("dev -> text %d B", len(payload))
            if self.ws:
                await self.ws.send(text)
        elif ftype == T_BINARY:
            LOG.debug("dev -> binary %d B", len(payload))
            if self.ws:
                await self.ws.send(payload)
        elif ftype == T_PING:
            LOG.debug("ping from the device")  # never answer: the device echoes, so two echoes loop
        elif ftype == T_CLOSE:
            await self.drop_ws("device closed the link")
        elif ftype == T_HELLO:
            try:
                self.hello = json.loads(payload.decode("utf-8", "replace"))
            except json.JSONDecodeError:
                LOG.warning("bad hello: %r", payload[:120])
                return
            LOG.info("hello: %s", self.hello)
            await self.connect_ws()
        elif ftype == T_HELLO_ACK:
            LOG.info("device acknowledged: %s", payload.decode("utf-8", "replace"))
        else:
            LOG.warning("unknown frame type 0x%02X (%d B)", ftype, len(payload))

    async def ws_reader(self) -> None:
        while True:
            if not self.ws:
                await asyncio.sleep(0.2)
                continue
            try:
                message = await self.ws.recv()
            except Exception as exc:  # noqa: BLE001 - any failure ends the link
                LOG.info("ws recv ended: %s", exc)
                await self.write_frame(T_CLOSE)
                await self.drop_ws("ws recv ended")
                continue
            if isinstance(message, bytes):
                await self.write_frame(T_BINARY, message)
            else:
                await self.write_frame(T_TEXT, message.encode("utf-8"))

    async def watchdog(self) -> None:
        while True:
            await asyncio.sleep(1)
            if self.ser is None:
                await self.recover_serial("port missing")
                continue
            if time.monotonic() - self.last_device > DEVICE_SILENT_S:
                LOG.warning("no bytes from the board for %.0f s: reopening the port",
                            time.monotonic() - self.last_device)
                await self.drop_ws("device silent")
                await self.recover_serial("device silent")
                continue
            idle = time.monotonic() - max(self.last_rx, self.last_tx)
            if idle > IDLE_TIMEOUT and self.ws:
                LOG.warning("no traffic for %.0f s: dropping the link", idle)
                await self.write_frame(T_CLOSE)
                await self.drop_ws("idle timeout")
            elif time.monotonic() - self.last_tx > PING_EVERY:
                await self.write_frame(T_PING)

    async def run(self) -> int:
        # Wait for the board instead of exiting: a pulled cable or a wedged USB side
        # should cost a retry, not a restart someone has to notice.
        while True:
            try:
                self.open_serial()
                break
            except Exception as exc:  # noqa: BLE001 - no board on the bus yet
                await self.wait_before_reopen("waiting for the board", exc)
        self.reopen_attempts, self.reopen_since, self.port_missing_hinted = 0, None, False
        tasks = [asyncio.create_task(self.serial_reader(), name="serial"),
                 asyncio.create_task(self.ws_reader(), name="ws"),
                 asyncio.create_task(self.watchdog(), name="watchdog")]
        stop = asyncio.Event()
        loop = asyncio.get_running_loop()
        for sig in (signal.SIGINT, signal.SIGTERM):
            try:
                loop.add_signal_handler(sig, stop.set)
            except NotImplementedError:  # Windows: rely on KeyboardInterrupt
                pass
        try:
            await stop.wait()
        except (KeyboardInterrupt, asyncio.CancelledError):
            pass
        for task in tasks:
            task.cancel()
        await self.drop_ws("relay stopping")
        if self.ser:
            self.ser.close()
        LOG.info("relay stopped")
        return 0


def selftest() -> int:
    """Offline checks: SLIP round-trip and console/frame separation."""
    import random

    failures = 0

    def check(name: str, ok: bool, detail: str = "") -> None:
        nonlocal failures
        print(("PASS " if ok else "FAIL ") + name + (f" :: {detail}" if detail and not ok else ""))
        if not ok:
            failures += 1

    decoder = SlipDecoder()
    frames, lines = decoder.feed(encode_frame(T_TEXT, b'{"a":1}'))
    check("single frame", frames == [(T_TEXT, b'{"a":1}')] and not lines, repr(frames))

    rng = random.Random(7)
    round_trips = 0
    for _ in range(200):
        payload = bytes(rng.randrange(256) for _ in range(rng.randrange(0, 300)))
        frames, _ = SlipDecoder().feed(encode_frame(T_BINARY, payload))
        if frames == [(T_BINARY, payload)]:
            round_trips += 1
    check("200 random payloads incl. 0xC0/0xDB", round_trips == 200, f"{round_trips}/200")

    stream = b"boot: hello 0xC0 inside a log line\n" + encode_frame(T_TEXT, b'{"b":2}') + b"tail log\n"
    frames, lines = SlipDecoder().feed(stream)
    check("frame between log lines", frames == [(T_TEXT, b'{"b":2}')], repr(frames))
    check("log lines recovered", len(lines) == 2, repr(lines))

    split = encode_frame(T_TEXT, b"x" * 100)
    dec = SlipDecoder()
    got = []
    for i in range(0, len(split), 7):
        f, _ = dec.feed(split[i:i + 7])
        got.extend(f)
    check("byte-by-byte delivery", got == [(T_TEXT, b"x" * 100)], repr(got)[:80])

    frames, _ = SlipDecoder().feed(encode_frame(T_PING) + encode_frame(T_CLOSE))
    check("back-to-back frames", frames == [(T_PING, b""), (T_CLOSE, b"")], repr(frames))

    print(f"\n{'all checks passed' if not failures else str(failures) + ' check(s) failed'}")
    return 0 if not failures else 1


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description="Bridge a Hermes Gadget on USB to the local plugin.")
    parser.set_defaults(reset=False)
    parser.add_argument("--port", help="serial device (default: find it by VID:PID)")
    parser.add_argument("--vid", type=lambda s: int(s, 0), default=0x303A)
    parser.add_argument("--pid", type=lambda s: int(s, 0), default=0x1001)
    parser.add_argument("--target", default="ws://127.0.0.1:8765", help="plugin base URL")
    parser.add_argument("--path", default=WS_PATH_DEFAULT, help="path to ask for when the device sends none")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"])
    parser.add_argument("--log-file", help="also write the log here")
    parser.add_argument("--reset", dest="reset", action="store_true",
                        help="pulse RTS on start; on the Movecall Moji this leaves the "
                             "board silent, so the default connects to a running device")
    parser.add_argument("--selftest", action="store_true", help="run the offline checks and exit")
    return parser.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)
    if args.selftest:
        return selftest()
    handlers = [logging.StreamHandler(sys.stdout)]
    if args.log_file:
        handlers.append(logging.FileHandler(args.log_file, encoding="utf-8"))
    logging.basicConfig(level=getattr(logging, args.log_level), handlers=handlers,
                        format="%(asctime)s %(levelname)-7s %(message)s", datefmt="%H:%M:%S")
    reconfigure = getattr(sys.stdout, "reconfigure", None)  # not on every stream type
    if reconfigure:
        try:
            reconfigure(encoding="utf-8", errors="replace")
        except Exception:  # noqa: BLE001 - not fatal on odd consoles
            pass
    if sys.platform != "win32":
        LOG.warning("this runs on Windows: the serial port is only visible there")
    try:
        return asyncio.run(Relay(args).run())
    except KeyboardInterrupt:
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
