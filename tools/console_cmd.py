"""Send one console command to a gadget over USB serial, then print its reply.

The relay multiplexes the port: SLIP frames carry the WebSocket and plain bytes
are the console. That means the port must be free while this runs - stop the
relay first, then start it again afterwards:

    taskkill /PID <relay pid> /F
    python console_cmd.py --cmd "set screen_timeout 60"
    (start the relay again)

Opening the port pulses DTR, which resets the board, so the first seconds carry
boot logs rather than replies. `--wait-ready` (the default) waits for the boot
line before sending; a command that still lands early answers
`@error ESP_ERR_INVALID_STATE` and is retried.

Console commands and their replies are documented under "Serial console" in
docs/hardware.md. A reply starts with '@': @ok, @value, @error, @log.
"""

from __future__ import annotations

import argparse
import sys
import time

import serial

# Lines the firmware prints once it can take console commands.
READY_MARKERS = ("USB link ready", "hg: online")
# The firmware's answer while it is still booting.
BOOTING_REPLY = "@error ESP_ERR_INVALID_STATE"


def _wait_ready(link: serial.Serial, timeout_s: float) -> bool:
    deadline = time.time() + timeout_s
    seen = ""
    while time.time() < deadline:
        chunk = link.read(4096)
        if not chunk:
            continue
        seen = (seen + chunk.decode("utf-8", "replace"))[-4096:]
        if any(marker in seen for marker in READY_MARKERS):
            return True
    return False


def _read_reply(link: serial.Serial, wait_s: float) -> tuple[int, str]:
    """The reply as (exit code, text to print): 0 ok, 1 '@error', 2 no reply."""
    buf = ""
    deadline = time.time() + wait_s
    multiline = False
    while time.time() < deadline:
        chunk = link.read(4096)
        if not chunk:
            continue
        buf += chunk.decode("utf-8", "replace")
        lines = buf.splitlines()
        # A console reply is one line starting with '@'; `diag log` opens a
        # block with "@log ..." and closes it with "@log end".
        for line in lines:
            if line.startswith("@log"):
                multiline = True
            if multiline and line.strip() == "@log end":
                return 0, "\n".join(l for l in lines if l.strip())
            if not multiline and line.startswith("@"):
                return (0 if not line.startswith("@error") else 1), line.strip()
    tail = "\n".join(buf.splitlines()[-8:])
    return 2, f"no '@' reply within {wait_s}s; last bytes:\n{tail}"


def run(port: str, baud: int, command: str, wait_s: float, *,
        wait_ready: bool = True, ready_s: float = 5.0, attempts: int = 3) -> int:
    with serial.Serial(port, baud, timeout=0.2) as link:
        if wait_ready and not _wait_ready(link, ready_s):
            print(f"device did not announce itself within {ready_s}s; sending anyway", file=sys.stderr)
        code, text = 2, ""
        for attempt in range(1, attempts + 1):
            time.sleep(0.3)
            link.reset_input_buffer()
            link.write((command + "\n").encode("utf-8"))
            link.flush()
            code, text = _read_reply(link, wait_s)
            if text.startswith(BOOTING_REPLY) and attempt < attempts:
                time.sleep(0.5)  # still booting: let it settle and try again
                continue
            break
        print(text, file=sys.stderr if code == 2 else sys.stdout)
        return code


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--port", default="COM5", help="serial port (default: COM5)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--cmd", required=True, help='console command, e.g. "set volume 40"')
    parser.add_argument("--wait", type=float, default=5.0, help="seconds to wait for the reply")
    parser.add_argument("--wait-ready", dest="wait_ready", action="store_true", default=True,
                        help="wait for the boot line before sending (default)")
    parser.add_argument("--no-wait-ready", dest="wait_ready", action="store_false",
                        help="send at once, for a board that is already up")
    parser.add_argument("--ready-timeout", type=float, default=5.0,
                        help="seconds to wait for the boot line (default 5)")
    args = parser.parse_args(argv)
    return run(args.port, args.baud, args.cmd, args.wait,
               wait_ready=args.wait_ready, ready_s=args.ready_timeout)


if __name__ == "__main__":
    raise SystemExit(main())
