"""tools/usb_relay.py: the offline selftest, the reopen backoff, and a maximum-size frame.

``--selftest`` also runs in CI (see .github/workflows/ci.yml); running it here means a broken
SLIP codec fails the test suite, not only the release. The backoff is pinned because the relay
used to log "port missing" every 5 s forever — 430 identical lines in one afternoon — which is
how a port Windows had stopped (device code 43) stayed invisible.
"""

from __future__ import annotations

import asyncio
import importlib.util
import sys
import time
import types
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]


def _load():
    spec = importlib.util.spec_from_file_location("usb_relay_under_test", REPO_ROOT / "tools" / "usb_relay.py")
    assert spec and spec.loader, "cannot load tools/usb_relay.py"
    module = importlib.util.module_from_spec(spec)
    sys.modules["usb_relay_under_test"] = module
    spec.loader.exec_module(module)
    return module


relay = _load()


def test_the_offline_selftest_passes(capsys):
    assert relay.selftest() == 0
    assert "all checks passed" in capsys.readouterr().out


def test_reopen_waits_double_up_to_a_minute():
    assert [relay.reopen_delay(n) for n in range(1, 6)] == [5, 10, 20, 40, 60]
    assert relay.reopen_delay(6) == 60
    assert relay.reopen_delay(60) == 60


def test_the_log_names_each_stage_instead_of_every_retry():
    # Stages 1..5 (5, 10, 20, 40, 60 s) are announced; 6..9 are DEBUG; the tenth says so again.
    assert [n for n in range(1, 8) if relay.reopen_is_worth_logging(n)] == [1, 2, 3, 4, 5]
    assert relay.reopen_is_worth_logging(10)
    assert not relay.reopen_is_worth_logging(11)
    assert relay.reopen_is_worth_logging(20)


def test_a_maximum_size_binary_frame_survives_the_codec():
    payload = bytes(range(256)) * (relay.MAX_FRAME // 256 - 1)  # holds 0xC0 and 0xDB, so it escapes
    frames, lines = relay.SlipDecoder().feed(relay.encode_frame(relay.T_BINARY, payload))
    assert not lines
    assert frames == [(relay.T_BINARY, payload)]


def _relay():
    return relay.Relay(types.SimpleNamespace(port="COM-test", vid=0x303A, pid=0x1001,
                                             target="ws://127.0.0.1:8765", path="/gadget"))


def test_two_recoveries_in_flight_open_the_port_once():
    """The watchdog, a read failure and a silent device all call recover_serial within a second.

    Opening twice at once leaves the first handle open on the same port, and two readers then
    split the bytes into frames neither can decode.
    """
    r = _relay()
    opens = []

    def slow_open():
        opens.append(1)
        time.sleep(0.05)  # long enough for the second caller to arrive

    r.open_serial = slow_open

    async def both():
        await asyncio.gather(r.recover_serial("watchdog"), r.recover_serial("read failed"))

    asyncio.run(both())
    assert len(opens) == 1
    assert r._recovering is False  # the guard is released, so a later outage can recover


class _FakePort:
    """A port that hands back its bytes once, then nothing (what a pulled cable looks like)."""

    def __init__(self, data: bytes) -> None:
        self._data = data

    def read(self, size: int) -> bytes:
        data, self._data = self._data, b""
        return data

    def close(self) -> None:
        pass


def test_a_frame_the_ws_refuses_does_not_kill_the_serial_pump():
    """A send into a just-closed WS raises; uncaught, the reader task dies and the device-to-Hermes
    direction stops for good while the relay still looks alive."""
    r = _relay()
    sent = []

    class BrokenWs:
        async def send(self, payload: bytes) -> None:
            sent.append(payload)
            raise ConnectionError("the ws just closed")

    r.ws = BrokenWs()
    frame = relay.encode_frame(relay.T_TEXT, b"hello")
    r.ser = _FakePort(frame + frame)  # two frames, the second after the first one broke the ws

    async def pump():
        task = asyncio.create_task(r.serial_reader())
        await asyncio.sleep(0.4)
        alive = not task.done()
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass
        return alive

    assert asyncio.run(pump()) is True
    assert len(sent) == 1  # the first frame was tried; the link was dropped instead of the task
