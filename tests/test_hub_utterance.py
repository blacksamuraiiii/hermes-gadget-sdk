"""A capture that holds no speech never becomes an utterance.

The path this pins down is the one that used to hang a device for half an hour: an utterance
whose transcript comes back empty still starts a turn. The hub now answers the device and
starts nothing.
"""

from __future__ import annotations

import asyncio
import struct
from types import SimpleNamespace

from hermes_gadget_plugin import hub as hubmod

RATE = 16000
NOTICE = hubmod.NO_SPEECH_NOTICE


class _Delegate:
    def __init__(self):
        self.utterances = []

    async def on_utterance(self, session, msg_id, wav, seconds):
        self.utterances.append((msg_id, round(seconds, 2)))


class _Session:
    """Only what ``_h_audio_end`` touches: the upload, the notice, and how work is spawned."""

    device_id = "hg-1"

    def __init__(self, pcm: bytes):
        self._upload = hubmod._Upload(id="u1", stream=1, rate=RATE, started=0.0, pcm=bytearray(pcm))
        self.notices: list[str] = []
        self.spawned: list = []

    async def send_notice(self, text: str) -> None:
        self.notices.append(text)

    def spawn(self, coro):
        self.spawned.append(coro)
        return coro


def _pcm(seconds: float, amplitude: int, rate: int = RATE) -> bytes:
    return struct.pack("<h", amplitude) * int(rate * seconds)


def _end(session: _Session):
    delegate = _Delegate()
    hub = SimpleNamespace(delegate=delegate)
    asyncio.run(hubmod.DeviceHub._h_audio_end(hub, session, {"stream": 1}))
    return delegate


def test_a_capture_quieter_than_the_floor_starts_no_turn():
    session = _Session(_pcm(3.0, 5))  # three seconds of a room, no voice
    delegate = _end(session)
    assert session.notices == [NOTICE]
    assert session.spawned == []
    assert delegate.utterances == []


def test_a_capture_shorter_than_a_moment_starts_no_turn():
    session = _Session(_pcm(0.2, 3000))  # loud, but a tap
    delegate = _end(session)
    assert session.notices == [NOTICE]
    assert session.spawned == []
    assert delegate.utterances == []


def test_speech_reaches_the_adapter():
    session = _Session(_pcm(1.5, 800))
    delegate = _end(session)
    assert session.notices == []
    assert len(session.spawned) == 1
    asyncio.run(session.spawned[0])
    assert delegate.utterances == [("u1", 1.5)]


def test_both_gates_drop_the_same_short_capture():
    """One threshold, one answer: the hub and the adapter judge a 0.3 s press the same way."""
    pcm = _pcm(0.3, 3000)  # loud, but a tap
    session = _Session(pcm)
    delegate = _end(session)
    assert session.notices == [NOTICE]
    assert delegate.utterances == []
    assert hubmod.audio.utterance_rejection(hubmod.audio.wav_bytes(pcm, RATE), 0.3) is not None
