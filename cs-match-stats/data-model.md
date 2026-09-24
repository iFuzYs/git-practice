# Модель данных

Логическая модель, на которой держится API. Физическая схема БД может отличаться (индексы, служебные поля), но сущности и связи здесь те же.

```mermaid
erDiagram
    EVENT |o--o{ MATCH : "включает"
    MATCH ||--|{ MATCH_TEAM : "участники"
    TEAM ||--o{ MATCH_TEAM : "играет"
    MATCH ||--o{ MATCH_MAP : "карты серии"
    TEAM |o--o{ MATCH_MAP : "пикнула"
    TEAM |o--o{ MATCH : "победила"

    EVENT {
        string id PK "iem-cologne-2026"
        string name "IEM Cologne 2026"
        date start_date
        date end_date
    }
    MATCH {
        string id PK "m-2026-0731-01"
        string event_id FK "NULL для шоу-матча"
        string status "upcoming, live, finished, cancelled"
        timestamp starts_at
        string format "bo1, bo3, bo5"
        string winner_team_id FK "только для finished"
        boolean forfeit "техническое поражение"
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
        int world_ranking_snapshot "фиксируется при выходе из upcoming"
    }
    MATCH_MAP {
        string match_id PK, FK
        int map_number PK "1..5"
        string map_name "Mirage"
        string status "live, finished"
        string picked_by_team_id FK "NULL для десайдера"
        int rounds_team1
        int rounds_team2
    }
```

## Сущности

| Сущность | Что хранит | Откуда данные |
|---|---|---|
| `EVENT` | Турнир и его даты | Провайдер матчевых данных |
| `MATCH` | Серию между двумя командами: статус, время начала, формат, победителя | Провайдер → Ingestion Service |
| `TEAM` | Команду и её текущее место в мировом рейтинге | Провайдер (команды), Ranking Sync (рейтинг) |
| `MATCH_TEAM` | Участие команды в матче: слот (team1 или team2) и рейтинг на момент матча | Ingestion Service |
| `MATCH_MAP` | Карту серии: порядковый номер, статус, кто её выбрал, счёт по раундам | Ingestion Service |

## Бизнес-правила

1. У матча ровно два участника в слотах 1 и 2, и это разные команды.
2. Карт не больше, чем допускает формат: bo1 — 1, bo3 — 3, bo5 — 5.
3. `picked_by_team_id` — одна из двух команд матча или `NULL`, если карта — десайдер.
4. Счёт по раундам не может быть отрицательным.
5. `world_ranking_snapshot` заполняется, когда матч выходит из `upcoming`, и дальше не меняется ([ADR-0003](adr/0003-ranking-snapshot.md), [ADR-0004](adr/0004-cancelled-and-forfeit.md)).
6. Матч может проходить вне турнира (шоу-матч). Тогда `event_id` пустой, а `eventId` и `eventName` в API не передаются.
7. `winner_team_id` — одна из двух команд матча. Заполняется только в статусе `finished`, `forfeit = true` допустим только вместе с ним.
8. В матче не больше одной карты со статусом `live`, и только пока матч в статусе `live`.

## Соответствие API и модели

| Поле API | Источник в модели |
|---|---|
| `Match.id`, `status`, `startsAt`, `format` | `MATCH.id`, `status`, `starts_at`, `format` |
| `Match.eventId`, `eventName` | `MATCH.event_id`, `EVENT.name` |
| `Match.teams[]` | `MATCH_TEAM` по возрастанию `slot`, данные команды из `TEAM` |
| `Team.worldRanking` | Для `upcoming` — `TEAM.current_world_ranking`, для остальных статусов — `MATCH_TEAM.world_ranking_snapshot` |
| `Match.winnerTeamId`, `forfeit` | `MATCH.winner_team_id`, `forfeit` |
| `Match.maps[]` | `MATCH_MAP` по возрастанию `map_number` |
| `MapResult.status` | `MATCH_MAP.status` |
| `MapResult.pickedBy` | `MATCH_MAP.picked_by_team_id` |
| `MapResult.roundsTeam1`, `roundsTeam2` | `MATCH_MAP.rounds_team1`, `rounds_team2`; номер команды совпадает со `slot` |
| Фильтр `eventId` в `GET /matches` | `MATCH.event_id` |
| `Event.id`, `name`, `startDate`, `endDate` (`GET /events`) | `EVENT.id`, `name`, `start_date`, `end_date` |
