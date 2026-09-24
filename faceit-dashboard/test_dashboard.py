"""Тесты расчётов и сборки страницы. Запуск: python -m unittest discover -s faceit-dashboard"""

import unittest
from datetime import timezone
from unittest import mock

import analytics
import build_dashboard
import demo_data
import fetch_stats
from fetch_stats import normalize_match


def match(result=1, kills=20, deaths=10, adr=80.0, rounds=24, map_name="de_mirage",
          finished_at="2026-09-20T18:00:00Z", headshots=10):
    return {"id": "m", "finished_at": finished_at, "map": map_name, "result": result, "score": "13 / 11",
            "kills": kills, "deaths": deaths, "assists": 3, "headshots": headshots, "adr": adr, "rounds": rounds}


class NormalizeMatchTest(unittest.TestCase):
    STATS = {
        "Match Id": "1-abc", "Game Mode": "5v5", "Map": "de_inferno", "Result": "1", "Score": "13 / 7",
        "Kills": "21", "Deaths": "14", "Assists": "4", "Headshots": "11", "ADR": "92.4", "Rounds": "20",
        "Match Finished At": 1758736800000,
    }

    def test_parses_numbers_sent_as_strings(self):
        m = normalize_match(self.STATS)
        self.assertEqual((m["kills"], m["deaths"], m["assists"], m["headshots"], m["rounds"]), (21, 14, 4, 11, 20))
        self.assertAlmostEqual(m["adr"], 92.4)
        self.assertEqual(m["result"], 1)
        self.assertEqual(m["finished_at"], "2025-09-24T18:00:00Z")

    def test_timestamp_in_seconds_is_accepted(self):
        m = normalize_match({**self.STATS, "Match Finished At": "1758736800"})
        self.assertEqual(m["finished_at"], "2025-09-24T18:00:00Z")

    def test_missing_adr_becomes_none(self):
        stats = dict(self.STATS)
        del stats["ADR"]
        self.assertIsNone(normalize_match(stats)["adr"])

    def test_non_5v5_and_broken_items_are_skipped(self):
        self.assertIsNone(normalize_match({**self.STATS, "Game Mode": "2v2"}))
        self.assertIsNone(normalize_match({**self.STATS, "Kills": ""}))
        self.assertIsNone(normalize_match({}))

    def test_loss(self):
        self.assertEqual(normalize_match({**self.STATS, "Result": "0"})["result"], 0)


class FetchTest(unittest.TestCase):
    """fetch() с подменённым HTTP: пагинация, фильтр режимов, профиль."""

    def fake_api(self, total_matches):
        calls = []

        def request(path, params, api_key):
            calls.append((path, dict(params)))
            if path == "/players":
                return {"player_id": "p-1", "nickname": "iFuzY", "country": "ru",
                        "faceit_url": "https://www.faceit.com/{lang}/players/iFuzY",
                        "games": {"cs2": {"faceit_elo": 2600, "skill_level": 10, "region": "EU"}}}
            if path == "/players/p-1/stats/cs2":
                return {"lifetime": {"Matches": "2143", "Win Rate %": "54"}}
            offset, limit = params["offset"], params["limit"]
            count = max(0, min(limit, total_matches - offset))
            items = [{"stats": {**NormalizeMatchTest.STATS, "Match Id": f"m-{offset + i}"}} for i in range(count)]
            if items:
                items[0]["stats"]["Game Mode"] = "2v2"  # должен отфильтроваться
            return {"items": items}

        return request, calls

    def test_pages_until_short_page(self):
        request, calls = self.fake_api(total_matches=130)
        with mock.patch.object(fetch_stats, "request", request):
            data = fetch_stats.fetch("iFuzY", "key", limit=200)
        stat_calls = [c for c in calls if c[0].endswith("/games/cs2/stats")]
        self.assertEqual([c[1]["offset"] for c in stat_calls], [0, 100])
        self.assertEqual(len(data["matches"]), 128)  # по одному 2v2 на страницу отброшено
        self.assertEqual(data["player"]["elo"], 2600)
        self.assertEqual(data["player"]["country"], "RU")
        self.assertEqual(data["player"]["faceit_url"], "https://www.faceit.com/ru/players/iFuzY")
        self.assertEqual(data["player"]["lifetime_matches"], 2143)

    def test_respects_limit(self):
        request, calls = self.fake_api(total_matches=1000)
        with mock.patch.object(fetch_stats, "request", request):
            fetch_stats.fetch("iFuzY", "key", limit=150)
        stat_calls = [c[1] for c in calls if c[0].endswith("/games/cs2/stats")]
        self.assertEqual(stat_calls, [{"offset": 0, "limit": 100}, {"offset": 100, "limit": 50}])


class AnalyticsTest(unittest.TestCase):
    def test_summary_uses_totals_not_average_of_ratios(self):
        s = analytics.summarize([match(kills=30, deaths=10), match(result=0, kills=10, deaths=20)])
        self.assertAlmostEqual(s["kd"], 40 / 30)  # среднее отношений дало бы 1.75
        self.assertEqual((s["wins"], s["losses"]), (1, 1))
        self.assertAlmostEqual(s["win_rate"], 50)
        self.assertAlmostEqual(s["hs_pct"], 20 / 40 * 100)

    def test_adr_is_weighted_by_rounds(self):
        s = analytics.summarize([match(adr=100, rounds=30), match(adr=50, rounds=10)])
        self.assertAlmostEqual(s["adr"], (100 * 30 + 50 * 10) / 40)

    def test_adr_skips_matches_without_it(self):
        self.assertAlmostEqual(analytics.summarize([match(adr=None), match(adr=70)])["adr"], 70)
        self.assertIsNone(analytics.summarize([match(adr=None)])["adr"])

    def test_empty_summary(self):
        s = analytics.summarize([])
        self.assertEqual(s["matches"], 0)
        self.assertIsNone(s["win_rate"])

    def test_map_labels(self):
        self.assertEqual(analytics.map_label("de_mirage"), "Mirage")
        self.assertEqual(analytics.map_label("de_dust2"), "Dust2")

    def test_maps_sorted_by_matches_and_flagged_by_sample(self):
        rows = analytics.by_map([match(map_name="de_nuke")] + [match(map_name="de_mirage")] * 5)
        self.assertEqual([r["name"] for r in rows], ["Mirage", "Nuke"])
        self.assertEqual([r["reliable"] for r in rows], [True, False])

    def test_time_of_day_uses_local_time(self):
        tz = analytics.get_timezone("Europe/Moscow")
        # 22:30 UTC — это 01:30 по Москве, ночь
        rows = analytics.by_time_of_day([match(finished_at="2026-09-20T22:30:00Z")], tz)
        self.assertEqual([r["matches"] for r in rows], [1, 0, 0, 0])

    def test_rolling_mean(self):
        self.assertEqual(analytics.rolling_mean([1, 2, 3, 4], window=2), [1, 1.5, 2.5, 3.5])
        self.assertEqual(analytics.rolling_mean([None, 2, None], window=2), [None, 2, 2])

    def test_streaks(self):
        s = analytics.streaks([match(1), match(1), match(1), match(0), match(1), match(1)])
        self.assertEqual((s["current_win"], s["current_len"], s["longest_win"]), (True, 2, 3))

    def test_form_needs_two_windows(self):
        self.assertIsNone(analytics.form([match()] * 39))
        self.assertIsNotNone(analytics.form([match()] * 40))

    def test_insights_ignore_small_samples(self):
        matches = [match(map_name="de_mirage")] * 5 + [match(result=0, map_name="de_nuke")] * 5 + [match(map_name="de_train")]
        text = " ".join(analytics.insights(matches, analytics.by_map(matches), []))
        self.assertIn("Лучшая карта — Mirage", text)
        self.assertIn("Nuke", text)
        self.assertNotIn("Train", text)


class RenderTest(unittest.TestCase):
    def test_demo_page_builds(self):
        report = analytics.build_report(demo_data.generate(), "Europe/Moscow")
        html = build_dashboard.render(report, "https://example.com/repo")
        self.assertIn("Демо-данные", html)
        self.assertIn("Винрейт по картам", html)
        self.assertEqual(html.count('class="plot"'), 2)

    def test_player_data_is_escaped(self):
        data = demo_data.generate(count=12)
        data["player"]["nickname"] = '<script>alert(1)</script>'
        data["matches"][0]["map"] = 'de_<img src=x onerror=alert(1)>'
        html = build_dashboard.render(analytics.build_report(data), "https://example.com/repo")
        self.assertNotIn("<script>alert", html)
        self.assertNotIn("<img src=x", html)

    def test_flat_series_still_has_a_scale(self):
        ticks = build_dashboard.nice_ticks(1.0, 1.0)
        self.assertGreaterEqual(len(ticks), 2)
        self.assertLess(ticks[0], ticks[-1])
        html = build_dashboard.line_chart([1.0, 1.0, 1.0], ["a", "b", "c"], "t", "kd", 1.0)
        self.assertIn("polyline", html)

    def test_placeholder(self):
        self.assertIn("FACEIT_API_KEY", build_dashboard.render_placeholder("https://example.com/repo"))

    def test_fixed_offset_timezone_label(self):
        self.assertEqual(analytics.get_timezone("Nowhere/Invalid"), timezone.utc)


if __name__ == "__main__":
    unittest.main()
