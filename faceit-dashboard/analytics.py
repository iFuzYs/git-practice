"""Расчёт статистики для FACEIT-дашборда.

Чистые функции без сети и ввода-вывода: на вход — список матчей в нормализованном
формате (см. fetch_stats.normalize_match), на выход — словари для отрисовки.
"""

from __future__ import annotations

from datetime import datetime, timedelta, timezone, tzinfo

# Минимум матчей, чтобы выводу по карте или времени суток можно было доверять.
MIN_SAMPLE = 5
# Окно скользящего среднего для графиков формы.
ROLLING_WINDOW = 10
# Сколько последних матчей сравнивать с предыдущими в блоке «Форма».
FORM_WINDOW = 20

TIME_BUCKETS = (
    ("Ночь", 0, 6),
    ("Утро", 6, 12),
    ("День", 12, 18),
    ("Вечер", 18, 24),
)


def get_timezone(name: str) -> tzinfo:
    """Часовой пояс по имени; если базы tzdata нет, для Москвы берём UTC+3."""
    try:
        from zoneinfo import ZoneInfo

        return ZoneInfo(name)
    except Exception:  # noqa: BLE001 — нет tzdata или неизвестное имя
        if name == "Europe/Moscow":
            return timezone(timedelta(hours=3), "MSK")
        return timezone.utc


def parse_time(value: str) -> datetime:
    return datetime.fromisoformat(value.replace("Z", "+00:00"))


def map_label(raw: str) -> str:
    """de_mirage → Mirage, de_ancient → Ancient."""
    name = raw.strip()
    for prefix in ("de_", "cs_", "ar_"):
        if name.startswith(prefix):
            name = name[len(prefix):]
            break
    return name.replace("_", " ").title() or "Неизвестная карта"


def _ratio(numerator: float, denominator: float) -> float:
    return numerator / denominator if denominator else float(numerator)


def _weighted_adr(matches: list[dict]) -> float | None:
    """ADR серии матчей: среднее по матчам, взвешенное по числу раундов."""
    with_adr = [m for m in matches if m.get("adr") is not None]
    if not with_adr:
        return None
    rounds = [m.get("rounds") or 0 for m in with_adr]
    if all(rounds):
        return sum(m["adr"] * r for m, r in zip(with_adr, rounds)) / sum(rounds)
    return sum(m["adr"] for m in with_adr) / len(with_adr)


def summarize(matches: list[dict]) -> dict:
    """Сводка по набору матчей: винрейт, K/D, ADR, процент хедшотов."""
    total = len(matches)
    wins = sum(m["result"] for m in matches)
    kills = sum(m["kills"] for m in matches)
    deaths = sum(m["deaths"] for m in matches)
    headshots = sum(m.get("headshots") or 0 for m in matches)
    return {
        "matches": total,
        "wins": wins,
        "losses": total - wins,
        "win_rate": 100 * wins / total if total else None,
        "kd": _ratio(kills, deaths) if total else None,
        "adr": _weighted_adr(matches),
        "hs_pct": 100 * headshots / kills if kills else None,
        "avg_kills": kills / total if total else None,
    }


def by_map(matches: list[dict]) -> list[dict]:
    """Сводка по каждой карте, от самой частой к самой редкой."""
    groups: dict[str, list[dict]] = {}
    for m in matches:
        groups.setdefault(map_label(m["map"]), []).append(m)
    rows = [{"name": name, **summarize(group)} for name, group in groups.items()]
    rows.sort(key=lambda r: (-r["matches"], r["name"]))
    for r in rows:
        r["reliable"] = r["matches"] >= MIN_SAMPLE
    return rows


def by_time_of_day(matches: list[dict], tz: tzinfo) -> list[dict]:
    """Сводка по времени суток, когда закончился матч: время начала API в этой выборке не отдаёт."""
    rows = []
    for name, start, end in TIME_BUCKETS:
        group = [m for m in matches if start <= parse_time(m["finished_at"]).astimezone(tz).hour < end]
        row = {"name": f"{name}, {start:02d}–{end:02d}", **summarize(group)}
        row["reliable"] = row["matches"] >= MIN_SAMPLE
        rows.append(row)
    return rows


def rolling_mean(values: list[float | None], window: int = ROLLING_WINDOW) -> list[float | None]:
    """Скользящее среднее; пока матчей меньше окна — среднее по тем, что есть."""
    result: list[float | None] = []
    for i in range(len(values)):
        chunk = [v for v in values[max(0, i - window + 1): i + 1] if v is not None]
        result.append(sum(chunk) / len(chunk) if chunk else None)
    return result


def streaks(matches: list[dict]) -> dict:
    """Текущая серия (побед или поражений) и самая длинная серия побед."""
    current_len = 0
    current_win = None
    longest_win = run = 0
    for m in matches:
        win = bool(m["result"])
        run = run + 1 if win else 0
        longest_win = max(longest_win, run)
        if win == current_win:
            current_len += 1
        else:
            current_win, current_len = win, 1
    return {"current_win": current_win, "current_len": current_len, "longest_win": longest_win}


def form(matches: list[dict], window: int = FORM_WINDOW) -> dict | None:
    """Последние `window` матчей против `window` предыдущих."""
    if len(matches) < 2 * window:
        return None
    return {
        "window": window,
        "recent": summarize(matches[-window:]),
        "previous": summarize(matches[-2 * window: -window]),
    }


def insights(matches: list[dict], maps: list[dict], times: list[dict]) -> list[str]:
    """Короткие выводы простыми правилами. Учитываются только выборки от MIN_SAMPLE матчей."""
    result = []
    reliable_maps = [r for r in maps if r["reliable"]]
    if len(reliable_maps) >= 2:
        best = max(reliable_maps, key=lambda r: (r["win_rate"], r["matches"]))
        worst = min(reliable_maps, key=lambda r: (r["win_rate"], -r["matches"]))
        result.append(
            f"Лучшая карта — {best['name']}: {best['win_rate']:.0f}% побед за {best['matches']} матчей. "
            f"Слабее всего идёт {worst['name']}: {worst['win_rate']:.0f}% за {worst['matches']}."
        )
    reliable_times = [r for r in times if r["reliable"]]
    if len(reliable_times) >= 2:
        best_time = max(reliable_times, key=lambda r: (r["win_rate"], r["matches"]))
        result.append(
            f"Лучшее время для игры — {best_time['name'].split(',')[0].lower()}: "
            f"{best_time['win_rate']:.0f}% побед и K/D {best_time['kd']:.2f} за {best_time['matches']} матчей."
        )
    f = form(matches)
    if f:
        recent, previous = f["recent"], f["previous"]
        direction = "выше" if recent["kd"] > previous["kd"] else "ниже" if recent["kd"] < previous["kd"] else "на уровне"
        result.append(
            f"Форма: K/D за последние {f['window']} матчей — {recent['kd']:.2f}, "
            f"{direction} предыдущих {f['window']} ({previous['kd']:.2f}). "
            f"Винрейт {recent['win_rate']:.0f}% против {previous['win_rate']:.0f}%."
        )
    s = streaks(matches)
    if s["current_len"] >= 3:
        kind = "побед" if s["current_win"] else "поражений"
        result.append(f"Сейчас серия из {s['current_len']} {kind} подряд.")
    return result


def build_report(data: dict, tz_name: str = "Europe/Moscow") -> dict:
    """Всё, что нужно для отрисовки страницы, из нормализованных данных."""
    tz = get_timezone(tz_name)
    matches = sorted(data["matches"], key=lambda m: m["finished_at"])
    maps = by_map(matches)
    times = by_time_of_day(matches, tz)
    return {
        "player": data["player"],
        "generated_at": data["generated_at"],
        "demo": data.get("demo", False),
        "tz": tz,
        "matches": matches,
        "summary": summarize(matches),
        "maps": maps,
        "times": times,
        "kd_trend": rolling_mean([_ratio(m["kills"], m["deaths"]) for m in matches]),
        "adr_trend": rolling_mean([m.get("adr") for m in matches]),
        "streaks": streaks(matches),
        "insights": insights(matches, maps, times),
    }
