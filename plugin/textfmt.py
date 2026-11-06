"""Turn chat-flavoured agent text into something a tiny screen can show.

Devices render a small bitmap font (ASCII only on the reference firmware), so
the server strips Markdown, folds typographic punctuation and accents to
ASCII, and drops emoji. Doing it here keeps the firmware simple and lets every
board benefit from improvements without a reflash.
"""

from __future__ import annotations

import re
import unicodedata

_PUNCT = {
    "\u2018": "'", "\u2019": "'", "\u201a": "'", "\u201b": "'",
    "\u201c": '"', "\u201d": '"', "\u201e": '"', "\u2033": '"', "\u2032": "'",
    "\u2013": "-", "\u2014": "-", "\u2015": "-", "\u2212": "-", "\u2010": "-", "\u2011": "-",
    "\u2026": "...", "\xa0": " ", "\u2009": " ", "\u202f": " ", "\u200b": "",
    "\u2022": "*", "\xb7": "*", "\u25cf": "*", "\u2192": "->", "\u2190": "<-", "\u21d2": "=>",
    "\xb0": " deg", "\xd7": "x", "\xf7": "/", "\u2264": "<=", "\u2265": ">=", "\u2260": "!=",
    "\xa9": "(c)", "\xae": "(r)", "\u2122": "(tm)", "\u20ac": "EUR", "\xa3": "GBP", "\xa5": "JPY",
    "\xbd": "1/2", "\xbc": "1/4", "\xbe": "3/4", "\xdf": "ss", "\xe6": "ae", "\xc6": "AE",
    "\u0153": "oe", "\u0152": "OE", "\xf8": "o", "\xd8": "O", "\u0142": "l", "\u0141": "L",
}

# The CJK font draws ° and °C reads as 摄氏度, so the ASCII " deg" fold is for ascii-only boards.
_CJK_PUNCT = {k: v for k, v in _PUNCT.items() if k != "\xb0"}

_FENCE = re.compile(r"```[^\n]*\n(.*?)```", re.DOTALL)
_INLINE_CODE = re.compile(r"`([^`]*)`")
_LINK = re.compile(r"!?\[([^\]]*)\]\(([^)]*)\)")
_BOLD = re.compile(r"(\*\*|__)(.+?)\1")
_ITALIC = re.compile(r"(?<![\w*])([*_])(?!\s)(.+?)(?<!\s)\1(?![\w*])")
_HEADING = re.compile(r"^\s{0,3}#{1,6}\s+", re.MULTILINE)
_QUOTE = re.compile(r"^\s{0,3}>\s?", re.MULTILINE)
_BULLET = re.compile(r"^(\s*)[-*+]\s+", re.MULTILINE)
_RULE = re.compile(r"^\s*([-*_])(\s*\1){2,}\s*$", re.MULTILINE)
_TABLE_SEP = re.compile(r"^\s*\|?\s*:?-{2,}:?\s*(\|\s*:?-{2,}:?\s*)*\|?\s*$", re.MULTILINE)
_THINK = re.compile(r"<think>.*?</think>", re.DOTALL | re.IGNORECASE)
# Raw tool-call markers a model emits as TEXT when the profile has no such tool
# ("<｜｜DSML｜｜ calls> ... </｜｜DSML｜｜ calls>": the tokenizer's <|tool▁calls|> arrives with
# fullwidth bars). Both glyphs are drawable, so without this they reach the screen
# and get read aloud. Strip the whole block first, then a truncated open block, then
# any leftover marker fragment.
_BAR = r"[|\uff5c]{1,2}"
_TOKEN_NAME = r"[A-Za-z0-9_\u2581 ]{1,24}"
_TOOL_BLOCK = re.compile(
    rf"<{_BAR}{_TOKEN_NAME}{_BAR}.*</{_BAR}{_TOKEN_NAME}{_BAR}[^<>\n]*>",
    re.DOTALL)
_TOOL_OPEN_TAIL = re.compile(
    rf"</?{_BAR}{_TOKEN_NAME}{_BAR}[^<>\n]*>.*\Z", re.DOTALL)
_TOOL_TOKEN = re.compile(rf"</?{_BAR}{_TOKEN_NAME}{_BAR}[^<>\n]*>")
# What a half-stripped call leaves behind from a nested marker: `name="terminal"`,
# `parameter string="true">true`. Only consulted when markers were actually present.
_TOOL_RESIDUE = re.compile(r"\b(?:name|parameter|invoke|arguments|string)\s*=", re.IGNORECASE)
_BLANKS = re.compile(r"\n{3,}")
_SPACES = re.compile(r"[ \t]{2,}")


def strip_tool_tokens(text: str) -> str:
    """Drop raw tool-call markers (and the block they wrap) from model output.

    A profile without the tool the model wants still gets the model's best attempt at
    calling it, as plain text: ``<｜｜DSML｜｜ calls>...``. Nothing downstream can draw or
    speak that, so remove it here — the same helper serves the screen path
    (:func:`for_device`) and the spoken-reply path in the adapter.

    Nested markers leave parameter fragments behind after the marker pass
    (``… parameter string="true">true</…parameter>``), so once markers were present the
    remaining text is cut at the first call-parameter fragment.
    """
    if not text or ("<|" not in text and "<\uff5c" not in text):
        return text
    text = _TOOL_BLOCK.sub("", text)
    text = _TOOL_OPEN_TAIL.sub("", text)
    text = _TOOL_TOKEN.sub("", text)
    residue = _TOOL_RESIDUE.search(text)
    if residue:
        text = text[:residue.start()]
    return text


def strip_markdown(text: str) -> str:
    text = strip_tool_tokens(text)
    text = _THINK.sub("", text)
    text = _FENCE.sub(lambda m: m.group(1).rstrip("\n"), text)
    text = _INLINE_CODE.sub(r"\1", text)
    text = _LINK.sub(lambda m: m.group(1) or m.group(2), text)
    text = _TABLE_SEP.sub("", text)
    text = _RULE.sub("", text)
    text = _HEADING.sub("", text)
    text = _QUOTE.sub("", text)
    text = _BULLET.sub(r"\1* ", text)
    text = _BOLD.sub(r"\2", text)
    text = _ITALIC.sub(r"\2", text)
    text = text.replace("|", " ")
    return text


def fold_ascii(text: str) -> str:
    out = []
    for ch in text:
        if ch == "\n" or " " <= ch <= "~":
            out.append(ch)
            continue
        if ch in _PUNCT:
            out.append(_PUNCT[ch])
            continue
        decomposed = unicodedata.normalize("NFKD", ch)
        ascii_part = "".join(c for c in decomposed if " " <= c <= "~")
        if ascii_part:
            out.append(ascii_part)
        elif ch == "\t":
            out.append(" ")
        # Anything else (emoji, symbols, CJK without a fallback font) is dropped.
    return "".join(out)


def fold_latin1(text: str) -> str:
    """For a device whose font draws Latin-1 too: keep ASCII and U+00A1..U+00FF, fold the rest."""
    out = []
    for ch in text:
        if ch == "\n" or " " <= ch <= "~" or ("\xa1" <= ch <= "\xff" and ch != "\xad"):
            out.append(ch)
            continue
        if ch == "\xa0":  # the device breaks lines only at plain spaces
            out.append(" ")
            continue
        if ch == "\xad":  # soft hyphen: invisible unless a line breaks there, and the device never does
            continue
        if ch in _PUNCT:
            out.append(_PUNCT[ch])
            continue
        decomposed = unicodedata.normalize("NFKD", ch)
        kept = "".join(c for c in decomposed if " " <= c <= "~" or "\xa1" <= c <= "\xff")
        if kept:
            out.append(kept)
        elif ch == "\t":
            out.append(" ")
        # Anything else (emoji, symbols, other scripts) is dropped.
    return "".join(out)


def _drawable(ch: str) -> bool:
    """True when the device's font can draw this character.

    The firmware carries ASCII plus the GB2312 level-1 hanzi (its CJK table is
    generated from that range by tools/make_cjk_font.py), so anything GB2312
    cannot encode - emoji, box drawing, rare hanzi - would come out as '?'.
    """
    if ch == "\n" or " " <= ch <= "~":
        return True
    try:
        ch.encode("gb2312")
    except UnicodeEncodeError:
        return False
    return True


def for_device(text: str, charset: str = "ascii") -> str:
    text = strip_markdown(text or "")
    if charset == "latin1":
        # The 5x7 font draws Latin-1 as well, so accents stay instead of folding to ASCII.
        text = fold_latin1(text)
    elif charset == "ascii":
        text = fold_ascii(text)
    elif charset.replace("-", "").lower() in ("utf8", "utf_8"):
        # Keep the CJK; drop what the font cannot draw instead of showing '?'.
        # The degree sign is drawable here and reads as 摄氏度, so it must not take the
        # ASCII fold ("22 degC" on a Chinese screen).
        text = "".join(_CJK_PUNCT.get(ch, ch) if ch in _CJK_PUNCT else ch for ch in text)
        text = "".join(ch for ch in text if _drawable(ch))
    text = "\n".join(_SPACES.sub(" ", line).rstrip() for line in text.splitlines())
    return _BLANKS.sub("\n\n", text).strip()


_TEMP_C = r"(?:℃|°\s*C(?![A-Za-z]))"
_TEMP_RANGE = re.compile(
    r"(?<![\d.])([-+]?\d+(?:\.\d+)?)\s*[~～\-–—至到]\s*([-+]?\d+(?:\.\d+)?)\s*" + _TEMP_C)
_TEMP_ONE = re.compile(r"(?<![\d.])([-+]?\d+(?:\.\d+)?)\s*" + _TEMP_C)
_TEMP_BARE = re.compile(_TEMP_C)


def speak_celsius(text: str) -> str:
    """Write temperatures the way a Chinese voice says them.

    Hermes's spoken-text normalizer expands ``22°C`` to the English ``22 degrees Celsius``,
    which a zh-CN voice pronounces "deg C". Ranges first, then single values, then any
    leftover symbol. Display text is untouched: only the spoken copy goes through here.
    """
    text = _TEMP_RANGE.sub(lambda m: f"{m.group(1)}到{m.group(2)}摄氏度", text or "")
    text = _TEMP_ONE.sub(lambda m: f"{m.group(1)}摄氏度", text)
    return _TEMP_BARE.sub("摄氏度", text)
