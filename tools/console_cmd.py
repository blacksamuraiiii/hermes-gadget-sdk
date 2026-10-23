"""Send one console command to a gadget over USB serial, then print its reply.

The relay multiplexes the port: SLIP frames carry the WebSocket and plain bytes
are the console. That means the port must be free while this runs - stop the
relay first, then start it again afterwards:

    taskkill /PID <relay pid> /F
    python console_cmd.py --cmd "set screen_timeout 60"
    (start the relay again)

Console commands and their replies are documented under "Serial console" in
docs/hardware.md. A reply starts with '@': @ok, @value, @error, @log.
"""

from __future__ import annotations

import argparse
import sys
import time

import serial


def run(port: str, baud: int, command: str, wait_s: float) -> int:
    with serial.Serial(port, baud, timeout=0.2) as link:
        time.sleep(0.3)
        link.reset_input_buffer()
        link.write((command + "\n").encode("utf-8"))
        link.flush()

        buf = ""
        deadline = time.time() + wait_s
        multiline = False
        while time.time() < deadline:
            chunk = link.read(4096)
            if chunk:
                buf += chunk.decode("utf-8", "replace")
                # A console reply is one line starting with '@'; `diag log` opens a
                # block with "@log ..." and closes it with "@log end".
                lines = buf.splitlines()
                for line in lines:
                    if line.startswith("@log"):
                        multiline = True
                    if multiline and line.strip() == "@log end":
                        print("\n".join(l for l in lines if l.strip()))
                        return 0
                    if not multiline and line.startswith("@"):
                        print(line.strip())
                        return 0 if not line.startswith("@error") else 1
        tail = "\n".join(buf.splitlines()[-8:])
        print(f"no '@' reply within {wait_s}s; last bytes:\n{tail}", file=sys.stderr)
        return 2


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--port", default="COM5", help="serial port (default: COM5)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--cmd", required=True, help='console command, e.g. "set volume 40"')
    parser.add_argument("--wait", type=float, default=5.0, help="seconds to wait for the reply")
    args = parser.parse_args(argv)
    return run(args.port, args.baud, args.cmd, args.wait)


if __name__ == "__main__":
    raise SystemExit(main())
