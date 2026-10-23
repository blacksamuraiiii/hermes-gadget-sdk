"""Cut the reminder cues once, with edge-tts, into the plugin's data directory.

The gadget plays a cue (a short, fixed phrase) instead of calling text-to-speech
at reminder time: a cue is offline, instant, identical every time, and costs no
tokens. ``plugin/notify.py`` only knows the file names below; the words live
here.

    hpy tools/make_cues.py                # write what is missing
    hpy tools/make_cues.py --force        # re-cut all three
    hpy tools/make_cues.py --voice zh-CN-YunxiNeural --rate -5%

Output: ``<hermes home>/plugin-data/gadget/cues/<cue>.wav`` — mono PCM16 at
24 kHz, which is the rate the ES8311 boards declare, so the adapter can decode
it without resampling.
"""

from __future__ import annotations

import argparse
import asyncio
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

CUES = {
    "approval": "需要你授权，请回到电脑上确认",
    "clarify": "需要你做一个选择",
    "done": "任务已完成",
}

DEFAULT_VOICE = "zh-CN-XiaoxiaoNeural"
DEFAULT_RATE = "+0%"
OUT_RATE = 24000


def cue_dir() -> Path:
    """The same directory the adapter reads: <hermes home>/plugin-data/gadget/cues."""
    try:
        from plugins.plugin_storage import plugin_data_dir

        return Path(plugin_data_dir("gadget")) / "cues"
    except Exception:
        home = os.environ.get("HERMES_HOME") or str(Path.home() / ".hermes")
        return Path(home) / "plugin-data" / "gadget" / "cues"


def find_ffmpeg() -> str:
    found = shutil.which("ffmpeg")
    if found:
        return found
    for base in (Path.home() / ".hermes" / "tools",):
        if base.is_dir():
            for entry in sorted(base.glob("ffmpeg-*/bin/ffmpeg")):
                return str(entry)
    raise SystemExit("ffmpeg not found: install it, or add Hermes's bundled ffmpeg to PATH")


async def speak(text: str, voice: str, rate: str, mp3: Path) -> None:
    import edge_tts

    await edge_tts.Communicate(text, voice, rate=rate).save(str(mp3))


def to_wav(ffmpeg: str, mp3: Path, wav: Path, rate: int) -> None:
    done = subprocess.run(
        [ffmpeg, "-hide_banner", "-loglevel", "error", "-y", "-i", str(mp3),
         "-ac", "1", "-ar", str(rate), "-c:a", "pcm_s16le", str(wav)],
        capture_output=True, text=True)
    if done.returncode != 0:
        raise SystemExit(f"ffmpeg failed: {done.stderr.strip()}")


def wav_seconds(path: Path) -> float:
    with wave.open(str(path), "rb") as w:
        return w.getnframes() / float(w.getframerate() or 1)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Cut the gadget's reminder cues with edge-tts.")
    ap.add_argument("--voice", default=DEFAULT_VOICE, help=f"edge-tts voice (default {DEFAULT_VOICE})")
    ap.add_argument("--rate", default=DEFAULT_RATE, help="edge-tts speaking rate, e.g. -5%% (default +0%%)")
    ap.add_argument("--out", default=None, help="output directory (default: the plugin's cue directory)")
    ap.add_argument("--force", action="store_true", help="re-cut cues that already exist")
    ap.add_argument("--list", action="store_true", help="show the cues and exit")
    args = ap.parse_args(argv)

    out = Path(args.out) if args.out else cue_dir()
    if args.list:
        for name, text in CUES.items():
            print(f"{name:<10} {text}")
        print(f"→ {out}")
        return 0
    out.mkdir(parents=True, exist_ok=True)
    ffmpeg = find_ffmpeg()
    made = skipped = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name, text in CUES.items():
            target = out / f"{name}.wav"
            if target.is_file() and not args.force:
                print(f"skip {name:<10} {target} ({wav_seconds(target):.2f}s)")
                skipped += 1
                continue
            mp3 = Path(tmp) / f"{name}.mp3"
            asyncio.run(speak(text, args.voice, args.rate, mp3))
            to_wav(ffmpeg, mp3, target, OUT_RATE)
            print(f"made {name:<10} {target} ({wav_seconds(target):.2f}s, {target.stat().st_size} B)")
            made += 1
    print(f"done: {made} made, {skipped} skipped, voice {args.voice}, {OUT_RATE} Hz mono")
    return 0


if __name__ == "__main__":
    sys.exit(main())
