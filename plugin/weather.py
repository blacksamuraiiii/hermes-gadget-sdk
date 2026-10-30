"""Weather on the gadget, from QWeather (和风天气).

The board is a 26×5 screen with a speaker, so the phrasing matters more than the data model:
one line answers a spoken question, two short lines fill the card, and an active warning leads
both. QWeather returns the condition text already localized (``lang=zh``), which is the reason
this provider beats a code table.

Nothing here holds a secret. Everything comes from the environment of the process that runs the
gateway (the profile's ``.env`` is loaded by Hermes before plugins start):

    QWEATHER_HOST=abcxyz.qweatherapi.com     # the account's API host (console → 设置)
    QWEATHER_KEY=...                          # the project's API KEY (X-QW-Api-Key)
    QWEATHER_PLACE=扬州                       # resolved through GeoAPI on first use
    QWEATHER_LAT=32.39  QWEATHER_LON=119.42   # optional: skip GeoAPI and pin the point

Docs: current ``/weather/v1/current/{lat}/{lon}``, daily ``/weather/v1/daily/{lat}/{lon}``,
alerts ``/weatheralert/v1/current/{lat}/{lon}``, GeoAPI ``/geo/v2/city/lookup``.
"""

from __future__ import annotations

import os
import threading
import time
from typing import Any, Optional

DEFAULT_PLACE = "扬州"
TIMEOUT_S = 8.0
CACHE_S = 300.0  # a forecast does not change inside five minutes; the screen asks often

_lock = threading.Lock()
_place_cache: dict[str, Any] = {}
_answer_cache: dict[str, Any] = {}


class WeatherError(Exception):
    """A configured problem (no key) or an upstream one (HTTP, code != 200)."""


def _env(name: str, default: str = "") -> str:
    """Environment first, then Hermes's own .env lookup (same source providers use)."""
    value = os.environ.get(name)
    if value:
        return value.strip()
    try:
        from agent.web_search_provider import get_provider_env

        return str(get_provider_env(name) or "").strip()
    except Exception:  # not inside Hermes (a test, or the CLI run bare)
        return default


def config() -> dict[str, Any]:
    """Everything a call needs, or a WeatherError naming what is missing."""
    host = _env("QWEATHER_HOST")
    key = _env("QWEATHER_KEY") or _env("QWEATHER_API_KEY")
    if not host:
        raise WeatherError("QWEATHER_HOST is not set (console.qweather.com → 设置 → API Host)")
    if not key:
        raise WeatherError("QWEATHER_KEY is not set (console.qweather.com → 项目管理 → API KEY)")
    return {
        "host": host.replace("https://", "").replace("http://", "").rstrip("/"),
        "key": key,
        "place": _env("QWEATHER_PLACE") or DEFAULT_PLACE,
        "lat": _env("QWEATHER_LAT"),
        "lon": _env("QWEATHER_LON"),
    }


def _get_json(path: str, params: Optional[dict[str, Any]] = None) -> dict[str, Any]:
    """One GET against the account's host. Raises WeatherError with the reason it failed."""
    cfg = config()
    url = f"https://{cfg['host']}{path}"
    try:
        import requests

        response = requests.get(url, params=params or {}, timeout=TIMEOUT_S,
                                headers={"X-QW-Api-Key": cfg["key"], "Accept-Encoding": "gzip"})
    except Exception as exc:  # network, DNS, TLS: the caller phrases it for the screen
        raise WeatherError(f"和风天气连不上：{exc}") from exc
    if response.status_code >= 400:
        raise WeatherError(f"和风天气返回 HTTP {response.status_code}（检查 host 与 key）")
    try:
        data = response.json()
    except ValueError as exc:
        raise WeatherError("和风天气返回的不是 JSON") from exc
    code = str(data.get("code") or "")
    if code and code != "200":
        raise WeatherError(f"和风天气返回 code={code}（{_CODE_HINTS.get(code, '见错误码文档')}）")
    return data


_CODE_HINTS = {
    "401": "key 无效或未启用",
    "402": "超出免费额度",
    "403": "无权限访问该接口",
    "404": "该位置没有数据",
    "429": "请求过于频繁",
}


def resolve_place(cfg: Optional[dict[str, Any]] = None) -> dict[str, Any]:
    """``{'name', 'lat', 'lon'}`` for the configured place, GeoAPI once per process."""
    cfg = cfg or config()
    if cfg["lat"] and cfg["lon"]:
        return {"name": cfg["place"], "lat": cfg["lat"], "lon": cfg["lon"]}
    with _lock:
        cached = _place_cache.get(cfg["place"])
    if cached:
        return cached
    data = _get_json("/geo/v2/city/lookup",
                     {"location": cfg["place"], "range": "cn", "number": 1, "lang": "zh"})
    found = (data.get("location") or [{}])[0]
    if not found.get("lat") or not found.get("lon"):
        raise WeatherError(f"找不到地点「{cfg['place']}」")
    place = {"name": found.get("name") or cfg["place"],
             "lat": f"{float(found['lat']):.2f}", "lon": f"{float(found['lon']):.2f}"}
    with _lock:
        _place_cache[cfg["place"]] = place
    return place


def now(place: dict[str, Any]) -> dict[str, Any]:
    return _get_json(f"/weather/v1/current/{place['lat']}/{place['lon']}",
                     {"localTime": "true", "lang": "zh"})


def forecast(place: dict[str, Any], days: int = 3) -> list[dict[str, Any]]:
    days = max(1, min(10, int(days or 3)))
    data = _get_json(f"/weather/v1/daily/{place['lat']}/{place['lon']}",
                     {"days": days, "localTime": "true", "lang": "zh"})
    return data.get("days") or []


def alerts(place: dict[str, Any]) -> list[dict[str, Any]]:
    """Active official warnings. An empty list is the normal answer, not an error."""
    try:
        data = _get_json(f"/weatheralert/v1/current/{place['lat']}/{place['lon']}",
                         {"localTime": "true", "lang": "zh"})
    except WeatherError:
        return []  # a missing warning channel must not cost the forecast
    return data.get("alerts") or []


def _round(value: Any) -> Optional[int]:
    try:
        return int(round(float(value)))
    except (TypeError, ValueError):
        return None


def _degree(value: Any) -> str:
    number = _round(value)
    return f"{number}℃" if number is not None else ""


_DAY_NAMES = ("今天", "明天", "后天")


def describe_now(place: dict[str, Any], data: dict[str, Any]) -> str:
    """One spoken line: ``扬州 晴 21℃，体感 22℃，西南风2级，湿度57%``."""
    parts = [place.get("name") or "", str((data.get("condition") or {}).get("text") or "").strip()]
    temp = _degree((data.get("temperature") or {}).get("value"))
    if temp:
        parts.append(temp)
    feels = _degree((data.get("feelsLike") or {}).get("value"))
    if feels and feels != temp:
        parts.append(f"体感{feels}")
    wind = data.get("wind") or {}
    scale = _round(wind.get("scale"))
    compass = _compass(wind)
    if scale is not None and compass:
        parts.append(f"{compass}{scale}级")
    humidity = data.get("humidity")
    if isinstance(humidity, (int, float)):
        parts.append(f"湿度{int(round(float(humidity) * 100))}%")
    return " ".join(p for p in parts if p)


_COMPASS_ZH = {"n": "北", "nne": "北东北", "ne": "东北", "ene": "东东北", "e": "东", "ese": "东东南",
               "se": "东南", "sse": "南东南", "s": "南", "ssw": "南西南", "sw": "西南", "wsw": "西西南",
               "w": "西", "wnw": "西西北", "nw": "西北", "nnw": "北西北"}


def _compass(wind: dict[str, Any]) -> str:
    code = str(((wind.get("direction") or {}).get("compass")) or "").lower()
    return _COMPASS_ZH.get(code, "")


def describe_day(index: int, day: dict[str, Any]) -> str:
    """``今天 少云 22/11℃`` — the screen has room for three of these.

    The v1 daily payload splits a day into ``daytime`` and ``nighttime``; the daytime text is
    what a forecast line means, and the night text is the fallback when it is missing.
    """
    name = _DAY_NAMES[index] if index < len(_DAY_NAMES) else str(day.get("forecastStartTime") or "")[:10]
    high = _degree((day.get("temperatureMax") or {}).get("value"))
    low = _degree((day.get("temperatureMin") or {}).get("value"))
    span = f"{high.split('℃')[0]}/{low}" if high and low else (high or low)
    text = str(((day.get("daytime") or {}).get("condition") or {}).get("text")
               or ((day.get("nighttime") or {}).get("condition") or {}).get("text") or "").strip()
    return " ".join(p for p in (name, text, span) if p)


def describe_alerts(items: list[dict[str, Any]]) -> str:
    """``⚠ 大风蓝色预警`` for the first one, ``等 2 条预警`` when there are more."""
    if not items:
        return ""
    first = str(items[0].get("headline") or items[0].get("title") or "").strip()
    more = f" 等 {len(items)} 条预警" if len(items) > 1 else ""
    return f"⚠ {first}{more}" if first else f"⚠ {len(items)} 条预警"


def answer(days: int = 3) -> str:
    """The line a voice question gets, cached for a few minutes."""
    key = f"{_env('QWEATHER_PLACE') or DEFAULT_PLACE}|{days}"
    with _lock:
        cached = _answer_cache.get(key)
    if cached and time.time() - cached["at"] < CACHE_S:
        return cached["text"]
    place = resolve_place()
    lines = [describe_alerts(alerts(place)), describe_now(place, now(place))]
    lines += [describe_day(i, d) for i, d in enumerate(forecast(place, days))]
    text = "；".join(p for p in lines if p)
    with _lock:
        _answer_cache[key] = {"at": time.time(), "text": text}
    return text


def card(days: int = 3) -> tuple[str, str]:
    """``(title, body)`` sized for 26 columns × 5 rows: a warning line, now, then two days."""
    place = resolve_place()
    active = describe_alerts(alerts(place))
    lines = [active] if active else []
    lines.append(describe_now(place, now(place)))
    lines += [describe_day(i, d) for i, d in enumerate(forecast(place, days)[:2])]
    title = f"{place.get('name') or ''}天气".strip()
    return title, "\n".join(p for p in lines if p)
