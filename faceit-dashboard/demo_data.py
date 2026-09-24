"""Детерминированные демо-данные для проверки вёрстки без ключа FACEIT.

Страница, собранная из них, помечается баннером «Демо-данные».
"""

from __future__ import annotations

import random
from datetime import datetime, timedelta, timezone

# Карта: (вероятность победы, средний K/D) — чтобы у карт была разная «сила».
MAP_POOL = {
    "de_mirage": (0.62, 1.22),
    "de_inferno": (0.55, 1.10),
    "de_ancient": (0.50, 1.02),
    "de_nuke": (0.44, 0.94),
    "de_anubis": (0.52, 1.05),
    "de_dust2": (0.58, 1.15),
    "de_train": (0.40, 0.90),
}
MAP_WEIGHTS = (30, 24, 16, 12, 10, 6, 2)


def generate(count: int = 200, seed: int = 7) -> dict:
    rng = random.Random(seed)
    now = datetime(2026, 9, 24, 18, 0, tzinfo=timezone.utc)
    matches = []
    moment = now
    for i in range(count):
        moment -= timedelta(hours=rng.uniform(3, 30))
        name = rng.choices(list(MAP_POOL), weights=MAP_WEIGHTS)[0]
        win_p, kd = MAP_POOL[name]
        hour_msk = (moment.hour + 3) % 24
        if hour_msk < 6:  # ночью играется хуже
            win_p -= 0.12
            kd -= 0.12
        win = rng.random() < win_p
        rounds = rng.randint(19, 30)
        deaths = max(5, round(rng.gauss(17, 3)))
        kills = max(3, round(deaths * max(0.4, rng.gauss(kd + (0.1 if win else -0.1), 0.25))))
        loser = rng.randint(3, 11)
        matches.append({
            "id": f"demo-{count - i:04d}",
            "finished_at": moment.strftime("%Y-%m-%dT%H:%M:%SZ"),
            "map": name,
            "result": int(win),
            "score": f"13 / {loser}" if win else f"{loser} / 13",
            "kills": kills,
            "deaths": deaths,
            "assists": rng.randint(1, 9),
            "headshots": round(kills * rng.uniform(0.38, 0.62)),
            "adr": round(rng.gauss(62 + kills * 1.1, 8), 1),
            "rounds": rounds,
        })
    return {
        "generated_at": now.strftime("%Y-%m-%dT%H:%M:%SZ"),
        "demo": True,
        "player": {
            "nickname": "demo-player",
            "country": "RU",
            "elo": 2600,
            "level": 10,
            "region": "EU",
            "faceit_url": "",
            "lifetime_matches": 2143,
            "lifetime_win_rate": 54.0,
        },
        "matches": matches,
    }
