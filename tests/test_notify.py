"""Reminders: which Hermes events ring the gadget, and which stay quiet.

A reminder is one pre-cut cue and one fixed card — no turn, no text-to-speech —
so most of what is worth pinning down is *when not to ring*: the same approval
twice, a turn that just took a few seconds, a conversation that already happens
on the device, a subagent's turn inside its parent's, and a platform turned off
by configuration.
"""

from __future__ import annotations

import pytest

from hermes_gadget_plugin import notify


class Clock:
    def __init__(self, t: float = 1000.0):
        self.t = t

    def __call__(self) -> float:
        return self.t

    def tick(self, seconds: float) -> None:
        self.t += seconds


@pytest.fixture()
def bridge():
    clock = Clock()
    bridge = notify.ReminderBridge(clock=clock)
    bridge.clock = clock  # the test's handle on time
    return bridge


@pytest.fixture()
def announced(monkeypatch, bridge):
    """Records what the bridge would have put on a device."""
    sent: list[dict] = []
    monkeypatch.setattr(notify, "_adapter", lambda: object())
    monkeypatch.setattr(notify, "_announce_now", lambda adapter, spec: sent.append(dict(spec)))
    return sent


# -- the three things worth interrupting someone for ---------------------------------------

def test_an_approval_waits_so_the_gadget_says_so(announced, bridge):
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r1", session_id="s1")
    assert [c["title"] for c in announced] == ["APPROVAL"]
    assert announced[0]["cue"] == "approval"


def test_a_sudo_prompt_rings_with_its_own_card(announced, bridge):
    bridge.on_human_input_request(kind="sudo", platform="cli", request_id="r2")
    assert [c["title"] for c in announced] == ["PASSWORD"]
    assert announced[0]["cue"] == "approval"  # same cue: it is the same errand


def test_a_clarify_question_rings(announced, bridge):
    bridge.on_human_input_request(kind="clarify", platform="telegram", request_id="r3")
    assert [c["title"] for c in announced] == ["QUESTION"]


def test_a_finished_turn_rings_when_it_ran_long_enough(announced, bridge):
    bridge.pre_llm_call(session_id="s1", platform="cli")
    bridge.clock.tick(notify.MIN_TURN_S + 5)
    bridge.on_session_end(session_id="s1", platform="cli")
    assert [c["title"] for c in announced] == ["DONE"]


def test_a_quick_turn_stays_quiet(announced, bridge):
    bridge.pre_llm_call(session_id="s1", platform="cli")
    bridge.clock.tick(3)
    bridge.on_session_end(session_id="s1", platform="cli")
    assert announced == []


def test_a_turn_cut_short_is_not_done(announced, bridge):
    bridge.pre_llm_call(session_id="s1", platform="cli")
    bridge.clock.tick(notify.MIN_TURN_S + 5)
    bridge.on_session_end(session_id="s1", platform="cli", completed=False, interrupted=True, reason="keyboard_interrupt")
    assert announced == []


# -- what must not ring --------------------------------------------------------------------

def test_the_same_approval_does_not_ring_twice(announced, bridge):
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r1")
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r1")
    assert len(announced) == 1


def test_two_approvals_at_once_ring_once(announced, bridge):
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r1")
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r2")
    assert len(announced) == 1  # one errand, one cue; the second is inside the cooldown


def test_an_approval_after_the_cooldown_rings_again(announced, bridge):
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r1")
    bridge.clock.tick(notify.COOLDOWN_S + 1)
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r2")
    assert len(announced) == 2


def test_a_conversation_on_the_gadget_does_not_ring_itself(announced, bridge):
    bridge.pre_llm_call(session_id="s1", platform="gadget")
    bridge.clock.tick(notify.MIN_TURN_S + 5)
    bridge.on_session_end(session_id="s1", platform="gadget")
    bridge.on_human_input_request(kind="clarify", platform="gadget", request_id="r1")
    assert announced == []  # the device is already showing this


def test_a_subagent_turn_does_not_ring_for_its_parent(announced, bridge):
    bridge.pre_llm_call(session_id="sub", platform="cli", parent_session_id="parent")
    bridge.clock.tick(notify.MIN_TURN_S + 5)
    bridge.on_session_end(session_id="sub", platform="cli")
    assert announced == []


def test_a_turn_the_bridge_never_saw_begin_stays_quiet(announced, bridge):
    bridge.on_session_end(session_id="unknown", platform="cli")
    assert announced == []


def test_an_unknown_kind_of_block_is_ignored(announced, bridge):
    bridge.on_human_input_request(kind="something-new", platform="cli", request_id="r9")
    assert announced == []


def test_reminders_can_be_switched_off(announced, bridge):
    bridge.configure(enabled=False)
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r1")
    assert announced == []


def test_no_adapter_in_this_process_means_no_reminder(monkeypatch, bridge):
    called: list = []
    monkeypatch.setattr(notify, "_adapter", lambda: None)
    monkeypatch.setattr(notify, "_announce_now", lambda adapter, spec: called.append(spec))
    bridge.on_human_input_request(kind="approval", platform="cli", request_id="r1")
    assert called == []  # a fresh session on another machine must not raise


def test_the_cards_fit_an_ascii_screen():
    for spec in notify.CUES.values():
        # The English card stays ASCII for an ASCII-only firmware...
        assert spec["title"].isascii() and spec["body"].isascii()
        # ...and a firmware with CJK glyphs gets the Chinese one instead.
        head, text = notify.card_text(spec, "utf-8")
        assert head and not head.isascii()
        assert text == "" or not text.isascii()
        assert notify.card_text(spec, "ascii") == (spec["title"], spec["body"])
        assert len(spec["title"]) <= 12
        assert len(spec["body"]) <= 24
