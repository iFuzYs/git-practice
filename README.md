# Практика системного анализа

Артефакты системного аналитика в формате docs-as-code: всё в Markdown и mermaid, диаграммы рендерятся прямо на GitHub, изменения проходят через pull request, контракты API проверяются в CI.

## Кейсы

### CS Match Stats API

Публичный API статистики матчей Counter-Strike 2 — от проблемы и требований до контракта и архитектурных решений.

- [Обзор кейса](cs-match-stats/README.md): проблема, стейкхолдеры, скоуп, нефункциональные требования, открытые вопросы
- [Документация API (Swagger UI)](https://ifuzys.github.io/git-practice/)
- Артефакты: [user stories с критериями в Gherkin](cs-match-stats/user-stories.md) · [C4](cs-match-stats/architecture.md) · [ER-модель](cs-match-stats/data-model.md) · [диаграмма состояний](cs-match-stats/match-lifecycle.md) · [sequence](cs-match-stats/match-flow.md) · [OpenAPI 3.0](cs-match-stats/openapi.yaml) · [ADR](cs-match-stats/adr/)

### Заказ такси

Процесс заказа такси от ввода адреса до оплаты.

- [Sequence-диаграмма](taxi-order-flow.md): обмен сообщениями между сервисами
- [BPMN 2.0](taxi-order-bpmn.md): участники, решения и варианты завершения процесса

## Автоматические проверки

| Workflow | Когда запускается | Что делает |
|---|---|---|
| [OpenAPI lint](.github/workflows/openapi-lint.yml) | Pull request и push в `main` с изменениями спецификаций | Spectral проверяет `openapi.yaml` по правилам [.spectral.yaml](.spectral.yaml) |
| [API docs](.github/workflows/api-docs.yml) | Push в `main` | Публикует Swagger UI на GitHub Pages |
| [COASTLINE](.github/workflows/coastline.yml) | Изменения в `coastline/` | Собирает игру под Linux, Windows и браузер, прогоняет тесты симуляции |

## Песочница

[hello.txt](hello.txt) и [game.py](game.py) (игра «Угадай число») — первые шаги с Git.

[Dust Raiders: Космопорт](dust-raiders/) — браузерный extraction-шутер от третьего лица по мотивам ARC Raiders на Three.js: машины-враги с уязвимыми местами, лут, гранаты и эвакуация на лифте. Открывается одним файлом `index.html`.

[CORAL City](coral-city/) — браузерная игра с открытым миром в духе GTA VI на Three.js: вымышленный город во Флориде с даунтауном, островом, портом и болотами, трафик и пешеходы, полиция с розыском до пяти звёзд, сюжет из шести заданий, гонки, такси, трамплины и радио. Открывается одним файлом `index.html`.

[COASTLINE — Фестиваль Побережья](coastline/) — гоночная игра с открытым миром в духе Forza Horizon на C++ и raylib. Процедурный остров с шоссе, серпантином, городом и лесами, смена суток, 14 машин с подробной физикой, 14 гонок против ИИ, трюки и цепочки навыков, гараж, автосалон и колесо удачи. Собирается в нативное приложение и в WebAssembly.
