# Архитектура: модель C4

Два верхних уровня [модели C4](https://c4model.com): контекст (кто и с какими системами взаимодействует) и контейнеры (из каких развёртываемых частей состоит система). Нотация C4, отрисовано через mermaid `flowchart`, чтобы схема рендерилась прямо на GitHub.

Цвета по соглашению C4: тёмно-синий — люди, синий — наша система и её контейнеры, серый — внешние системы.

## Уровень 1. Контекст системы

```mermaid
flowchart TB
    classDef person fill:#08427b,stroke:#052e56,color:#fff
    classDef system fill:#1168bd,stroke:#0b4884,color:#fff
    classDef external fill:#8a8a8a,stroke:#5e5e5e,color:#fff

    fan(["Болельщик / аналитик<br/><i>[Человек]</i><br/>Следит за матчами и статистикой"]):::person
    clients["Клиентские приложения<br/><i>[Внешняя система]</i><br/>Фан-приложения, медиа, аналитические сервисы"]:::external
    cms["CS Match Stats<br/><i>[Программная система]</i><br/>Единый API статистики матчей Counter-Strike"]:::system
    feed["Провайдер матчевых данных<br/><i>[Внешняя система]</i><br/>Расписание, счёт и результаты в реальном времени"]:::external
    rank["Источник рейтинга команд<br/><i>[Внешняя система]</i><br/>Мировой рейтинг, обновляется еженедельно"]:::external

    fan -- "Смотрит расписание и счёт" --> clients
    clients -- "Запрашивает матчи<br/>[HTTPS, REST/JSON]" --> cms
    feed -- "Присылает события матчей<br/>[вебхуки, JSON]" --> cms
    cms -- "Забирает рейтинг<br/>[HTTPS, раз в сутки]" --> rank
```

| Элемент | Роль |
|---|---|
| Болельщик / аналитик | Конечный пользователь. С CS Match Stats напрямую не работает, только через клиентские приложения |
| Клиентские приложения | Потребители API: показывают расписание и live-счёт, строят аналитику |
| CS Match Stats | Система в фокусе: собирает данные из внешних источников и отдаёт их по единому контракту |
| Провайдер матчевых данных | Источник расписания и событий матча: старт карты, счёт, конец серии |
| Источник рейтинга команд | Источник мирового рейтинга для поля `worldRanking` |

## Уровень 2. Контейнеры

```mermaid
flowchart TB
    classDef container fill:#438dd5,stroke:#2e6295,color:#fff
    classDef external fill:#8a8a8a,stroke:#5e5e5e,color:#fff

    clients["Клиентские приложения<br/><i>[Внешняя система]</i>"]:::external
    feed["Провайдер матчевых данных<br/><i>[Внешняя система]</i>"]:::external
    rank["Источник рейтинга команд<br/><i>[Внешняя система]</i>"]:::external

    subgraph cms["CS Match Stats"]
        api["CS Match Stats API<br/><i>[Контейнер: REST-сервис]</i><br/>Отдаёт матчи по контракту openapi.yaml"]:::container
        ingest["Ingestion Service<br/><i>[Контейнер: обработчик событий]</i><br/>Проверяет события матча,<br/>обновляет счёт и статусы"]:::container
        sync["Ranking Sync<br/><i>[Контейнер: плановая задача]</i><br/>Раз в сутки обновляет рейтинг команд"]:::container
        db[("База данных<br/><i>[Контейнер: PostgreSQL]</i><br/>Турниры, матчи, команды, карты")]:::container
    end
    style cms fill:transparent,stroke:#1168bd,stroke-width:2px,stroke-dasharray:6 4

    clients -- "GET /matches<br/>GET /matches/{matchId}<br/>[HTTPS, JSON]" --> api
    api -- "Читает<br/>[SQL]" --> db
    feed -- "События матча<br/>[вебхуки, JSON]" --> ingest
    ingest -- "Пишет матчи, карты, статусы<br/>[SQL]" --> db
    sync -- "Забирает рейтинг<br/>[HTTPS]" --> rank
    sync -- "Обновляет рейтинг команд<br/>[SQL]" --> db
```

| Контейнер | Ответственность | Связанные артефакты |
|---|---|---|
| CS Match Stats API | Реализует публичный контракт: фильтры, пагинацию, ошибки. Только чтение | [openapi.yaml](openapi.yaml), [match-flow.md](match-flow.md) |
| Ingestion Service | Принимает события от провайдера, проверяет их и переводит матч по статусам. При старте матча фиксирует рейтинг команд | [match-lifecycle.md](match-lifecycle.md), [ADR-0003](adr/0003-ranking-snapshot.md) |
| Ranking Sync | Раз в сутки обновляет текущий рейтинг команд | [ADR-0003](adr/0003-ranking-snapshot.md) |
| База данных | Хранит турниры, матчи, участников и карты | [data-model.md](data-model.md) |

## Ключевые потоки

- **Чтение:** клиент → CS Match Stats API → база данных. Пошагово — в [match-flow.md](match-flow.md).
- **Запись:** провайдер → Ingestion Service → база данных. Правила смены статусов — в [match-lifecycle.md](match-lifecycle.md).
