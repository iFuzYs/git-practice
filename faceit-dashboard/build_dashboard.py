"""Сборка статической страницы FACEIT-дашборда.

    python build_dashboard.py --data data.json --out _site/faceit   # реальные данные
    python build_dashboard.py --demo --out preview                  # демо-данные
    python build_dashboard.py --placeholder --out _site/faceit      # дашборд ещё не настроен

Страница самодостаточна: без внешних скриптов, шрифтов и CDN.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from html import escape
from pathlib import Path

import analytics
import demo_data

RECENT_ROWS = 20
PLOT_W, PLOT_H = 1000, 240


# ---------- форматирование ----------

def fmt_pct(value: float | None) -> str:
    return "—" if value is None else f"{value:.0f}%"


def fmt_num(value: float | None, digits: int = 2) -> str:
    return "—" if value is None else f"{value:.{digits}f}"


def plural(n: int, one: str, few: str, many: str) -> str:
    if n % 10 == 1 and n % 100 != 11:
        return one
    if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14:
        return few
    return many


def matches_word(n: int) -> str:
    return f"{n} {plural(n, 'матч', 'матча', 'матчей')}"


def local_time(iso: str, tz, pattern: str) -> str:
    return analytics.parse_time(iso).astimezone(tz).strftime(pattern)


# ---------- графики ----------

def bar_chart(rows: list[dict], title_id: str) -> str:
    """Горизонтальные столбики винрейта (0–100%) с линией 50% и размером выборки в подписи."""
    items = []
    for r in rows:
        if not r["matches"]:
            continue
        low = "" if r["reliable"] else " low"
        note = "" if r["reliable"] else " · мало данных"
        tip_label = (
            f"{r['name']} · {r['wins']}–{r['losses']} · K/D {fmt_num(r['kd'])} · ADR {fmt_num(r['adr'], 0)}"
        )
        items.append(f"""
      <div class="bar-row{low}" tabindex="0" data-tip-value="{escape(fmt_pct(r['win_rate']))} побед" data-tip-label="{escape(tip_label)}">
        <span class="bar-label">{escape(r['name'])}</span>
        <span class="bar-track"><span class="bar" style="width:{r['win_rate']:.1f}%"></span></span>
        <span class="bar-value">{fmt_pct(r['win_rate'])}<small> · {matches_word(r['matches'])}{note}</small></span>
      </div>""")
    return f"""
    <div class="bars" role="list" aria-labelledby="{title_id}">
      <div class="bar-scale" aria-hidden="true"><span></span><span class="bar-scale-track"><span style="left:0">0%</span><span style="left:50%">50%</span><span style="left:100%">100%</span></span><span></span></div>{''.join(items)}
    </div>"""


def nice_ticks(lo: float, hi: float, target: int = 4) -> list[float]:
    span = hi - lo or abs(hi) or 1.0
    raw = span / target
    step = next(s for s in (0.05, 0.1, 0.2, 0.25, 0.5, 1, 2, 2.5, 5, 10, 20, 25, 50, 100) if s >= raw)
    start = int(lo // step) * step
    ticks, value = [], start
    while value <= hi + step * 0.001:
        ticks.append(round(value, 4))
        value += step
    if ticks[-1] < hi or len(ticks) < 2:  # шкале нужны минимум два деления
        ticks.append(round(ticks[-1] + step, 4))
    return ticks


def line_chart(values: list[float | None], labels: list[str], title_id: str, fmt: str, reference: float | None) -> str:
    """Один ряд: скользящее среднее. Перекрестие и подсказка — в скрипте страницы."""
    points = [(i, v) for i, v in enumerate(values) if v is not None]
    if len(points) < 2:
        return '<p class="empty">Недостаточно матчей для графика.</p>'
    digits = 2 if fmt == "kd" else 0
    vals = [v for _, v in points] + ([reference] if reference is not None else [])
    ticks = nice_ticks(min(vals), max(vals))
    lo, hi = ticks[0], ticks[-1]
    n = len(values)

    def x(i: int) -> float:
        return i / (n - 1) * PLOT_W

    def y(v: float) -> float:
        return PLOT_H - (v - lo) / (hi - lo) * PLOT_H

    grid = "".join(f'<line class="grid" x1="0" x2="{PLOT_W}" y1="{y(t):.1f}" y2="{y(t):.1f}"/>' for t in ticks)
    ref = ""
    if reference is not None:
        ref = f'<line class="ref" x1="0" x2="{PLOT_W}" y1="{y(reference):.1f}" y2="{y(reference):.1f}"/>'
    line = " ".join(f"{x(i):.1f},{y(v):.1f}" for i, v in points)
    ytick_html = "".join(
        f'<span class="ytick" style="top:{y(t) / PLOT_H * 100:.2f}%">{t:.{digits}f}</span>' for t in ticks
    )
    last_i, last_v = points[-1]
    data = [{"v": round(v, 3) if v is not None else None, "t": t} for v, t in zip(values, labels)]
    return f"""
    <div class="line-chart">
      <div class="yaxis" aria-hidden="true">{ytick_html}</div>
      <div class="plot" tabindex="0" role="img" aria-labelledby="{title_id}"
           data-format="{fmt}" data-lo="{lo}" data-hi="{hi}" data-points="{escape(json.dumps(data, ensure_ascii=False))}">
        <svg viewBox="0 0 {PLOT_W} {PLOT_H}" preserveAspectRatio="none" aria-hidden="true">{grid}{ref}
          <polyline class="series" points="{line}"/>
        </svg>
        <span class="dot end" style="left:{x(last_i) / PLOT_W * 100:.2f}%;top:{y(last_v) / PLOT_H * 100:.2f}%"></span>
        <span class="crosshair" hidden></span>
        <span class="dot hover" hidden></span>
      </div>
      <div class="end-label" style="top:{y(last_v) / PLOT_H * 100:.2f}%">{fmt_num(last_v, digits)}</div>
      <div class="xaxis" aria-hidden="true"><span>{escape(labels[0].split(' · ')[0])}</span><span>{escape(labels[-1].split(' · ')[0])}</span></div>
    </div>"""


# ---------- таблицы ----------

def split_table(rows: list[dict], first_col: str) -> str:
    body = "".join(
        f"<tr><td>{escape(r['name'])}</td><td>{r['matches']}</td><td>{r['wins']}–{r['losses']}</td>"
        f"<td>{fmt_pct(r['win_rate'])}</td><td>{fmt_num(r['kd'])}</td><td>{fmt_num(r['adr'], 0)}</td></tr>"
        for r in rows
        if r["matches"]
    )
    return f"""
    <details class="table-view"><summary>Таблица</summary>
      <div class="table-scroll"><table>
        <thead><tr><th>{first_col}</th><th>Матчей</th><th>П–П</th><th>Винрейт</th><th>K/D</th><th>ADR</th></tr></thead>
        <tbody>{body}</tbody>
      </table></div>
    </details>"""


def recent_table(matches: list[dict], tz) -> str:
    rows = []
    for m in reversed(matches[-RECENT_ROWS:]):
        result = '<span class="res win">Победа</span>' if m["result"] else '<span class="res loss">Поражение</span>'
        kd = analytics._ratio(m["kills"], m["deaths"])
        hs = fmt_pct(100 * m["headshots"] / m["kills"]) if m.get("headshots") is not None and m["kills"] else "—"
        room = f"https://www.faceit.com/ru/cs2/room/{m['id']}"
        map_cell = escape(analytics.map_label(m["map"]))
        if m["id"] and not m["id"].startswith("demo-"):
            map_cell = f'<a href="{escape(room)}">{map_cell}</a>'
        rows.append(
            f"<tr><td>{local_time(m['finished_at'], tz, '%d.%m, %H:%M')}</td><td>{map_cell}</td><td>{result}</td>"
            f"<td>{escape(m.get('score') or '—')}</td><td>{m['kills']}–{m['deaths']}–{m['assists']}</td>"
            f"<td>{fmt_num(kd)}</td><td>{fmt_num(m.get('adr'), 0)}</td><td>{hs}</td></tr>"
        )
    return f"""
    <div class="table-scroll"><table class="recent">
      <thead><tr><th>Когда</th><th>Карта</th><th>Итог</th><th>Счёт</th><th>K–D–A</th><th>K/D</th><th>ADR</th><th>HS</th></tr></thead>
      <tbody>{''.join(rows)}</tbody>
    </table></div>"""


# ---------- страница ----------

def kpi(label: str, value: str, note: str = "") -> str:
    note_html = f'<span class="kpi-note">{escape(note)}</span>' if note else ""
    return f'<div class="kpi"><span class="kpi-label">{escape(label)}</span><span class="kpi-value">{escape(value)}</span>{note_html}</div>'


def render(report: dict, repo_url: str) -> str:
    p, s, tz = report["player"], report["summary"], report["tz"]
    matches = report["matches"]
    tz_label = "МСК" if getattr(tz, "key", "") == "Europe/Moscow" or str(tz) == "MSK" else str(tz)
    updated = local_time(report["generated_at"], tz, "%d.%m.%Y %H:%M")
    labels = [
        f"{local_time(m['finished_at'], tz, '%d.%m')} · {analytics.map_label(m['map'])} · {'победа' if m['result'] else 'поражение'}"
        for m in matches
    ]
    level = f"{p['level']} уровень" if p.get("level") else ""
    profile = f' · <a href="{escape(p["faceit_url"])}">профиль на FACEIT</a>' if p.get("faceit_url") else ""
    lifetime = ""
    if p.get("lifetime_matches"):
        lifetime = f" · за всё время {matches_word(p['lifetime_matches'])}, {fmt_pct(p.get('lifetime_win_rate'))} побед"
    demo = (
        '<p class="banner">Демо-данные: страница собрана из сгенерированных матчей для проверки вёрстки. '
        "Реальная статистика появится после настройки ключа FACEIT.</p>" if report["demo"] else ""
    )
    streak = report["streaks"]
    streak_note = ""
    if streak["current_len"] >= 2:
        n = streak["current_len"]
        word = plural(n, "победа", "победы", "побед") if streak["current_win"] else plural(n, "поражение", "поражения", "поражений")
        streak_note = f"{n} {word} подряд"
    insights = "".join(f"<li>{escape(text)}</li>" for text in report["insights"]) or "<li>Пока мало матчей для выводов.</li>"

    body = f"""
  <header class="head">
    <div>
      <p class="eyebrow">FACEIT · Counter-Strike 2</p>
      <h1>{escape(p['nickname'])}</h1>
      <p class="sub">Последние {matches_word(len(matches))} 5v5{lifetime}{profile}</p>
    </div>
    <div class="hero">
      <span class="hero-value">{escape(str(p.get('elo') or '—'))}</span>
      <span class="hero-label">Elo{(' · ' + escape(level)) if level else ''}</span>
    </div>
  </header>
  {demo}
  <section class="kpis" aria-label="Сводка">
    {kpi('Винрейт', fmt_pct(s['win_rate']), f"{s['wins']}–{s['losses']}")}
    {kpi('K/D', fmt_num(s['kd']), streak_note)}
    {kpi('ADR', fmt_num(s['adr'], 0))}
    {kpi('Хедшоты', fmt_pct(s['hs_pct']))}
    {kpi('Убийств за матч', fmt_num(s['avg_kills'], 1))}
  </section>

  <section class="card">
    <h2>Выводы</h2>
    <ul class="insights">{insights}</ul>
    <p class="hint">Выводы строятся только по выборкам от {analytics.MIN_SAMPLE} матчей.</p>
  </section>

  <section class="card">
    <h2 id="maps-title">Винрейт по картам</h2>
    <p class="hint">Серым — карты, где меньше {analytics.MIN_SAMPLE} матчей: по ним рано делать выводы. Вертикальная линия — 50%.</p>
    {bar_chart(report['maps'], 'maps-title')}
    {split_table(report['maps'], 'Карта')}
  </section>

  <div class="grid-2">
    <section class="card">
      <h2 id="kd-title">Форма: K/D</h2>
      <p class="hint">Скользящее среднее за {analytics.ROLLING_WINDOW} матчей. Линия — K/D 1.00.</p>
      {line_chart(report['kd_trend'], labels, 'kd-title', 'kd', 1.0)}
    </section>
    <section class="card">
      <h2 id="adr-title">Форма: ADR</h2>
      <p class="hint">Скользящее среднее за {analytics.ROLLING_WINDOW} матчей: урон за раунд.</p>
      {line_chart(report['adr_trend'], labels, 'adr-title', 'adr', None)}
    </section>
  </div>

  <section class="card">
    <h2 id="time-title">Время суток</h2>
    <p class="hint">По времени окончания матча, {escape(tz_label)}. Серым — меньше {analytics.MIN_SAMPLE} матчей.</p>
    {bar_chart(report['times'], 'time-title')}
    {split_table(report['times'], 'Время')}
  </section>

  <section class="card">
    <h2>Последние {min(RECENT_ROWS, len(matches))} матчей</h2>
    {recent_table(matches, tz)}
  </section>

  <footer>
    Данные: FACEIT Data API. Обновлено {updated} ({escape(tz_label)}).
    Страницу собирает <a href="{escape(repo_url)}/tree/main/faceit-dashboard">faceit-dashboard</a> раз в сутки.
  </footer>"""
    return page(f"FACEIT: {p['nickname']}", body)


def render_placeholder(repo_url: str) -> str:
    body = f"""
  <section class="card setup">
    <p class="eyebrow">FACEIT · Counter-Strike 2</p>
    <h1>Дашборд ещё не настроен</h1>
    <p>Чтобы здесь появилась статистика, владельцу репозитория нужно один раз:</p>
    <ol>
      <li>Получить серверный API-ключ на <a href="https://developers.faceit.com/">developers.faceit.com</a>.</li>
      <li>Добавить его в репозиторий: Settings → Secrets and variables → Actions → секрет <code>FACEIT_API_KEY</code>.</li>
      <li>Там же на вкладке Variables добавить переменную <code>FACEIT_NICKNAME</code> с никнеймом на FACEIT.</li>
      <li>Запустить workflow «GitHub Pages» во вкладке Actions.</li>
    </ol>
    <p>Подробнее — в <a href="{escape(repo_url)}/tree/main/faceit-dashboard">README дашборда</a>.</p>
  </section>"""
    return page("FACEIT-дашборд", body)


def page(title: str, body: str) -> str:
    return f"""<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{escape(title)}</title>
<style>{CSS}</style>
</head>
<body>
<main>{body}
</main>
<div id="tip" role="tooltip" hidden><strong></strong><span></span></div>
<script>{JS}</script>
</body>
</html>
"""


CSS = """
:root {
  color-scheme: light;
  --page: #f9f9f7; --surface: #fcfcfb; --ink: #0b0b0b; --ink-2: #52514e; --muted: #898781;
  --grid: #e1e0d9; --axis: #c3c2b7; --border: rgba(11, 11, 11, 0.10);
  --series: #2a78d6; --good: #0ca30c; --bad: #d03b3b; --banner: #fff4d6;
}
@media (prefers-color-scheme: dark) {
  :root {
    color-scheme: dark;
    --page: #0d0d0d; --surface: #1a1a19; --ink: #ffffff; --ink-2: #c3c2b7; --muted: #898781;
    --grid: #2c2c2a; --axis: #383835; --border: rgba(255, 255, 255, 0.10);
    --series: #3987e5; --banner: #3a3012;
  }
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--page); color: var(--ink);
  font: 15px/1.5 system-ui, -apple-system, "Segoe UI", sans-serif; }
main { max-width: 980px; margin: 0 auto; padding: 24px 16px 40px; }
a { color: var(--series); }
h1 { font-size: 30px; line-height: 1.15; margin: 2px 0 6px; overflow-wrap: anywhere; }
h2 { font-size: 17px; margin: 0 0 4px; }
.eyebrow { margin: 0; font-size: 12px; letter-spacing: .06em; text-transform: uppercase; color: var(--muted); }
.sub, .hint, footer { color: var(--ink-2); }
.sub { margin: 0; }
.hint { font-size: 13px; margin: 0 0 14px; }
.head { display: flex; justify-content: space-between; align-items: flex-end; gap: 16px; flex-wrap: wrap; margin-bottom: 20px; }
.hero { display: flex; flex-direction: column; align-items: flex-end; }
.hero-value { font-size: 56px; font-weight: 600; line-height: 1; }
.hero-label { color: var(--ink-2); font-size: 14px; }
.banner { background: var(--banner); border: 1px solid var(--border); border-radius: 10px; padding: 10px 14px; margin: 0 0 16px; }
.kpis { display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; margin-bottom: 16px; }
.kpi, .card { background: var(--surface); border: 1px solid var(--border); border-radius: 12px; }
.kpi { padding: 14px 16px; display: flex; flex-direction: column; gap: 2px; }
.kpi-label { font-size: 13px; color: var(--ink-2); }
.kpi-value { font-size: 28px; font-weight: 600; }
.kpi-note { font-size: 13px; color: var(--muted); }
.card { padding: 18px 18px 16px; margin-bottom: 16px; min-width: 0; }
.grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 0 16px; }
.insights { margin: 8px 0 10px; padding-left: 20px; }
.insights li { margin-bottom: 6px; }

.bars { display: flex; flex-direction: column; gap: 2px; }
.bar-row, .bar-scale { display: grid; grid-template-columns: minmax(84px, 120px) 1fr minmax(118px, auto); align-items: center; gap: 10px; }
.bar-row { padding: 5px 0; border-radius: 6px; outline-offset: 2px; }
.bar-row:hover, .bar-row:focus-visible { background: color-mix(in srgb, var(--series) 7%, transparent); }
.bar-label { font-size: 14px; overflow-wrap: anywhere; }
.bar-track { position: relative; height: 18px; }
.bar-track::after { content: ""; position: absolute; left: 50%; top: -5px; bottom: -5px; width: 1px; background: var(--axis); }
.bar { position: absolute; left: 0; top: 0; bottom: 0; background: var(--series); border-radius: 0 4px 4px 0; min-width: 2px; }
.bar-row.low .bar { background: var(--muted); }
.bar-value { font-size: 14px; font-weight: 600; font-variant-numeric: tabular-nums; }
.bar-value small { font-weight: 400; color: var(--ink-2); font-size: 13px; }
.bar-scale { font-size: 12px; color: var(--muted); height: 16px; }
.bar-scale-track { position: relative; height: 16px; }
.bar-scale-track span { position: absolute; transform: translateX(-50%); }
.bar-scale-track span:first-child { transform: none; }
.bar-scale-track span:last-child { transform: translateX(-100%); }

.line-chart { display: grid; grid-template-columns: 36px 1fr 40px; grid-template-rows: 200px auto; column-gap: 6px; margin-top: 6px; }
.yaxis { position: relative; }
.ytick { position: absolute; right: 0; transform: translateY(-50%); font-size: 12px; color: var(--muted); font-variant-numeric: tabular-nums; }
.plot { position: relative; outline-offset: 4px; cursor: crosshair; touch-action: pan-y; }
.plot svg { display: block; width: 100%; height: 100%; overflow: visible; }
.plot .grid { stroke: var(--grid); stroke-width: 1px; vector-effect: non-scaling-stroke; }
.plot .ref { stroke: var(--axis); stroke-width: 1px; vector-effect: non-scaling-stroke; }
.plot .series { fill: none; stroke: var(--series); stroke-width: 2px; stroke-linejoin: round; stroke-linecap: round; vector-effect: non-scaling-stroke; }
.dot { position: absolute; width: 10px; height: 10px; margin: -5px 0 0 -5px; border-radius: 50%;
  background: var(--series); box-shadow: 0 0 0 2px var(--surface); pointer-events: none; }
.crosshair { position: absolute; top: 0; bottom: 0; width: 1px; background: var(--axis); pointer-events: none; }
.end-label { position: relative; font-size: 13px; font-weight: 600; transform: translateY(-50%); height: 0; font-variant-numeric: tabular-nums; }
.xaxis { grid-column: 2; display: flex; justify-content: space-between; font-size: 12px; color: var(--muted); padding-top: 6px; }
.empty { color: var(--muted); }

.table-view { margin-top: 12px; font-size: 14px; }
.table-view summary { cursor: pointer; color: var(--ink-2); }
.table-scroll { overflow-x: auto; margin-top: 8px; }
table { border-collapse: collapse; width: 100%; font-size: 14px; font-variant-numeric: tabular-nums; }
th { text-align: left; font-weight: 600; color: var(--ink-2); font-size: 13px; }
th, td { padding: 7px 10px 7px 0; border-bottom: 1px solid var(--grid); white-space: nowrap; }
.res::before { content: ""; display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 6px; vertical-align: 1px; }
.res.win::before { background: var(--good); }
.res.loss::before { background: var(--bad); }

.setup { max-width: 640px; margin: 40px auto; }
.setup li { margin-bottom: 6px; }
code { font-size: 13px; background: var(--page); border: 1px solid var(--border); border-radius: 4px; padding: 1px 4px; }
footer { font-size: 13px; margin-top: 8px; }

#tip { position: fixed; z-index: 10; max-width: 280px; padding: 8px 10px; border-radius: 8px; pointer-events: none;
  background: var(--surface); color: var(--ink); border: 1px solid var(--border); box-shadow: 0 4px 16px rgba(0, 0, 0, .12);
  display: flex; flex-direction: column; font-size: 13px; }
#tip[hidden] { display: none; }
#tip strong { font-size: 16px; font-variant-numeric: tabular-nums; }
#tip span { color: var(--ink-2); }
@media (max-width: 520px) {
  .hero { align-items: flex-start; }
  .bar-row, .bar-scale { grid-template-columns: 76px 1fr; }
  .bar-value { grid-column: 2; }
  .bar-scale > span:last-child { display: none; }
}
"""

JS = """
(() => {
  const tip = document.getElementById('tip');
  const [tipValue, tipLabel] = [tip.querySelector('strong'), tip.querySelector('span')];

  function show(value, label, x, y) {
    tipValue.textContent = value;
    tipLabel.textContent = label;
    tip.hidden = false;
    const w = tip.offsetWidth, h = tip.offsetHeight;
    const left = Math.min(Math.max(8, x + 14), window.innerWidth - w - 8);
    const top = y - h - 12 < 8 ? y + 16 : y - h - 12;
    tip.style.left = left + 'px';
    tip.style.top = top + 'px';
  }
  const hide = () => { tip.hidden = true; };

  document.querySelectorAll('.bar-row').forEach((row) => {
    const at = (x, y) => show(row.dataset.tipValue, row.dataset.tipLabel, x, y);
    row.addEventListener('pointermove', (e) => at(e.clientX, e.clientY));
    row.addEventListener('pointerleave', hide);
    row.addEventListener('focus', () => { const r = row.getBoundingClientRect(); at(r.left + r.width / 2, r.top); });
    row.addEventListener('blur', hide);
  });

  document.querySelectorAll('.plot').forEach((plot) => {
    const points = JSON.parse(plot.dataset.points);
    const digits = plot.dataset.format === 'kd' ? 2 : 0;
    const name = plot.dataset.format === 'kd' ? 'K/D' : 'ADR';
    const valid = points.map((p, i) => (p.v === null ? -1 : i)).filter((i) => i >= 0);
    const lo = parseFloat(plot.dataset.lo), hi = parseFloat(plot.dataset.hi);
    const cross = plot.querySelector('.crosshair'), dot = plot.querySelector('.dot.hover');
    let current = valid[valid.length - 1];

    function select(i, clientX, clientY) {
      current = i;
      const p = points[i];
      const x = (i / (points.length - 1)) * 100, y = (1 - (p.v - lo) / (hi - lo)) * 100;
      cross.hidden = dot.hidden = false;
      cross.style.left = x + '%';
      dot.style.left = x + '%';
      dot.style.top = y + '%';
      const r = plot.getBoundingClientRect();
      show(p.v.toFixed(digits), name + ' за 10 матчей · ' + p.t,
           clientX ?? r.left + (r.width * x) / 100, clientY ?? r.top + (r.height * y) / 100);
    }
    const nearest = (clientX) => {
      const r = plot.getBoundingClientRect();
      const raw = Math.round(((clientX - r.left) / r.width) * (points.length - 1));
      return valid.reduce((best, i) => (Math.abs(i - raw) < Math.abs(best - raw) ? i : best), valid[0]);
    };
    const clear = () => { cross.hidden = dot.hidden = true; hide(); };
    plot.addEventListener('pointermove', (e) => select(nearest(e.clientX), e.clientX, e.clientY));
    plot.addEventListener('pointerleave', clear);
    plot.addEventListener('focus', () => select(current));
    plot.addEventListener('blur', clear);
    plot.addEventListener('keydown', (e) => {
      const pos = valid.indexOf(current);
      if (e.key === 'ArrowLeft' && pos > 0) { select(valid[pos - 1]); e.preventDefault(); }
      if (e.key === 'ArrowRight' && pos < valid.length - 1) { select(valid[pos + 1]); e.preventDefault(); }
    });
  });
})();
"""


def main() -> int:
    parser = argparse.ArgumentParser(description="Собрать страницу FACEIT-дашборда")
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--data", help="JSON из fetch_stats.py")
    source.add_argument("--demo", action="store_true", help="собрать из демо-данных")
    source.add_argument("--placeholder", action="store_true", help="страница «дашборд не настроен»")
    parser.add_argument("--out", required=True, help="папка для index.html")
    parser.add_argument("--tz", default=os.environ.get("FACEIT_TZ") or "Europe/Moscow", help="часовой пояс")
    parser.add_argument("--repo-url", default="https://github.com/iFuzYs/git-practice")
    args = parser.parse_args()

    if args.placeholder:
        html = render_placeholder(args.repo_url)
    else:
        data = demo_data.generate() if args.demo else json.loads(Path(args.data).read_text(encoding="utf-8"))
        html = render(analytics.build_report(data, args.tz), args.repo_url)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    (out / "index.html").write_text(html, encoding="utf-8")
    print(f"Готово: {out / 'index.html'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
