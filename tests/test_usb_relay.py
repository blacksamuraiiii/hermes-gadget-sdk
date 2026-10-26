"""tools/usb_relay.py: the offline selftest, the reopen backoff, and a maximum-size frame.

``--selftest`` also runs in CI (see .github/workflows/ci.yml); running it here means a broken
SLIP codec fails the test suite, not only the release. The backoff is pinned because the relay
used to log "port missing" every 5 s forever — 430 identical lines in one afternoon — which is
how a port Windows had stopped (device code 43) stayed invisible.
"""

from __future__ import annotations

import importlib.util
import sys
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
