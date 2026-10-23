"""Reminders on the gadget: an approval waiting, a question waiting, a turn done.

Hermes already tells the device ``prompt``, ``reply`` and ``display`` on a
conversation that happens *on* the gadget. This module covers the other case:
work running in a terminal or another chat while nobody watches that screen.
Hermes blocks for a human at three points, and all three fire ``on_human_input_*``
or ``on_session_end`` plugin hooks in whichever process runs the agent, so the
bridge needs no polling and no new protocol.

A reminder is a fixed phrase: one cue cut once by ``tools/make_cues.py`` and one
short card. Nothing here starts a turn, calls text-to-speech or sends a chat
message, so a reminder costs no tokens, no network round trip and no model call.
The card follows the device's ``charset``: a firmware with CJK glyphs gets the
Chinese title and body, an ASCII-only build keeps the English ones. The cue file
speaks Chinese either way.

The screen goes dark after a timeout, and the server holds cards for a dark
screen. Playing a cue first lights the screen, so the card that follows lands on
a lit display; if it is still held, the hub delivers it when the device reports
itself lit.
"""

from __future__ import annotations

import asyncio
import logging
import threading
import time
from typing import Any, Callable, Optional

logger = logging.getLogger(__name__)

# One entry per thing worth interrupting someone for. ``cue`` names the audio
# file (``<plugin data>/gadget/cues/<cue>.wav``); a missing file only costs the
# sound, never the card.
CUES: dict[str, dict[str, Any]] = {
    "approval": {"cue": "approval", "title": "APPROVAL", "body": "A command is waiting", "ttl_s": 20.0,
                 "title_zh": "需要授权", "body_zh": "有命令等待确认"},
    "sudo": {"cue": "approval", "title": "PASSWORD", "body": "The terminal wants it", "ttl_s": 20.0,
             "title_zh": "需要口令", "body_zh": "终端要输口令"},
    "clarify": {"cue": "clarify", "title": "QUESTION", "body": "Hermes needs a choice", "ttl_s": 20.0,
                "title_zh": "请选择", "body_zh": "Hermes 等你回话"},
    "done": {"cue": "done", "title": "DONE", "body": "", "ttl_s": 12.0,
             "title_zh": "已完成", "body_zh": ""},
}

# Hermes reports which kind of block this is; unknown kinds are not announced.
KIND_CUES = {"approval": "approval", "sudo": "sudo", "clarify": "clarify"}

COOLDOWN_S = 4.0        # the same cue twice inside this window is one reminder
MIN_TURN_S = 45.0       # a turn shorter than this is not worth a "done"


def card_text(spec: dict[str, Any], charset: str) -> tuple[str, str]:
    """The card's title and body in the language the device's font can draw."""
    wide = charset.replace("-", "").lower() in ("utf8", "utf_8")
    title = spec.get("title_zh") if wide else None
    body = spec.get("body_zh") if wide else None
    return (title or spec.get("title") or "", body if body is not None else (spec.get("body") or ""))


def _announce_now(adapter, spec: dict[str, Any]) -> None:
    """Hand the reminder to the hub's loop without blocking the agent thread."""
    loop = getattr(adapter, "loop", None)
    if loop is None or loop.is_closed():
        return
    coro = adapter.push_reminder(spec["cue"], spec["title"], spec["body"], spec["ttl_s"], spec=spec)
    future = asyncio.run_coroutine_threadsafe(coro, loop)
    future.add_done_callback(_report_failure)


def _report_failure(future) -> None:
    try:
        future.result()
    except Exception as exc:  # a reminder must never take a turn down
        logger.debug("[Gadget] reminder failed: %s", exc)


class ReminderBridge:
    """Turns Hermes's human-input hooks into one cue and one card on the gadget."""

    HOOKS = ("pre_llm_call", "on_session_end", "on_human_input_request", "on_human_input_resolved")

    def __init__(self, clock: Callable[[], float] = time.monotonic):
        self._clock = clock
        self._lock = threading.Lock()
        self._turns: dict[str, tuple[float, str]] = {}
        self._last: dict[str, float] = {}
        self._enabled = True
        self._min_turn_s = MIN_TURN_S

    # -- registration ------------------------------------------------------------------

    def register(self, ctx) -> None:
        for name in self.HOOKS:
            ctx.register_hook(name, getattr(self, name))

    def configure(self, *, enabled: Optional[bool] = None, min_turn_s: Optional[float] = None) -> None:
        """Called by the adapter once it knows the platform's settings."""
        with self._lock:
            if enabled is not None:
                self._enabled = bool(enabled)
            if min_turn_s is not None:
                self._min_turn_s = max(0.0, float(min_turn_s))

    # -- hooks (keyword arguments only; Hermes adds fields over time) -------------------

    def pre_llm_call(self, session_id=None, platform=None, parent_session_id=None, **_kw) -> None:
        if parent_session_id:
            return None  # a subagent works inside a turn that is already tracked
        with self._lock:
            self._turns[str(session_id or "")] = (self._clock(), str(platform or ""))
        return None

    def on_session_end(self, session_id=None, platform=None, completed=None, interrupted=False, **_kw) -> None:
        if interrupted or completed is False:
            return None  # a turn cut short (/stop, Ctrl-C) is not "done"
        with self._lock:
            started = self._turns.pop(str(session_id or ""), None)
        if started is None:
            return None  # not a turn this bridge saw begin (a subagent, or a replay)
        began, began_platform = started
        if _is_gadget(platform) or _is_gadget(began_platform):
            return None  # the user is looking at the device already
        with self._lock:
            min_turn_s = self._min_turn_s
        if self._clock() - began < min_turn_s:
            return None  # a quick answer is not worth a ring
        self._announce("done", how="session-end")
        return None

    def on_human_input_request(self, kind=None, platform=None, request_id=None, **_kw) -> None:
        cue = KIND_CUES.get(str(kind or ""))
        if cue is None or _is_gadget(platform):
            return None
        self._announce(cue, key=str(request_id or ""), how=str(kind))
        return None

    def on_human_input_resolved(self, **_kw) -> None:
        return None  # the cue is short and the card expires on its own

    # -- delivery ----------------------------------------------------------------------

    def _announce(self, cue: str, *, key: str = "", how: str = "") -> None:
        spec = CUES.get(cue)
        if spec is None:
            return
        now = self._clock()
        with self._lock:
            if not self._enabled:
                return
            stamp = f"{cue}:{key}"
            if now - self._last.get(stamp, -1e9) < COOLDOWN_S or now - self._last.get(cue, -1e9) < COOLDOWN_S:
                return
            self._last[stamp] = self._last[cue] = now
            if len(self._last) > 64:  # a long session must not grow this without bound
                self._last = {k: v for k, v in self._last.items() if now - v < 600}
        adapter = _adapter()
        if adapter is None:
            logger.debug("[Gadget] no gadget adapter in this process; %s reminder dropped", how)
            return
        _announce_now(adapter, spec)


def _is_gadget(platform: Any) -> bool:
    return str(platform or "").strip().lower() == "gadget"


def _adapter():
    """The adapter serving this process, if the gadget platform is running here."""
    from . import runtime

    try:
        from gateway.session_context import get_session_env

        return runtime.active(get_session_env("HERMES_SESSION_PROFILE") or None)
    except Exception:
        return runtime.active()


BRIDGE = ReminderBridge()  # the plugin registers its hooks
