"""Загрузка статистики игрока из FACEIT Data API v4.

Запуск:
    FACEIT_API_KEY=... FACEIT_NICKNAME=... python fetch_stats.py --out data.json

Ключ берётся только из переменной окружения, передаётся в заголовке и никуда не выводится.
Используемые эндпоинты:
    GET /players?nickname=           — профиль, Elo и уровень в CS2
    GET /players/{id}/stats/cs2      — статистика за всё время
    GET /players/{id}/games/cs2/stats — статистика по каждому матчу, до 100 за запрос
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone

API = "https://open.faceit.com/data/v4"
GAME = "cs2"
PAGE_SIZE = 100
# API не принимает offset больше 200, поэтому через offset доступны последние 300 матчей.
MAX_MATCHES = 300
RETRY_CODES = {429, 500, 502, 503, 504}


class FaceitError(RuntimeError):
    pass


def request(path: str, params: dict, api_key: str, retries: int = 4) -> dict:
    query = f"?{urllib.parse.urlencode(params)}" if params else ""
    req = urllib.request.Request(
        f"{API}{path}{query}",
        headers={
            "Authorization": f"Bearer {api_key}",
            "Accept": "application/json",
            "User-Agent": "git-practice-faceit-dashboard",
        },
    )
    for attempt in range(retries + 1):
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                return json.load(resp)
        except urllib.error.HTTPError as err:
            if err.code in RETRY_CODES and attempt < retries:
                time.sleep(2 ** attempt)
                continue
            if err.code in (401, 403):
                raise FaceitError(f"FACEIT отклонил ключ ({err.code}): проверьте секрет FACEIT_API_KEY") from None
            if err.code == 404:
                raise FaceitError(f"FACEIT вернул 404 для {path}: проверьте никнейм в FACEIT_NICKNAME") from None
            raise FaceitError(f"FACEIT API вернул {err.code} для {path}") from None
        except urllib.error.URLError as err:
            if attempt < retries:
                time.sleep(2 ** attempt)
                continue
            raise FaceitError(f"Нет связи с FACEIT API: {err.reason}") from None
    raise AssertionError("unreachable")


def _num(value, cast=float):
    """FACEIT отдаёт числа строками: "1.25", "13". Пустое или нечисловое → None."""
    try:
        return cast(float(value))
    except (TypeError, ValueError):
        return None


def normalize_match(stats: dict) -> dict | None:
    """Матч из ответа /games/cs2/stats в формат дашборда. Не 5v5 или без данных → None."""
    mode = stats.get("Game Mode")
    if mode and mode != "5v5":
        return None
    finished = _num(stats.get("Match Finished At"), int)
    kills = _num(stats.get("Kills"), int)
    deaths = _num(stats.get("Deaths"), int)
    if finished is None or kills is None or deaths is None:
        return None
    if finished < 10 ** 11:  # на случай секунд вместо миллисекунд
        finished *= 1000
    return {
        "id": stats.get("Match Id") or "",
        "finished_at": datetime.fromtimestamp(finished / 1000, tz=timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "map": stats.get("Map") or "unknown",
        "result": 1 if str(stats.get("Result")).strip() == "1" else 0,
        "score": stats.get("Score") or "",
        "kills": kills,
        "deaths": deaths,
        "assists": _num(stats.get("Assists"), int) or 0,
        "headshots": _num(stats.get("Headshots"), int),
        "adr": _num(stats.get("ADR")),
        "rounds": _num(stats.get("Rounds"), int),
    }


def fetch(nickname: str, api_key: str, limit: int) -> dict:
    player = request("/players", {"nickname": nickname}, api_key)
    player_id = player["player_id"]
    cs2 = (player.get("games") or {}).get(GAME) or {}

    lifetime = {}
    try:
        lifetime = request(f"/players/{player_id}/stats/{GAME}", {}, api_key).get("lifetime") or {}
    except FaceitError as err:  # сводка за всё время — необязательная часть страницы
        print(f"::warning::Не удалось получить статистику за всё время: {err}", file=sys.stderr)

    matches = []
    for offset in range(0, min(limit, MAX_MATCHES), PAGE_SIZE):
        page = request(
            f"/players/{player_id}/games/{GAME}/stats",
            {"offset": offset, "limit": min(PAGE_SIZE, limit - offset)},
            api_key,
        )
        items = page.get("items") or []
        matches.extend(m for m in (normalize_match(i.get("stats") or {}) for i in items) if m)
        if len(items) < PAGE_SIZE:
            break

    return {
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "demo": False,
        "player": {
            "nickname": player.get("nickname") or nickname,
            "country": (player.get("country") or "").upper(),
            "elo": cs2.get("faceit_elo"),
            "level": cs2.get("skill_level"),
            "region": cs2.get("region"),
            "faceit_url": (player.get("faceit_url") or "").replace("{lang}", "ru"),
            "lifetime_matches": _num(lifetime.get("Matches"), int),
            "lifetime_win_rate": _num(lifetime.get("Win Rate %")),
        },
        "matches": matches,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Скачать статистику FACEIT для дашборда")
    parser.add_argument("--out", required=True, help="куда сохранить JSON")
    parser.add_argument("--matches", type=int, default=200, help=f"сколько последних матчей взять (до {MAX_MATCHES})")
    args = parser.parse_args()

    api_key = os.environ.get("FACEIT_API_KEY", "").strip()
    nickname = os.environ.get("FACEIT_NICKNAME", "").strip()
    if not api_key or not nickname:
        print("Нужны переменные окружения FACEIT_API_KEY и FACEIT_NICKNAME", file=sys.stderr)
        return 2

    try:
        data = fetch(nickname, api_key, max(1, min(args.matches, MAX_MATCHES)))
    except FaceitError as err:
        print(f"::error::{err}", file=sys.stderr)
        return 1

    if not data["matches"]:
        print(f"::error::У игрока {nickname} нет матчей CS2 5v5 в ответе FACEIT", file=sys.stderr)
        return 1
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(data, fh, ensure_ascii=False, indent=1)
    print(f"Сохранено матчей: {len(data['matches'])}, Elo: {data['player']['elo']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
