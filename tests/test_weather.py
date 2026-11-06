"""Weather phrasing for the gadget: what a voice question and a 26×5 card actually say.

The transport is faked here: these tests pin the wording, the units, the warning line and the
cache, which is what the device shows and speaks. Live calls are checked by hand against the
account's own API host (see ``notes``), not in CI.
"""

from __future__ import annotations

import pytest

from hermes_gadget_plugin import weather

CURRENT = {
    "condition": {"text": "晴", "code": "100"},
    "temperature": {"value": 21.3, "unit": "°C"},
    "feelsLike": {"value": 22.4, "unit": "°C"},
    "humidity": 0.57,
    "wind": {"direction": {"degree": 226, "compass": "sw"}, "speed": {"value": 4.7}, "scale": 2},
}

DAILY = {"days": [
    {"forecastStartTime": "2026-10-30T00:00+08:00",
     "daytime": {"condition": {"text": "晴"}}, "nighttime": {"condition": {"text": "多云"}},
     "temperatureMax": {"value": 22.0}, "temperatureMin": {"value": 10.5}},
    {"forecastStartTime": "2026-10-31T00:00+08:00",
     "daytime": {"condition": {"text": "多云"}}, "nighttime": {"condition": {"text": "小雨"}},
     "temperatureMax": {"value": 23.1}, "temperatureMin": {"value": 10.5}},
    {"forecastStartTime": "2026-11-01T00:00+08:00",
     "daytime": {"condition": {"text": "小雨"}}, "nighttime": {"condition": {"text": "阴"}},
     "temperatureMax": {"value": 18.7}, "temperatureMin": {"value": 15.1}},
]}


@pytest.fixture()
def wired(monkeypatch):
    """A configured client whose transport answers the three endpoints from fixtures."""
    calls: list[str] = []
    alerts: list[dict] = []

    monkeypatch.setenv("QWEATHER_HOST", "test.qweatherapi.com")
    monkeypatch.setenv("QWEATHER_KEY", "test-key")
    monkeypatch.setenv("QWEATHER_PLACE", "扬州")
    monkeypatch.setenv("QWEATHER_LAT", "32.39")
    monkeypatch.setenv("QWEATHER_LON", "119.42")

    def fake_get_json(path, params=None):
        calls.append(path)
        if path.startswith("/weatheralert/"):
            return {"alerts": alerts}
        if path.startswith("/weather/v1/current/"):
            return CURRENT
        if path.startswith("/weather/v1/daily/"):
            return DAILY
        raise AssertionError(f"unexpected path {path}")

    monkeypatch.setattr(weather, "_get_json", fake_get_json)
    weather._answer_cache.clear()
    weather._place_cache.clear()
    return {"calls": calls, "alerts": alerts}


def test_a_voice_answer_is_one_sentence_with_units(wired):
    said = weather.answer()
    assert "扬州" in said and "晴" in said
    assert "21℃" in said and "体感22℃" in said
    assert "西南2级" in said and "湿度57%" in said
    # today/tomorrow/the day after, with high/low
    assert "今天 晴 22/10℃" in said
    assert "明天 多云 23/10℃" in said
    assert "后天 小雨 19/15℃" in said  # 18.7 rounds to 19


def test_the_card_fits_the_screen_and_names_the_place(wired):
    title, body = weather.card()
    assert title == "扬州天气"
    lines = body.splitlines()
    assert len(lines) == 3  # now + two days, no warning today
    assert lines[0].startswith("扬州 晴")
    assert max(len(line) for line in lines) <= 52  # two 26-column rows' worth per line


def test_an_active_warning_leads_both_the_card_and_the_answer(wired):
    wired["alerts"].append({"headline": "大风蓝色预警", "severity": "Moderate"})
    title, body = weather.card()
    assert body.splitlines()[0] == "⚠ 大风蓝色预警"
    assert weather.answer().startswith("⚠ 大风蓝色预警")


def test_several_warnings_are_counted(wired):
    wired["alerts"].extend([{"headline": "大风蓝色预警"}, {"headline": "寒潮黄色预警"}])
    assert "等 2 条预警" in weather.card()[1].splitlines()[0]


def test_a_broken_warning_channel_still_leaves_a_forecast(monkeypatch, wired):
    def only_alerts_fail(path, params=None):
        if path.startswith("/weatheralert/"):
            raise weather.WeatherError("和风天气返回 code=403")
        return CURRENT if "/current/" in path else DAILY

    monkeypatch.setattr(weather, "_get_json", only_alerts_fail)
    said = weather.answer()
    assert "晴" in said and "⚠" not in said


def test_without_a_key_the_error_names_the_variable(monkeypatch, wired):
    monkeypatch.delenv("QWEATHER_KEY", raising=False)
    monkeypatch.setattr(weather, "_env",
                        lambda name, default="": "" if name in ("QWEATHER_KEY", "QWEATHER_API_KEY") else "x")
    with pytest.raises(weather.WeatherError) as err:
        weather.config()
    assert "QWEATHER_KEY" in str(err.value)


def test_the_answer_is_cached_for_a_few_minutes(wired):
    weather.answer()
    before = len(wired["calls"])
    weather.answer()
    assert len(wired["calls"]) == before  # second call served from the cache


def test_a_named_place_goes_through_geoapi_even_when_coordinates_are_pinned(monkeypatch, wired):
    """「南京」 must not be answered with 扬州's pinned point."""
    seen: list[tuple[str, dict]] = []

    def transport(path, params=None):
        seen.append((path, params or {}))
        if path == "/geo/v2/city/lookup":
            assert params["location"] == "南京"
            return {"code": "200", "location": [{"name": "南京", "lat": "32.06", "lon": "118.80"}]}
        if path.startswith("/weatheralert/"):
            return {"alerts": []}
        if "/current/" in path:
            assert "/32.06/118.80" in path  # the named city, not 32.39/119.42
            return CURRENT
        return DAILY

    monkeypatch.setattr(weather, "_get_json", transport)
    said = weather.answer(place="南京")
    assert said.startswith("南京")
    assert any(path == "/geo/v2/city/lookup" for path, _ in seen)


def test_a_missing_place_is_named_in_the_error(monkeypatch, wired):
    monkeypatch.setenv("QWEATHER_LAT", "")
    monkeypatch.setenv("QWEATHER_LON", "")

    def no_such_city(path, params=None):
        assert path == "/geo/v2/city/lookup"
        return {"code": "200", "location": []}

    monkeypatch.setattr(weather, "_get_json", no_such_city)
    with pytest.raises(weather.WeatherError) as err:
        weather.resolve_place()
    assert "扬州" in str(err.value)
