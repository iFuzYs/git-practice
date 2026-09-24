# Модель данных

Логическая модель, на которой держится API. Физическая схема БД может отличаться (индексы, служебные поля), но сущности и связи здесь те же.

```mermaid
erDiagram
    EVENT |o--o{ MATCH : "включает"
    MATCH ||--|{ MATCH_TEAM : "участники"
    TEAM ||--o{ MATCH_TEAM : "играет"
    MATCH ||--o{ MATCH_MAP : "карты серии"
    TEAM |o--o{ MATCH_MAP : "пикнула"

    EVENT {
        string id PK "iem-cologne-2026"
        string name "IEM Cologne 2026"
    }
    MATCH {
        string id PK "m-2026-0731-01"
        string event_id FK "NULL для шоу-матча"
        string status "upcoming, live, finished"
        timestamp starts_at
        string format "bo1, bo3, bo5"
    }
    TEAM {
        string id PK "t-navi"
        string name "Natus Vincere"
        int current_world_ranking "обновляет Ranking Sync"
    }
    MATCH_TEAM {
        string match_id PK, FK
        int slot PK "1 или 2"
        string team_id FK
        int world_ranking_snapshot "фиксируется при старте матча"
    }
    MATCH_MAP {
        string match_id PK, FK
        int map_number PK "1..5"
        string map_name "Mirage"
        string picked_by_team_id FK "NULL для десайдера"
        int rounds_team1
        int rounds_team2
    }
```

## Сущности

| Сущность | Что хранит | Откуда данные |
|---|---|---|
| `EVENT` | Турнир | Провайдер матчевых данных |
| `MATCH` | Серию между двумя командами: статус, время начала, формат | Провайдер → Ingestion Service |
| `TEAM` | Команду и её текущее место в мировом рейтинге | Провайдер (команды), Ranking Sync (рейтинг) |
| `MATCH_TEAM` | Участие команды в матче: слот (team1 или team2) и рейтинг на момент матча | Ingestion Service |
| `MATCH_MAP` | Карту серии: порядковый номер, кто её выбрал, счёт по раундам | Ingestion Service |

## Бизнес-правила

1. У матча ровно два участника в слотах 1 и 2, и это разные команды.
2. Карт не больше, чем допускает формат: bo1 — 1, bo3 — 3, bo5 — 5.
3. `picked_by_team_id` — одна из двух команд матча или `NULL`, если карта — десайдер.
4. Счёт по раундам не может быть отрицательным.
5. `world_ranking_snapshot` заполняется при переходе матча в `live` и дальше не меняется ([ADR-0003](adr/0003-ranking-snapshot.md)).
6. Матч может проходить вне турнира (шоу-матч). Тогда `event_id` пустой и `eventName` в API не передаётся.

## Соответствие API и модели

| Поле API | Источник в модели |
|---|---|
| `Match.id`, `status`, `startsAt`, `format` | `MATCH.id`, `status`, `starts_at`, `format` |
| `Match.eventName` | `EVENT.name` через `MATCH.event_id` |
| `Match.teams[]` | `MATCH_TEAM` по возрастанию `slot`, данные команды из `TEAM` |
| `Team.worldRanking` | Для `upcoming` — `TEAM.current_world_ranking`, для `live` и `finished` — `MATCH_TEAM.world_ranking_snapshot` |
| `Match.maps[]` | `MATCH_MAP` по возрастанию `map_number` |
| `MapResult.pickedBy` | `MATCH_MAP.picked_by_team_id` |
| `MapResult.roundsTeam1`, `roundsTeam2` | `MATCH_MAP.rounds_team1`, `rounds_team2`; номер команды совпадает со `slot` |
| Фильтр `eventId` в `GET /matches` | `MATCH.event_id`. Через API его сейчас не узнать, см. [Q-1](README.md#открытые-вопросы) |
