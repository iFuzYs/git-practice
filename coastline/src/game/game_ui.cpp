// Экраны и HUD
#include <algorithm>
#include <cstdio>

#include "game.h"
#include "rlgl.h"

namespace cl {

namespace {

std::string fmtTime(float t) {
    if (t < 0) t = 0;
    int m = (int)(t / 60);
    float s = t - m * 60;
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%05.2f", m, s);
    return buf;
}

std::string fmtCr(int v) {
    std::string s = std::to_string(std::abs(v));
    std::string out;
    int n = 0;
    for (int i = (int)s.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), s[i]);
        if (++n % 3 == 0 && i > 0) out.insert(out.begin(), ' ');
    }
    return (v < 0 ? "-" : "") + out + " CR";
}

const char* placeWord(int p) {
    static char buf[16];
    std::snprintf(buf, sizeof buf, "%d-е", p);
    return buf;
}

Color eventColor(EventType t) {
    switch (t) {
        case EV_ROAD: return pal::orange;
        case EV_DIRT: return Color{230, 170, 60, 255};
        case EV_CROSS: return pal::green;
        case EV_DRAG: return pal::cyan;
        default: return pal::pink;
    }
}

const char* offOn(bool b) { return b ? "Вкл" : "Выкл"; }

constexpr int TAB_RACEMENU = 100;

// оценки характеристик 0..10 для карточки машины
struct CarRatings { float speed, handling, accel, launch, braking, offroad; };
CarRatings ratings(const CarSpec& s) {
    CarRatings r;
    r.speed = clampf((s.topSpeedKmh - 130) / 290.0f * 10, 1, 10);
    r.handling = clampf((s.grip - 0.85f) / 0.55f * 8 + s.clA * 4 + (1.6f - s.cgH) * 0.8f, 1, 10);
    float pw = s.powerKW / s.mass;
    r.accel = clampf(pw / 0.55f * 10, 1, 10);
    r.launch = clampf(r.accel * (s.drive == DRIVE_AWD ? 1.15f : s.drive == DRIVE_RWD ? 0.9f : 0.85f) * (s.electric ? 1.2f : 1.0f), 1, 10);
    r.braking = clampf(s.brakeTorque * s.grip / s.mass * 2.4f, 1, 10);
    r.offroad = clampf((s.looseGrip - 0.7f) / 0.45f * 7 + s.travel * 10 + (s.drive == DRIVE_AWD ? 1.5f : 0), 1, 10);
    return r;
}

const char* driveName(Drive d) { return d == DRIVE_AWD ? "Полный привод" : d == DRIVE_FWD ? "Передний привод" : "Задний привод"; }

void piBadge(const Ui& ui, float x, float y, int pi, float h = 28) {
    Color c = classColor(classIndex(pi));
    float w = h * 2.3f;
    ui.rrect({x, y, h, h}, 0.2f, c);
    ui.text(piClass(pi), x + h * 0.5f, y + h * 0.13f, h * 0.7f, pal::dark, F_BOLD, 1);
    ui.rrect({x + h - 2, y, w - h, h}, 0.2f, WHITE);
    ui.text(std::to_string(pi), x + h + (w - h) * 0.5f - 2, y + h * 0.13f, h * 0.7f, pal::dark, F_BOLD, 1);
}

}  // namespace

// ------------------------------------------------------------------ загрузка
void Game::drawBoot() {
    ui.beginFrame(0);
    ui.gradV({0, 0, ui.W, ui.H}, Color{10, 12, 26, 255}, Color{40, 16, 40, 255});
    float cx = ui.W * 0.5f, cy = ui.H * 0.42f;
    ui.text("COASTLINE", cx, cy - 70, 92, pal::orange, F_DISPLAY, 1, true);
    ui.text("ФЕСТИВАЛЬ  ПОБЕРЕЖЬЯ", cx, cy + 34, 26, pal::pink, F_BOLD, 1);
    float bw = 420;
    ui.rrect({cx - bw / 2, cy + 110, bw, 8}, 0.5f, alpha(WHITE, 0.15f));
    ui.rrect({cx - bw / 2, cy + 110, bw * std::max(0.03f, bootProgress_), 8}, 0.5f, pal::orange);
    ui.text(bootMsg_, cx, cy + 132, 20, pal::muted, F_REG, 1);
}

// ------------------------------------------------------------------ титульный экран
void Game::drawTitle(float dt) {
    (void)dt;
    ui.gradH({0, 0, ui.W * 0.6f, ui.H}, Color{6, 8, 16, 230}, Color{6, 8, 16, 0});
    float x = 80, y = ui.H * 0.2f;
    ui.text("COASTLINE", x, y, 100, pal::orange, F_DISPLAY, 0, true);
    ui.text("ФЕСТИВАЛЬ  ПОБЕРЕЖЬЯ", x + 6, y + 108, 26, pal::pink, F_BOLD);
    ui.text("Открытый мир · 14 машин · 14 событий · 18 трюков", x + 6, y + 144, 20, pal::muted);
    std::vector<std::string> items;
    if (prof.started) items = {"Продолжить", "Новая игра", "Настройки", "Управление"};
    else items = {"Начать игру", "Настройки", "Управление"};
#if !defined(PLATFORM_WEB)
    items.push_back("Выход");
#endif
    int n = (int)items.size();
    if (ui.nav.up) { sel = (sel + n - 1) % n; audio.play(SFX_UI_MOVE); }
    if (ui.nav.down) { sel = (sel + 1) % n; audio.play(SFX_UI_MOVE); }
    float by = y + 210;
    for (int i = 0; i < n; i++) {
        Rectangle r{x, by + i * 62.0f, 340, 50};
        if (ui.hover(r) && ui.mouseMoved) sel = i;
        if (ui.button(r, items[i], sel == i, 24)) {
            audio.play(SFX_UI_OK);
            const std::string& it = items[i];
            if (it == "Продолжить") {
                setPlayerCar(currentSpec()->id, prof.garage[std::clamp(prof.current, 0, (int)prof.garage.size() - 1)].color, false);
                const Place* fp = world.place("festival");
                if (fp) teleportPlayer(fp->p + V3{18, 0, 26}, 0.7f);
                enterState(GS_DRIVE);
                fade = 1;
                ui.toast("С возвращением на фестиваль!", "Уровень " + std::to_string(prof.level) + " · " + fmtCr(prof.credits), pal::orange, 3.5f);
            } else if (it == "Новая игра" || it == "Начать игру") {
                starterSel = 0;
                enterState(GS_STARTER);
                fade = 1;
            } else if (it == "Настройки" || it == "Управление") {
                pauseFrom = GS_TITLE;
                tab = it == "Настройки" ? TAB_SETTINGS : TAB_CONTROLS;
                state = GS_PAUSE;
                stateT = 0;
                tabSel = 0;
            } else if (it == "Выход") {
                quit = true;
            }
            break;
        }
    }
    ui.text("Все машины, звуки, музыка и мир созданы процедурно · C++ / raylib", x, ui.H - 44, 17, alpha(pal::muted, 0.8f));
    ui.keycap("Enter", ui.W - 250, ui.H - 50, 28);
    ui.text("выбрать", ui.W - 170, ui.H - 46, 20, pal::muted);
}

// ------------------------------------------------------------------ выбор первой машины
void Game::drawStarter(float dt) {
    (void)dt;
    int n = (int)starters.size();
    if (ui.nav.left) { starterSel = (starterSel + n - 1) % n; audio.play(SFX_UI_MOVE); }
    if (ui.nav.right) { starterSel = (starterSel + 1) % n; audio.play(SFX_UI_MOVE); }
    ui.gradV({0, 0, ui.W, 140}, Color{6, 8, 16, 220}, Color{6, 8, 16, 0});
    ui.text("ВЫБЕРИТЕ ПЕРВУЮ МАШИНУ", ui.W * 0.5f, 34, 44, WHITE, F_DISPLAY, 1, true);
    ui.text("Остальные можно купить в автосалоне или выиграть на колесе удачи", ui.W * 0.5f, 92, 20, pal::muted, F_REG, 1);
    const CarSpec* s = findCar(starters[starterSel]);
    if (!s) return;
    Rectangle card{ui.W - 470, 170, 420, 400};
    ui.rrect(card, 0.06f, pal::panel);
    ui.text(s->maker, card.x + 24, card.y + 18, 20, pal::muted, F_BOLD);
    ui.text(s->name, card.x + 24, card.y + 42, 36, WHITE, F_DISPLAY);
    piBadge(ui, card.x + 24, card.y + 94, s->pi);
    ui.text(driveName(s->drive), card.x + 110, card.y + 98, 20, pal::muted);
    drawCarStats(*s, {card.x + 24, card.y + 140, card.width - 48, 200});
    for (int i = 0; i < n; i++) {
        float cx = ui.W * 0.5f + (i - 1) * 26.0f;
        ui.circle({cx, ui.H - 110}, i == starterSel ? 7.0f : 5.0f, i == starterSel ? pal::orange : alpha(WHITE, 0.5f));
    }
    Rectangle lb{ui.W * 0.5f - 300, ui.H - 130, 60, 44}, rb{ui.W * 0.5f + 240, ui.H - 130, 60, 44};
    if (ui.button(lb, "", false, 34)) starterSel = (starterSel + n - 1) % n;
    if (ui.button(rb, "", false, 34)) starterSel = (starterSel + 1) % n;
    ui.chevron({lb.x + lb.width * 0.5f, lb.y + lb.height * 0.5f}, 22, -1, WHITE);
    ui.chevron({rb.x + rb.width * 0.5f, rb.y + rb.height * 0.5f}, 22, 1, WHITE);
    Rectangle ok{ui.W * 0.5f - 160, ui.H - 70, 320, 50};
    if (ui.button(ok, "Выбрать и поехать", true, 24)) {
        audio.play(SFX_REWARD);
        newGame(starters[starterSel]);
        enterState(GS_DRIVE);
        fade = 1;
        int ev = fest.eventIndex("coast_sprint");
        if (ev >= 0) {
            waypoint = fest.events[ev].marker;
            hasWaypoint = true;
        }
        ui.toast("Добро пожаловать на Фестиваль Побережья!", "Первое событие отмечено жёлтым лучом — «Прибрежный спринт»", pal::orange, 6.0f);
    }
    if (ui.nav.back) {
        enterState(GS_TITLE);
        audio.play(SFX_UI_BACK);
    }
}

void Game::drawCarStats(const CarSpec& s, Rectangle r) {
    CarRatings rt = ratings(s);
    const char* names[6] = {"Скорость", "Управляемость", "Разгон", "Старт", "Торможение", "Бездорожье"};
    float vals[6] = {rt.speed, rt.handling, rt.accel, rt.launch, rt.braking, rt.offroad};
    float rowH = std::min(30.0f, r.height / 7.0f);
    for (int i = 0; i < 6; i++) {
        float y = r.y + i * rowH;
        ui.text(names[i], r.x, y, 18, pal::muted);
        char buf[16];
        std::snprintf(buf, sizeof buf, "%.1f", vals[i]);
        ui.text(buf, r.x + r.width, y, 18, WHITE, F_BOLD, 2);
        Rectangle bar{r.x + 140, y + 7, r.width - 190, 8};
        ui.rrect(bar, 0.5f, alpha(WHITE, 0.12f));
        ui.rrect({bar.x, bar.y, bar.width * vals[i] / 10.0f, bar.height}, 0.5f, pal::orange);
    }
    char buf[128];
    std::snprintf(buf, sizeof buf, "%.0f л.с. · %.0f Н·м · %.0f кг · до %.0f км/ч", s.powerKW * 1.36f, s.torqueNm, s.mass, s.topSpeedKmh);
    ui.text(buf, r.x, r.y + rowH * 6 + 6, 18, WHITE);
}

// ------------------------------------------------------------------ HUD
void Game::drawSpeedo() {
    float R = 88;
    Vector2 c{ui.W - 130, ui.H - 124};
    ui.circle(c, R + 10, alpha(pal::dark, 0.62f));
    const CarSpec& S = *player.spec;
    float rpmK = clamp01((player.rpm - 0) / (S.redline * 1.05f));
    float a0 = 135, sweep = 270;
    ui.ring(c, R - 8, R, a0, a0 + sweep, alpha(WHITE, 0.12f));
    float red = S.redline * 0.92f / (S.redline * 1.05f);
    ui.ring(c, R - 8, R, a0 + sweep * red, a0 + sweep, alpha(pal::red, 0.45f));
    Color fill = rpmK > red ? pal::red : pal::orange;
    ui.ring(c, R - 8, R, a0, a0 + sweep * rpmK, fill);
    // деления
    int marks = (int)(S.redline / 1000) + 1;
    for (int i = 0; i <= marks; i++) {
        float k = i * 1000.0f / (S.redline * 1.05f);
        if (k > 1) break;
        float a = (a0 + sweep * k) * DEG;
        Vector2 p0{c.x + std::cos(a) * (R - 12), c.y + std::sin(a) * (R - 12)}, p1{c.x + std::cos(a) * (R - 20), c.y + std::sin(a) * (R - 20)};
        ui.line(p0, p1, 2, alpha(WHITE, 0.6f));
    }
    int kmh = (int)std::round(player.kmh());
    ui.text(std::to_string(kmh), c.x, c.y - 34, 56, WHITE, F_DISPLAY, 1);
    ui.text("км/ч", c.x, c.y + 24, 18, pal::muted, F_BOLD, 1);
    std::string g = player.gear < 0 ? "R" : player.gear == 0 ? "N" : std::to_string(player.gear);
    Color gc = (!S.electric && player.rpm > S.redline * 0.9f) ? pal::red : WHITE;
    ui.rrect({c.x - 20, c.y + 46, 40, 36}, 0.25f, alpha(WHITE, 0.1f));
    ui.text(g, c.x, c.y + 49, 30, gc, F_DISPLAY, 1);
    if (!prof.settings.autoGear) ui.text("РУЧН.", c.x + 32, c.y + 58, 14, pal::muted, F_BOLD);
    if (player.tcsCut < 0.9f && prof.settings.tcs) ui.text("ТКС", c.x - 64, c.y + 58, 15, pal::yellow, F_BOLD);
}

void Game::drawMinimap(Rectangle r, float radiusM) {
    Vector2 c{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    float rp = r.width * 0.5f;
    V3 cf = rig.target() - rig.pos();
    if (len2(V3{cf.x, 0, cf.z}) < 1e-4f) cf = player.fwd();
    float th = std::atan2(cf.x, -cf.z);
    float u = (player.pos.x - World::ORIGIN) / 3072.0f, v = (player.pos.z - World::ORIGIN) / 3072.0f;
    float scale = radiusM / 3072.0f;
    Shader sh = rend.sh.minimap;
    float cen[2] = {u, v};
    SetShaderValue(sh, shaderLoc(sh, "uCenter"), cen, SHADER_UNIFORM_VEC2);
    SetShaderValue(sh, shaderLoc(sh, "uRot"), &th, SHADER_UNIFORM_FLOAT);
    SetShaderValue(sh, shaderLoc(sh, "uScale"), &scale, SHADER_UNIFORM_FLOAT);
    ui.circle(c, rp + 5, alpha(pal::dark, 0.7f));
    BeginShaderMode(sh);
    DrawTexturePro(mapTex, {0, 0, (float)mapTex.width, (float)mapTex.height}, ui.px(r), {0, 0}, 0, WHITE);
    EndShaderMode();
    float cs = std::cos(th), sn = std::sin(th);
    auto toMini = [&](V3 P, bool clampEdge, bool& inside) {
        float dx = (P.x - player.pos.x) / 3072.0f / scale, dz = (P.z - player.pos.z) / 3072.0f / scale;
        float qx = dx * cs + dz * sn, qy = -dx * sn + dz * cs;
        float l = std::sqrt(qx * qx + qy * qy);
        inside = l <= 0.94f;
        if (!inside && clampEdge) { qx *= 0.94f / l; qy *= 0.94f / l; }
        return Vector2{c.x + qx * rp, c.y + qy * rp};
    };
    bool in;
    // маршрут гонки
    if (state == GS_RACE && race.ev) {
        const Route& R = race.ev->route;
        float s0 = race.playerTrack.localS();
        Vector2 prev{};
        bool prevIn = false;
        for (float d = -80; d < radiusM * 2.2f; d += 12) {
            float s = s0 + d;
            if (!R.loop && (s < 0 || s > R.length)) { prevIn = false; continue; }
            Vector2 p = toMini(R.at(s).p, false, in);
            if (in && prevIn) ui.line(prev, p, 4.5f, alpha(pal::orange, 0.95f));
            prev = p;
            prevIn = in;
        }
        for (auto& rc : race.racers) {
            Vector2 p = toMini(rc.car.pos, true, in);
            ui.circle(p, 4.5f, in ? pal::red : alpha(pal::red, 0.6f));
        }
        if (race.gatesPassed < (int)race.gates.size()) {
            Vector2 p = toMini(race.nextGatePos(), true, in);
            ui.circle(p, 6, pal::yellow);
        }
    } else {
        for (size_t i = 0; i < fest.events.size(); i++) {
            const EventDef& e = fest.events[i];
            Vector2 p = toMini(e.marker, false, in);
            if (!in) continue;
            ui.circle(p, 7, pal::dark);
            ui.circle(p, 5.5f, eventColor(e.type));
        }
        for (const StuntDef& st : fest.stunts) {
            Vector2 p = toMini(st.pos, false, in);
            if (in) ui.star(p, 5, prof.stuntStars.count(st.id) && prof.stuntStars[st.id] >= 3 ? pal::yellow : alpha(WHITE, 0.8f), true);
        }
        for (auto& t : traffic) {
            Vector2 p = toMini(t.car.pos, false, in);
            if (in) ui.circle(p, 3, alpha(WHITE, 0.85f));
        }
        if (hasWaypoint) {
            Vector2 p = toMini(waypoint, true, in);
            ui.circle(p, 7, pal::dark);
            ui.circle(p, 5, pal::yellow);
        }
    }
    // север
    {
        bool dummy;
        V3 north = player.pos + V3{0, 0, -radiusM * 5};
        Vector2 p = toMini(north, true, dummy);
        ui.circle(p, 10, alpha(pal::dark, 0.9f));
        ui.text("С", p.x, p.y - 9, 16, WHITE, F_BOLD, 1);
    }
    // стрелка игрока
    float pyaw = player.yaw();
    float ang = std::atan2(std::sin(pyaw), std::cos(pyaw));
    (void)ang;
    V3 pf = player.fwd();
    float fx = pf.x * cs + pf.z * sn, fy = -pf.x * sn + pf.z * cs;
    float fl = std::sqrt(fx * fx + fy * fy) + 1e-5f;
    fx /= fl;
    fy /= fl;
    Vector2 tip{c.x + fx * 11, c.y + fy * 11}, l{c.x - fx * 7 + fy * 7, c.y - fy * 7 - fx * 7}, rr{c.x - fx * 7 - fy * 7, c.y - fy * 7 + fx * 7};
    ui.tri(tip, l, c, WHITE);
    ui.tri(tip, c, rr, WHITE);
    ui.ring(c, rp, rp + 3, 0, 360, alpha(WHITE, 0.35f));
}

void Game::drawSkills() {
    float cx = ui.W * 0.5f, y = 150;
    if (skills.active || skills.chainPts > 0) {
        int pts = (int)((skills.chainPts + skills.currentPts) * skills.mult);
        ui.text(std::to_string(pts), cx, y, 46, WHITE, F_DISPLAY, 1, true);
        std::string m = "×" + std::to_string(skills.mult);
        ui.text(m, cx + ui.measure(std::to_string(pts), 46, F_DISPLAY).x * 0.5f + 12, y + 8, 30, pal::pink, F_DISPLAY, 0, true);
        float tk = clamp01(skills.timer / 3.2f);
        ui.rrect({cx - 90, y + 56, 180, 5}, 0.5f, alpha(WHITE, 0.2f));
        ui.rrect({cx - 90, y + 56, 180 * tk, 5}, 0.5f, pal::pink);
        if (!skills.current.empty()) ui.text(skills.current + "  " + std::to_string((int)skills.currentPts), cx, y + 68, 22, pal::yellow, F_BOLD, 1, true);
    }
    float py = y + 98;
    for (auto it = skills.pops.rbegin(); it != skills.pops.rend(); ++it) {
        float a = clamp01(it->t * 2.0f);
        ui.text(it->name + "  +" + std::to_string(it->pts), cx, py, 20, alpha(WHITE, a), F_BOLD, 1, true);
        py += 24;
        if (py > y + 200) break;
    }
    if (skills.lostT > 0) ui.text(skills.lostMsg, cx, y - 30, 22, alpha(pal::red, clamp01(skills.lostT)), F_BOLD, 1, true);
}

void Game::drawHud(float dt) {
    (void)dt;
    // уровень, опыт, кредиты
    float x = 26, y = 22;
    ui.rrect({x, y, 300, 62}, 0.2f, alpha(pal::panel, 0.85f));
    ui.circle({x + 31, y + 31}, 22, pal::orange);
    ui.text(std::to_string(prof.level), x + 31, y + 17, 26, WHITE, F_DISPLAY, 1);
    float need = (float)Profile::xpForLevel(prof.level);
    ui.text("Уровень " + std::to_string(prof.level), x + 64, y + 8, 18, WHITE, F_BOLD);
    ui.rrect({x + 64, y + 32, 220, 6}, 0.5f, alpha(WHITE, 0.15f));
    ui.rrect({x + 64, y + 32, 220 * clamp01(prof.xp / need), 6}, 0.5f, pal::orange);
    ui.text(fmtCr(prof.credits), x + 64, y + 40, 16, pal::muted, F_BOLD);
    if (prof.wheelspins > 0) ui.text("Вращений: " + std::to_string(prof.wheelspins), x + 284, y + 40, 16, pal::yellow, F_BOLD, 2);
    // часы
    int hh = (int)prof.timeOfDay, mm = (int)((prof.timeOfDay - hh) * 60);
    char clock[16];
    std::snprintf(clock, sizeof clock, "%02d:%02d", hh, mm);
    ui.text(clock, ui.W - 28, 24, 26, WHITE, F_DISPLAY, 2, true);
    drawMinimap({30, ui.H - 230, 200, 200}, 330);
    drawSpeedo();
    drawSkills();
    // статус трюка
    if (!stunts.status.empty()) {
        float w = ui.measure(stunts.status, 24, F_BOLD).x + 40;
        ui.rrect({ui.W * 0.5f - w / 2, 96, w, 40}, 0.3f, alpha(pal::panel, 0.85f));
        ui.text(stunts.status, ui.W * 0.5f, 104, 24, pal::yellow, F_BOLD, 1);
    }
    // событие рядом
    if (nearEvent >= 0) {
        const EventDef& e = fest.events[nearEvent];
        Rectangle r{ui.W * 0.5f - 260, ui.H - 170, 520, 120};
        ui.rrect(r, 0.12f, alpha(pal::panel, 0.92f));
        ui.rect({r.x, r.y + 12, 6, r.height - 24}, eventColor(e.type));
        ui.text(eventTypeName(e.type), r.x + 26, r.y + 14, 18, eventColor(e.type), F_BOLD);
        ui.text(e.name, r.x + 26, r.y + 36, 32, WHITE, F_DISPLAY);
        std::string info = e.circuit ? "Кругов: " + std::to_string(e.laps) : "Спринт";
        char buf[64];
        std::snprintf(buf, sizeof buf, " · %.1f км", e.route.length * (e.circuit ? e.laps : 1) / 1000.0f);
        info += buf;
        if (prof.eventPlace.count(e.id)) info += std::string(" · лучший результат: ") + placeWord(prof.eventPlace[e.id]) + " место";
        ui.text(info, r.x + 26, r.y + 80, 18, pal::muted);
        ui.keycap("Enter", r.x + r.width - 150, r.y + 40, 34);
        ui.text("начать", r.x + r.width - 72, r.y + 46, 20, WHITE, F_BOLD);
    }
    // подсказки
    if (stateT < 25 || showHelp) {
        float hy = ui.H - 262;
        ui.text("Esc — меню · M — карта · R — перемотка · C — камера · T — на дорогу · F1 — помощь", 30, hy, 16, alpha(WHITE, 0.75f), F_REG, 0, true);
    }
}

void Game::drawRaceHud(float dt) {
    (void)dt;
    if (!race.ev) return;
    const EventDef& e = *race.ev;
    // позиция и время
    int pos = race.playerFinished ? race.playerPlace : race.position();
    int total = (int)race.racers.size() + 1 + (e.type == EV_SHOWCASE ? 1 : 0);
    float x = 30, y = 24;
    ui.text(std::to_string(pos), x, y - 6, 78, WHITE, F_DISPLAY, 0, true);
    float pw = ui.measure(std::to_string(pos), 78, F_DISPLAY).x;
    ui.text("/" + std::to_string(total), x + pw + 6, y + 30, 30, pal::muted, F_DISPLAY, 0, true);
    ui.text("ПОЗИЦИЯ", x + 2, y + 84, 16, pal::orange, F_BOLD);
    float tx = x + 170;
    if (e.circuit) {
        ui.text("КРУГ", tx, y + 4, 16, pal::orange, F_BOLD);
        ui.text(std::to_string(race.lap) + "/" + std::to_string(e.laps), tx, y + 20, 36, WHITE, F_DISPLAY, 0, true);
        tx += 110;
    }
    ui.text("ВРЕМЯ", tx, y + 4, 16, pal::orange, F_BOLD);
    ui.text(fmtTime(race.playerFinished ? race.playerFinishT : std::max(0.0f, race.t)), tx, y + 20, 36, WHITE, F_DISPLAY, 0, true);
    if (e.circuit && race.bestLap > 0) ui.text("Лучший круг " + fmtTime(race.bestLap), tx, y + 62, 18, pal::muted, F_BOLD, 0, true);
    // таблица
    auto st = race.standings();
    float ly = y + 116;
    for (size_t i = 0; i < st.size() && i < 9; i++) {
        int id = st[i];
        std::string name = id == -1 ? "Вы" : id == -2 ? "Самолёт" : race.racers[id].name;
        bool me = id == -1;
        ui.rrect({x, ly, 250, 26}, 0.3f, me ? alpha(pal::orange, 0.85f) : alpha(pal::panel, 0.7f));
        ui.text(std::to_string(i + 1), x + 16, ly + 3, 18, WHITE, F_BOLD, 1);
        ui.text(name, x + 34, ly + 3, 18, WHITE, me ? F_BOLD : F_REG);
        ly += 29;
    }
    // прогресс
    float prog = clamp01(race.playerProgress() / std::max(race.total, 1.0f));
    Rectangle pb{ui.W * 0.5f - 250, 26, 500, 8};
    ui.rrect(pb, 0.5f, alpha(WHITE, 0.18f));
    ui.rrect({pb.x, pb.y, pb.width * prog, pb.height}, 0.5f, pal::orange);
    for (float g : race.gates) {
        float k = g / std::max(race.total, 1.0f);
        ui.rect({pb.x + pb.width * k - 1, pb.y - 3, 2, pb.height + 6}, alpha(WHITE, 0.5f));
    }
    ui.text(e.name, ui.W * 0.5f, 42, 20, WHITE, F_BOLD, 1, true);
    if (e.type == EV_SHOWCASE) {
        float gap = race.planeS - (race.playerTrack.localS());
        char buf[64];
        std::snprintf(buf, sizeof buf, gap > 0 ? "Самолёт впереди на %.0f м" : "Самолёт позади на %.0f м", std::fabs(gap));
        ui.text(buf, ui.W * 0.5f, 68, 20, gap > 0 ? pal::pink : pal::green, F_BOLD, 1, true);
    }
    // обратный отсчёт
    if (race.t < 0) {
        int n = (int)std::ceil(-race.t);
        if (n <= 3) {
            float k = -race.t - (n - 1);
            ui.text(std::to_string(n), ui.W * 0.5f, ui.H * 0.3f, 130 + (1 - k) * 30, alpha(WHITE, 0.4f + 0.6f * k), F_DISPLAY, 1, true);
        } else ui.text("Приготовьтесь", ui.W * 0.5f, ui.H * 0.34f, 44, WHITE, F_DISPLAY, 1, true);
    } else if (race.t < 1.2f) {
        ui.text("СТАРТ!", ui.W * 0.5f, ui.H * 0.3f, 120, alpha(pal::orange, 1.2f - race.t), F_DISPLAY, 1, true);
    }
    if (race.wrongWayT > 1.2f) ui.text("НЕВЕРНОЕ НАПРАВЛЕНИЕ", ui.W * 0.5f, ui.H * 0.24f, 40, pal::red, F_DISPLAY, 1, true);
    if (race.missedGate) ui.text("Пропущен чекпоинт — вернитесь или нажмите T", ui.W * 0.5f, ui.H * 0.24f + 50, 26, pal::yellow, F_BOLD, 1, true);
    if (race.playerFinished) {
        ui.text(race.playerPlace == 1 ? "ПОБЕДА!" : std::string("ФИНИШ · ") + placeWord(race.playerPlace) + " МЕСТО", ui.W * 0.5f, ui.H * 0.28f, 72, pal::orange, F_DISPLAY, 1, true);
    }
    drawMinimap({30, ui.H - 230, 200, 200}, 280);
    drawSpeedo();
    drawSkills();
    if (race.autodrive) ui.text("АВТОПИЛОТ", ui.W * 0.5f, ui.H - 40, 18, pal::cyan, F_BOLD, 1);
}

// ------------------------------------------------------------------ карточка события
void Game::drawEventCard(float dt) {
    (void)dt;
    if (nearEvent < 0) {
        enterState(GS_DRIVE);
        return;
    }
    const EventDef& e = fest.events[nearEvent];
    ui.rect({0, 0, ui.W, ui.H}, alpha(BLACK, 0.35f));
    Rectangle r{ui.W * 0.5f - 380, ui.H * 0.5f - 250, 760, 500};
    ui.rrect(r, 0.05f, pal::panel);
    ui.gradH({r.x, r.y, r.width, 8}, eventColor(e.type), pal::pink);
    ui.text(eventTypeName(e.type), r.x + 36, r.y + 30, 20, eventColor(e.type), F_BOLD);
    ui.text(e.name, r.x + 36, r.y + 56, 46, WHITE, F_DISPLAY);
    ui.textBox(e.desc, {r.x + 36, r.y + 120, r.width - 72, 70}, 20, pal::muted);
    float y = r.y + 200;
    char buf[96];
    std::snprintf(buf, sizeof buf, "%.1f км", e.route.length * (e.circuit ? e.laps : 1) / 1000.0f);
    const char* labels[4] = {"Дистанция", "Формат", "Соперники", "Сложность"};
    static const char* diffs[4] = {"Новичок", "Средний", "Про", "Легенда"};
    std::string vals[4] = {buf, e.circuit ? "Кольцо, кругов: " + std::to_string(e.laps) : "Спринт", e.type == EV_SHOWCASE ? "Самолёт «Чайка»" : std::to_string(e.opponents) + ", класс вашей машины",
                           diffs[std::clamp(prof.settings.difficulty, 0, 3)]};
    for (int i = 0; i < 4; i++) {
        ui.text(labels[i], r.x + 36, y + i * 34, 20, pal::muted);
        ui.text(vals[i], r.x + 220, y + i * 34, 20, WHITE, F_BOLD);
    }
    float rx = r.x + 470;
    ui.text("Награды", rx, y, 20, pal::muted);
    const char* pl[3] = {"1-е", "2-е", "3-е"};
    Color pc[3] = {pal::yellow, Color{210, 210, 220, 255}, Color{220, 150, 90, 255}};
    for (int i = 0; i < 3; i++) {
        ui.text(pl[i], rx, y + 30 + i * 30, 20, pc[i], F_BOLD);
        ui.text(fmtCr(e.reward[i]), rx + 50, y + 30 + i * 30, 20, WHITE, F_BOLD);
    }
    ui.text("+" + std::to_string(e.xp) + " опыта", rx, y + 124, 20, pal::orange, F_BOLD);
    if (prof.eventPlace.count(e.id)) ui.text(std::string("Лучший результат: ") + placeWord(prof.eventPlace[e.id]) + " место", r.x + 36, y + 150, 20, pal::green, F_BOLD);
    ui.text("Ваша машина: " + player.spec->name + " (" + player.spec->className() + " " + std::to_string(player.spec->pi) + ")", r.x + 36, y + 180, 20, WHITE);
    if (ui.nav.left || ui.nav.up) sel = 0;
    if (ui.nav.right || ui.nav.down) sel = 1;
    Rectangle b0{r.x + 36, r.y + r.height - 76, 330, 52}, b1{r.x + r.width - 366, r.y + r.height - 76, 330, 52};
    if (ui.hover(b0) && ui.mouseMoved) sel = 0;
    if (ui.hover(b1) && ui.mouseMoved) sel = 1;
    if (ui.button(b0, "Начать", sel == 0, 26)) {
        audio.play(SFX_UI_OK);
        startEvent(nearEvent);
        return;
    }
    if (ui.button(b1, "Отмена", sel == 1, 24) || ui.nav.back) {
        audio.play(SFX_UI_BACK);
        enterState(GS_DRIVE);
    }
}

// ------------------------------------------------------------------ итоги
void Game::drawResults(float dt) {
    (void)dt;
    const EventDef& e = fest.events[raceEvent];
    ui.gradH({0, 0, ui.W, ui.H}, Color{6, 8, 16, 235}, Color{6, 8, 16, 150});
    ui.text(resultTitle, 70, 50, 84, resultPlace == 1 ? pal::yellow : pal::orange, F_DISPLAY, 0, true);
    ui.text(e.name + " · " + eventTypeName(e.type), 76, 146, 24, WHITE, F_BOLD);
    float y = 200;
    for (size_t i = 0; i < results.size() && i < 10; i++) {
        const RaceResultRow& r = results[i];
        float a = clamp01(stateT * 3 - i * 0.2f);
        Rectangle row{70, y, 640, 36};
        ui.rrect(row, 0.25f, alpha(r.player ? pal::orange : pal::panel2, a * (r.player ? 0.9f : 0.8f)));
        ui.text(std::to_string(i + 1), row.x + 22, row.y + 6, 22, alpha(WHITE, a), F_DISPLAY, 1);
        ui.text(r.name, row.x + 52, row.y + 7, 20, alpha(WHITE, a), r.player ? F_BOLD : F_REG);
        ui.text(r.car, row.x + 300, row.y + 8, 18, alpha(r.player ? WHITE : pal::muted, a));
        ui.text(r.finished ? fmtTime(r.time) : "—", row.x + row.width - 16, row.y + 7, 20, alpha(WHITE, a), F_BOLD, 2);
        y += 41;
    }
    Rectangle rw{ui.W - 420, 200, 350, 240};
    ui.rrect(rw, 0.08f, pal::panel);
    ui.text("НАГРАДЫ", rw.x + 26, rw.y + 20, 22, pal::orange, F_BOLD);
    ui.text(fmtCr(resultReward.credits), rw.x + 26, rw.y + 60, 38, WHITE, F_DISPLAY);
    ui.text("+" + std::to_string(resultReward.xp) + " опыта", rw.x + 26, rw.y + 114, 26, pal::yellow, F_BOLD);
    ui.text(std::string("Место: ") + placeWord(resultPlace), rw.x + 26, rw.y + 156, 22, WHITE);
    if (prof.wheelspins > 0) ui.text("Доступно вращений колеса: " + std::to_string(prof.wheelspins), rw.x + 26, rw.y + 192, 18, pal::pink, F_BOLD);
    if (stateT < 0.6f) return;
    if (ui.nav.left || ui.nav.up) sel = 0;
    if (ui.nav.right || ui.nav.down) sel = 1;
    Rectangle b0{ui.W - 420, ui.H - 150, 350, 50}, b1{ui.W - 420, ui.H - 90, 350, 50};
    if (ui.hover(b0) && ui.mouseMoved) sel = 0;
    if (ui.hover(b1) && ui.mouseMoved) sel = 1;
    if (ui.button(b0, "Продолжить", sel == 0, 24)) {
        audio.play(SFX_UI_OK);
        race = RaceSession{};
        raceEvent = -1;
        rewind.clear();
        rig.reset(player);
        enterState(GS_DRIVE);
        fade = 1;
        return;
    }
    if (ui.button(b1, "Повторить", sel == 1, 24)) {
        audio.play(SFX_UI_OK);
        int ev = raceEvent;
        startEvent(ev);
    }
}

// ------------------------------------------------------------------ пауза
void Game::drawPause(float dt) {
    (void)dt;
    std::vector<std::pair<int, const char*>> tabs;
    if (pauseFrom == GS_DRIVE)
        tabs = {{TAB_MAP, "Карта"}, {TAB_GARAGE, "Гараж"}, {TAB_AUTOSHOW, "Автосалон"}, {TAB_WHEELSPIN, "Колесо удачи"}, {TAB_PROGRESS, "Прогресс"}, {TAB_SETTINGS, "Настройки"}, {TAB_CONTROLS, "Управление"}};
    else if (pauseFrom == GS_RACE)
        tabs = {{TAB_RACEMENU, "Событие"}, {TAB_SETTINGS, "Настройки"}, {TAB_CONTROLS, "Управление"}};
    else
        tabs = {{TAB_SETTINGS, "Настройки"}, {TAB_CONTROLS, "Управление"}};
    int ti = 0;
    for (size_t i = 0; i < tabs.size(); i++)
        if (tabs[i].first == tab) ti = (int)i;
    if (tabs[ti].first != tab) tab = tabs[0].first;
    bool mapFull = tab == TAB_MAP;
    bool showroom = tab == TAB_GARAGE || tab == TAB_AUTOSHOW;
    if (!showroom) ui.rect({0, 0, ui.W, ui.H}, alpha(Color{6, 8, 16, 255}, mapFull ? 0.92f : 0.78f));
    else ui.gradH({0, 0, ui.W, ui.H}, Color{6, 8, 16, 225}, Color{6, 8, 16, 40});
    // вкладки
    if (ui.nav.tabL) { ti = (ti + (int)tabs.size() - 1) % (int)tabs.size(); tab = tabs[ti].first; tabSel = tabSel2 = 0; confirmReset = false; audio.play(SFX_UI_MOVE); }
    if (ui.nav.tabR) { ti = (ti + 1) % (int)tabs.size(); tab = tabs[ti].first; tabSel = tabSel2 = 0; confirmReset = false; audio.play(SFX_UI_MOVE); }
    float tx = 60;
    ui.text("Q", tx - 34, 30, 18, pal::muted, F_BOLD);
    for (size_t i = 0; i < tabs.size(); i++) {
        float w = ui.measure(tabs[i].second, 22, F_BOLD).x + 34;
        Rectangle r{tx, 22, w, 38};
        bool on = (int)i == ti;
        if (ui.hover(r) && ui.click) { tab = tabs[i].first; ti = (int)i; tabSel = tabSel2 = 0; audio.play(SFX_UI_MOVE); }
        ui.rrect(r, 0.3f, on ? pal::orange : alpha(pal::panel2, 0.9f));
        ui.text(tabs[i].second, r.x + w * 0.5f, r.y + 8, 22, WHITE, F_BOLD, 1);
        tx += w + 8;
    }
    ui.text("E", tx + 8, 30, 18, pal::muted, F_BOLD);
    ui.text(fmtCr(prof.credits) + "   ·   Уровень " + std::to_string(prof.level), ui.W - 40, 30, 20, WHITE, F_BOLD, 2);
    Rectangle area{60, 86, ui.W - 120, ui.H - 150};
    switch (tab) {
        case TAB_MAP: drawMapTab(area, true); break;
        case TAB_GARAGE: drawGarageTab(area); break;
        case TAB_AUTOSHOW: drawAutoshowTab(area); break;
        case TAB_WHEELSPIN: drawSpinTab(area); break;
        case TAB_PROGRESS: drawProgressTab(area); break;
        case TAB_SETTINGS: drawSettingsTab(area); break;
        case TAB_CONTROLS: drawControlsTab(area); break;
        case TAB_RACEMENU: {
            const char* items[3] = {"Продолжить", "Начать заново", "Покинуть событие"};
            if (ui.nav.up) tabSel = (tabSel + 2) % 3;
            if (ui.nav.down) tabSel = (tabSel + 1) % 3;
            for (int i = 0; i < 3; i++) {
                Rectangle r{area.x, area.y + 20 + i * 62.0f, 380, 50};
                if (ui.hover(r) && ui.mouseMoved) tabSel = i;
                if (ui.button(r, items[i], tabSel == i, 24)) {
                    audio.play(SFX_UI_OK);
                    if (i == 0) { state = GS_RACE; return; }
                    if (i == 1) { startEvent(raceEvent); return; }
                    if (i == 2) { endRace(true); return; }
                }
            }
            if (race.ev) {
                ui.text(race.ev->name, area.x + 440, area.y + 24, 34, WHITE, F_DISPLAY);
                ui.textBox(race.ev->desc, {area.x + 440, area.y + 76, 560, 120}, 20, pal::muted);
            }
            break;
        }
        default: break;
    }
    // подвал
    ui.text("Q/E — вкладки · стрелки — выбор · Enter — действие · Esc — вернуться", 60, ui.H - 44, 17, pal::muted);
    if (ui.nav.back && !confirmReset) {
        audio.play(SFX_UI_BACK);
        save();
        if (pauseFrom == GS_TITLE) enterState(GS_TITLE);
        else {
            state = pauseFrom;
            stateT = 1;
            rig.reset(player);
        }
    }
}

void Game::drawMapTab(Rectangle area, bool full) {
    (void)full;
    // список точек: события, места
    struct Poi { std::string name, sub; V3 p; Color c; int ev = -1; float yaw = 0; };
    std::vector<Poi> pois;
    for (size_t i = 0; i < fest.events.size(); i++) {
        const EventDef& e = fest.events[i];
        std::string sub = eventTypeName(e.type);
        if (prof.eventPlace.count(e.id)) sub += std::string(" · лучшее: ") + placeWord(prof.eventPlace[e.id]);
        pois.push_back({e.name, sub, e.marker, eventColor(e.type), (int)i, e.markerYaw});
    }
    for (const Place& p : world.places) {
        if (p.name.empty()) continue;
        pois.push_back({p.name, "Место", p.p, WHITE, -1, p.yaw});
    }
    int n = (int)pois.size();
    if (ui.nav.left || ui.nav.up) { mapSel = (mapSel + n - 1) % n; audio.play(SFX_UI_MOVE); }
    if (ui.nav.right || ui.nav.down) { mapSel = (mapSel + 1) % n; audio.play(SFX_UI_MOVE); }
    mapSel = std::clamp(mapSel, 0, n - 1);
    float side = std::min(area.height, area.width - 380);
    Rectangle mr{area.x, area.y, side, side};
    DrawTexturePro(mapTex, {0, 0, (float)mapTex.width, (float)mapTex.height}, ui.px(mr), {0, 0}, 0, WHITE);
    ui.rrectLines(mr, 0.01f, 2, alpha(WHITE, 0.3f));
    auto toMap = [&](V3 p) { return Vector2{mr.x + (p.x - World::ORIGIN) / 3072.0f * side, mr.y + (p.z - World::ORIGIN) / 3072.0f * side}; };
    // трюки
    for (const StuntDef& st : fest.stunts) {
        int stars = prof.stuntStars.count(st.id) ? prof.stuntStars[st.id] : 0;
        ui.star(toMap(st.pos), 6, stars >= 3 ? pal::yellow : stars > 0 ? alpha(pal::yellow, 0.7f) : alpha(WHITE, 0.75f), true);
    }
    for (int i = 0; i < n; i++) {
        Vector2 q = toMap(pois[i].p);
        bool s = i == mapSel;
        float r = s ? 11 : (pois[i].ev >= 0 ? 7 : 5);
        ui.circle(q, r + 2, pal::dark);
        ui.circle(q, r, pois[i].c);
        if (ui.click && std::hypot(ui.mouse.x - q.x, ui.mouse.y - q.y) < 12) mapSel = i;
        if (s) ui.ring(q, r + 4, r + 7, 0, 360, WHITE);
    }
    if (hasWaypoint) ui.star(toMap(waypoint), 9, pal::yellow, true);
    // игрок
    {
        Vector2 q = toMap(player.pos);
        V3 f = player.fwd();
        float l = std::sqrt(f.x * f.x + f.z * f.z) + 1e-5f;
        float fx = f.x / l, fy = f.z / l;
        Vector2 tip{q.x + fx * 12, q.y + fy * 12}, a{q.x - fx * 8 + fy * 8, q.y - fy * 8 - fx * 8}, b{q.x - fx * 8 - fy * 8, q.y - fy * 8 + fx * 8};
        ui.tri(tip, a, q, WHITE);
        ui.tri(tip, q, b, WHITE);
    }
    // информация справа
    Rectangle info{mr.x + side + 30, area.y, area.width - side - 30, 300};
    ui.rrect(info, 0.06f, pal::panel);
    const Poi& p = pois[mapSel];
    ui.text(p.sub, info.x + 24, info.y + 20, 18, p.c, F_BOLD);
    ui.textBox(p.name, {info.x + 24, info.y + 46, info.width - 48, 80}, 30, WHITE, F_DISPLAY);
    char buf[64];
    std::snprintf(buf, sizeof buf, "Расстояние: %.1f км", len(p.p - player.pos) / 1000.0f);
    ui.text(buf, info.x + 24, info.y + 130, 20, pal::muted);
    Rectangle bt{info.x + 24, info.y + 180, info.width - 48, 46}, bw{info.x + 24, info.y + 236, info.width - 48, 46};
    bool travel = ui.button(bt, "Быстрое перемещение", true, 22);
    bool mark = ui.button(bw, hasWaypoint && len(waypoint - p.p) < 1 ? "Снять метку" : "Поставить метку", false, 20) || ui.pressed(KEY_SPACE);
    if (mark && !travel) {
        if (hasWaypoint && len(waypoint - p.p) < 1) hasWaypoint = false;
        else { waypoint = p.p; hasWaypoint = true; }
        audio.play(SFX_UI_MOVE);
    }
    if (travel) {
        audio.play(SFX_WHOOSH);
        V3 target = p.p;
        float yaw = p.yaw;
        if (p.ev >= 0) {
            // становимся чуть позади маркера события
            V3 back{std::sin(yaw), 0, std::cos(yaw)};
            target = target - back * 12.0f;
        }
        clearTraffic();
        teleportPlayer(target, yaw);
        spawnTraffic();
        state = GS_DRIVE;
        stateT = 1;
        fade = 1;
    }
    ui.text("Список: стрелки · Enter — перемещение · Пробел — метка", info.x, info.y + info.height + 16, 16, pal::muted);
    // легенда
    float ly = info.y + info.height + 50;
    const char* names[5] = {"Шоссе", "Грунт", "Кросс-кантри", "Дрэг", "Шоу"};
    EventType ts[5] = {EV_ROAD, EV_DIRT, EV_CROSS, EV_DRAG, EV_SHOWCASE};
    for (int i = 0; i < 5; i++) {
        ui.circle({info.x + 10, ly + 10 + i * 26}, 6, eventColor(ts[i]));
        ui.text(names[i], info.x + 26, ly + i * 26, 18, WHITE);
    }
    ui.star({info.x + 10, ly + 10 + 5 * 26}, 6, pal::yellow, true);
    ui.text("Трюки: радары, зоны, прыжки", info.x + 26, ly + 5 * 26, 18, WHITE);
}

void Game::drawGarageTab(Rectangle area) {
    if (prof.garage.empty()) return;
    int n = (int)prof.garage.size();
    if (ui.nav.up) { tabSel = (tabSel + n - 1) % n; audio.play(SFX_UI_MOVE); }
    if (ui.nav.down) { tabSel = (tabSel + 1) % n; audio.play(SFX_UI_MOVE); }
    tabSel = std::clamp(tabSel, 0, n - 1);
    static const uint32_t palette[16] = {0xff3050e0, 0xff2030c0, 0xff20a0ff, 0xff30d0ff, 0xff40c060, 0xff307030, 0xffe0a030, 0xffc06020,
                                         0xffd050b0, 0xff7030a0, 0xfff4f4f4, 0xffa0a0a0, 0xff505050, 0xff181818, 0xff70b0d0, 0xff3080c0};
    OwnedCar& oc = prof.garage[tabSel];
    if (ui.nav.left || ui.nav.right) {
        int ci = 0;
        for (int i = 0; i < 16; i++)
            if (palette[i] == oc.color) ci = i;
        ci = (ci + (ui.nav.right ? 1 : 15)) % 16;
        oc.color = palette[ci];
        if (tabSel == prof.current) player.color = oc.color;
        audio.play(SFX_UI_MOVE);
    }
    float y = area.y;
    ui.text("ВАШИ МАШИНЫ", area.x, y, 22, pal::orange, F_BOLD);
    y += 36;
    int first = std::max(0, tabSel - 8);
    for (int i = first; i < n && i < first + 10; i++) {
        const CarSpec* s = findCar(prof.garage[i].id);
        if (!s) continue;
        Rectangle r{area.x, y, 380, 40};
        if (ui.hover(r) && ui.click) tabSel = i;
        ui.rrect(r, 0.25f, i == tabSel ? pal::orange : alpha(pal::panel2, 0.9f));
        ui.text(s->name, r.x + 16, r.y + 9, 20, WHITE, F_BOLD);
        piBadge(ui, r.x + r.width - 76, r.y + 8, s->pi, 24);
        if (i == prof.current) ui.circle({r.x + r.width - 92, r.y + 20}, 5, pal::green);
        y += 46;
    }
    const CarSpec* s = findCar(oc.id);
    if (!s) return;
    Rectangle card{ui.W - 480, area.y, 420, 470};
    ui.rrect(card, 0.05f, pal::panel);
    ui.text(s->maker + " · " + std::to_string(s->year), card.x + 24, card.y + 18, 18, pal::muted, F_BOLD);
    ui.text(s->name, card.x + 24, card.y + 40, 32, WHITE, F_DISPLAY);
    piBadge(ui, card.x + 24, card.y + 86, s->pi);
    ui.text(driveName(s->drive), card.x + 110, card.y + 90, 18, pal::muted);
    drawCarStats(*s, {card.x + 24, card.y + 130, card.width - 48, 200});
    ui.text("Цвет — клавиши A / D или мышь", card.x + 24, card.y + 344, 18, pal::muted);
    for (int i = 0; i < 16; i++) {
        Rectangle sw{card.x + 24 + (i % 8) * 46.0f, card.y + 370 + (i / 8) * 34.0f, 38, 26};
        uint32_t c = palette[i];
        Color col{(unsigned char)(c & 255), (unsigned char)((c >> 8) & 255), (unsigned char)((c >> 16) & 255), 255};
        ui.rrect(sw, 0.3f, col);
        if (c == oc.color) ui.rrectLines({sw.x - 2, sw.y - 2, sw.width + 4, sw.height + 4}, 0.3f, 2, WHITE);
        if (ui.hover(sw) && ui.click) {
            oc.color = c;
            if (tabSel == prof.current) player.color = c;
        }
    }
    Rectangle drive{area.x, area.y + area.height - 56, 380, 50};
    if (ui.button(drive, tabSel == prof.current ? "Продолжить на этой машине" : "Сесть за руль", true, 22)) {
        audio.play(SFX_UI_OK);
        prof.current = tabSel;
        setPlayerCar(oc.id, oc.color, true);
        save();
        state = GS_DRIVE;
        stateT = 1;
        fade = 1;
    }
}

void Game::drawAutoshowTab(Rectangle area) {
    const auto& cat = carCatalog();
    int n = (int)cat.size();
    if (ui.nav.up) { tabSel = (tabSel + n - 1) % n; audio.play(SFX_UI_MOVE); }
    if (ui.nav.down) { tabSel = (tabSel + 1) % n; audio.play(SFX_UI_MOVE); }
    tabSel = std::clamp(tabSel, 0, n - 1);
    float y = area.y;
    ui.text("АВТОСАЛОН ФЕСТИВАЛЯ", area.x, y, 22, pal::orange, F_BOLD);
    y += 36;
    int first = std::clamp(tabSel - 6, 0, std::max(0, n - 10));
    for (int i = first; i < n && i < first + 10; i++) {
        const CarSpec& s = cat[i];
        Rectangle r{area.x, y, 420, 40};
        if (ui.hover(r) && ui.click) tabSel = i;
        ui.rrect(r, 0.25f, i == tabSel ? pal::orange : alpha(pal::panel2, 0.9f));
        ui.text(s.name, r.x + 16, r.y + 9, 20, WHITE, F_BOLD);
        piBadge(ui, r.x + r.width - 76, r.y + 8, s.pi, 24);
        ui.text(prof.owns(s.id) ? "есть" : fmtCr(s.price), r.x + r.width - 92, r.y + 10, 17, prof.owns(s.id) ? pal::green : pal::muted, F_BOLD, 2);
        y += 46;
    }
    const CarSpec& s = cat[tabSel];
    Rectangle card{ui.W - 480, area.y, 420, 470};
    ui.rrect(card, 0.05f, pal::panel);
    ui.text(s.maker + " · " + std::to_string(s.year), card.x + 24, card.y + 18, 18, pal::muted, F_BOLD);
    ui.text(s.name, card.x + 24, card.y + 40, 32, WHITE, F_DISPLAY);
    piBadge(ui, card.x + 24, card.y + 86, s.pi);
    ui.text(driveName(s.drive), card.x + 110, card.y + 90, 18, pal::muted);
    drawCarStats(s, {card.x + 24, card.y + 130, card.width - 48, 200});
    ui.text(fmtCr(s.price), card.x + 24, card.y + 350, 34, WHITE, F_DISPLAY);
    bool owned = prof.owns(s.id);
    bool can = !owned && prof.credits >= s.price;
    Rectangle buy{card.x + 24, card.y + 404, card.width - 48, 48};
    if (ui.button(buy, owned ? "Уже в гараже" : can ? "Купить" : "Недостаточно кредитов", can, 22, can)) {
        prof.credits -= s.price;
        prof.garage.push_back({s.id, s.color});
        audio.play(SFX_REWARD);
        ui.toast("Куплено: " + s.name, "Машина ждёт в гараже", pal::green, 3.0f);
        save();
    }
}

void Game::drawSpinTab(Rectangle area) {
    ui.text("КОЛЕСО УДАЧИ", area.x, area.y, 44, WHITE, F_DISPLAY);
    ui.textBox("Вращения дают кредиты или новые машины. Их приносят новые уровни и первые победы в событиях.", {area.x, area.y + 64, 620, 80}, 22, pal::muted);
    ui.text("Доступно вращений: " + std::to_string(prof.wheelspins), area.x, area.y + 150, 30, prof.wheelspins > 0 ? pal::yellow : pal::muted, F_BOLD);
    Rectangle b{area.x, area.y + 210, 380, 54};
    if (ui.button(b, "Крутить колесо", true, 26, prof.wheelspins > 0)) {
        audio.play(SFX_UI_OK);
        prepareSpin();
        state = GS_WHEELSPIN;
        stateT = 0;
    }
}

void Game::drawProgressTab(Rectangle area) {
    float x = area.x, y = area.y;
    ui.text("ПРОГРЕСС", x, y, 22, pal::orange, F_BOLD);
    int evDone = prof.eventsDone(), stars = prof.totalStars();
    char buf[128];
    std::string lines[7];
    lines[0] = "Уровень: " + std::to_string(prof.level) + " (" + std::to_string(prof.xp) + " / " + std::to_string(Profile::xpForLevel(prof.level)) + " опыта)";
    lines[1] = "Кредиты: " + fmtCr(prof.credits);
    lines[2] = "События пройдены: " + std::to_string(evDone) + " из " + std::to_string(fest.events.size());
    lines[3] = "Звёзды трюков: " + std::to_string(stars) + " из " + std::to_string(fest.stunts.size() * 3);
    lines[4] = "Доски опыта: " + std::to_string(prof.boards.size()) + " из " + std::to_string(world.boards.size());
    lines[5] = "Лучшая цепочка навыков: " + std::to_string(prof.bestChain);
    std::snprintf(buf, sizeof buf, "Пройдено: %.1f км · машин в гараже: %d", prof.distanceKm, (int)prof.garage.size());
    lines[6] = buf;
    for (int i = 0; i < 7; i++) ui.text(lines[i], x, y + 40 + i * 32, 21, WHITE);
    // события
    float ex = x + 560;
    ui.text("СОБЫТИЯ", ex, y, 22, pal::orange, F_BOLD);
    float ey = y + 36;
    for (const EventDef& e : fest.events) {
        int pl = prof.eventPlace.count(e.id) ? prof.eventPlace[e.id] : 0;
        ui.circle({ex + 8, ey + 11}, 6, eventColor(e.type));
        ui.text(e.name, ex + 24, ey, 18, WHITE);
        ui.text(pl ? std::string(placeWord(pl)) + " место" : "—", ex + 330, ey, 18, pl == 1 ? pal::yellow : pal::muted, F_BOLD);
        ey += 25;
    }
    // трюки
    float sx = x, sy = y + 290;
    ui.text("ТРЮКИ", sx, sy, 22, pal::orange, F_BOLD);
    sy += 34;
    int col = 0;
    for (const StuntDef& st : fest.stunts) {
        int s = prof.stuntStars.count(st.id) ? prof.stuntStars[st.id] : 0;
        float cx = sx + (col % 2) * 270, cy = sy + (col / 2) * 24;
        ui.text(st.name, cx, cy, 16, WHITE);
        for (int k = 0; k < 3; k++) ui.star({cx + 200 + k * 16.0f, cy + 9}, 6, k < s ? pal::yellow : alpha(WHITE, 0.25f), true);
        col++;
    }
}

void Game::drawSettingsTab(Rectangle area) {
    Settings& s = prof.settings;
    static const char* qual[3] = {"Низкое", "Среднее", "Высокое"};
    static const char* lineM[3] = {"Выкл", "Торможение", "Полная"};
    static const char* diffs[4] = {"Новичок", "Средний", "Про", "Легенда"};
    static const float tspeeds[5] = {0.0f, 0.5f, 1.0f, 2.0f, 4.0f};
    static const char* tsn[5] = {"Стоп", "×0,5", "×1", "×2", "×4"};
    char buf[64];
    int tsi = 2;
    for (int i = 0; i < 5; i++)
        if (std::fabs(s.timeSpeed - tspeeds[i]) < 0.01f) tsi = i;
    int hh = (int)prof.timeOfDay, mm = (int)((prof.timeOfDay - hh) * 60);
    std::snprintf(buf, sizeof buf, "%02d:%02d", hh, mm);
    struct Row { std::string label, value; };
    std::vector<Row> rows = {
        {"Качество графики", qual[std::clamp(s.quality, 0, 2)]},
        {"Тени", offOn(s.shadows)},
        {"АБС", offOn(s.abs)},
        {"Антипробуксовка", offOn(s.tcs)},
        {"Стабилизация", offOn(s.stm)},
        {"Коробка передач", s.autoGear ? "Автомат" : "Ручная"},
        {"Помощь рулению", offOn(s.steerAssist)},
        {"Линия движения", lineM[std::clamp(s.line, 0, 2)]},
        {"Сложность соперников", diffs[std::clamp(s.difficulty, 0, 3)]},
        {"Общая громкость", std::to_string((int)std::round(s.master * 100)) + "%"},
        {"Музыка", std::to_string((int)std::round(s.music * 100)) + "%"},
        {"Звуки", std::to_string((int)std::round(s.sfx * 100)) + "%"},
        {"Фоновая музыка", offOn(s.music_on)},
        {"Чувствительность камеры", std::to_string((int)std::round(s.camSens * 100)) + "%"},
        {"Камера", camModeName(std::clamp(s.camera, 0, CAM_COUNT - 1))},
        {"Скорость смены суток", tsn[tsi]},
        {"Время суток", buf},
    };
    bool canReset = pauseFrom != GS_RACE && prof.started;
    if (canReset) rows.push_back({"Сбросить прогресс", confirmReset ? "Enter — подтвердить" : "…"});
    int n = (int)rows.size();
    if (ui.nav.up) { tabSel = (tabSel + n - 1) % n; confirmReset = false; audio.play(SFX_UI_MOVE); }
    if (ui.nav.down) { tabSel = (tabSel + 1) % n; confirmReset = false; audio.play(SFX_UI_MOVE); }
    tabSel = std::clamp(tabSel, 0, n - 1);
    int perCol = 9;
    for (int i = 0; i < n; i++) {
        float cx = area.x + (i / perCol) * 590.0f, cy = area.y + (i % perCol) * 50.0f;
        Rectangle r{cx, cy, 560, 42};
        if (ui.hover(r) && ui.mouseMoved) tabSel = i;
        int d = ui.optionRow(r, rows[i].label, rows[i].value, tabSel == i);
        bool ok = tabSel == i && ui.nav.ok;
        if (d == 0 && ok) d = 1;
        if (d == 0) continue;
        audio.play(SFX_UI_MOVE);
        auto step = [&](float& v, float st, float lo, float hi) { v = clampf(std::round((v + st * d) * 100) / 100, lo, hi); };
        switch (i) {
            case 0: s.quality = (s.quality + d + 3) % 3; rend.setQuality(s.quality, s.shadows); break;
            case 1: s.shadows = !s.shadows; rend.setQuality(s.quality, s.shadows); if (s.shadows && !rend.shadows) { s.shadows = false; ui.toast("Тени недоступны на этом устройстве", "", pal::red); } break;
            case 2: s.abs = !s.abs; break;
            case 3: s.tcs = !s.tcs; break;
            case 4: s.stm = !s.stm; break;
            case 5: s.autoGear = !s.autoGear; break;
            case 6: s.steerAssist = !s.steerAssist; break;
            case 7: s.line = (s.line + d + 3) % 3; break;
            case 8: s.difficulty = std::clamp(s.difficulty + d, 0, 3); break;
            case 9: step(s.master, 0.1f, 0, 1); break;
            case 10: step(s.music, 0.1f, 0, 1); break;
            case 11: step(s.sfx, 0.1f, 0, 1); break;
            case 12: s.music_on = !s.music_on; break;
            case 13: step(s.camSens, 0.1f, 0.3f, 2.0f); break;
            case 14: s.camera = (s.camera + d + CAM_COUNT) % CAM_COUNT; rig.mode = s.camera; break;
            case 15: tsi = std::clamp(tsi + d, 0, 4); s.timeSpeed = tspeeds[tsi]; break;
            case 16: prof.timeOfDay = std::fmod(std::floor(prof.timeOfDay) + d + 24.0f, 24.0f); break;
            case 17:
                if (!confirmReset) confirmReset = true;
                else {
                    confirmReset = false;
                    prof = Profile{};
                    prof.settings = s;
                    save();
                    for (auto& p : world.props) p.alive = true;
                    enterState(GS_TITLE);
                    return;
                }
                break;
        }
        applySettings();
    }
    if (confirmReset && ui.nav.back) confirmReset = false;
    ui.text("Качество текстур применится после перезапуска игры", area.x, area.y + area.height - 20, 16, pal::muted);
}

void Game::drawControlsTab(Rectangle area) {
    struct R { const char* act; const char* kb; const char* pad; };
    static const R rows[] = {
        {"Газ", "W / ↑", "RT"},
        {"Тормоз / задний ход", "S / ↓", "LT"},
        {"Руль", "A D / ← →", "Левый стик"},
        {"Ручной тормоз", "Пробел", "A"},
        {"Передача вверх / вниз", "E / Q (Shift / Ctrl)", "B / X"},
        {"Перемотка (удерживать)", "R / Backspace", "Y"},
        {"Сменить камеру", "C", "RB"},
        {"Смотреть назад", "V", "LB"},
        {"Обзор", "Правая кнопка мыши", "Правый стик"},
        {"Начать событие", "Enter", "Крестовина ↑"},
        {"Вернуться на дорогу", "T", "—"},
        {"Фары", "L", "—"},
        {"Карта", "M / Tab", "Back"},
        {"Меню / пауза", "Esc / P", "Start"},
        {"Помощь / отладка", "F1 / F3", "—"},
    };
    ui.text("Действие", area.x, area.y, 20, pal::orange, F_BOLD);
    ui.text("Клавиатура и мышь", area.x + 380, area.y, 20, pal::orange, F_BOLD);
    ui.text("Геймпад", area.x + 740, area.y, 20, pal::orange, F_BOLD);
    float y = area.y + 36;
    for (const R& r : rows) {
        ui.text(r.act, area.x, y, 20, WHITE);
        ui.text(r.kb, area.x + 380, y, 20, WHITE, F_BOLD);
        ui.text(r.pad, area.x + 740, y, 20, pal::muted, F_BOLD);
        y += 31;
    }
}

// ------------------------------------------------------------------ колесо удачи
void Game::drawWheelspin(float dt) {
    ui.rect({0, 0, ui.W, ui.H}, alpha(Color{6, 8, 16, 255}, 0.9f));
    ui.text("КОЛЕСО УДАЧИ", ui.W * 0.5f, 40, 48, WHITE, F_DISPLAY, 1, true);
    spinT += dt;
    float k = clamp01(spinT / spinDur);
    float e = 1 - std::pow(1 - k, 3.2f);
    float pos = e * spinResult;
    static int lastTick = -1;
    if ((int)pos != lastTick) {
        lastTick = (int)pos;
        if (k < 1) audio.play(SFX_UI_MOVE, 0.6f);
    }
    float cy = ui.H * 0.46f, cardH = 110;
    ui.rrect({ui.W * 0.5f - 330, cy - cardH * 0.5f - 8, 660, cardH + 16}, 0.12f, alpha(pal::orange, 0.25f));
    Rectangle clip = ui.px({0, 110, ui.W, ui.H - 330});
    BeginScissorMode((int)clip.x, (int)clip.y, (int)clip.width, (int)clip.height);
    for (int i = (int)pos - 3; i <= (int)pos + 3; i++) {
        if (i < 0 || i >= (int)spinReel.size()) continue;
        float off = (i - pos) * (cardH + 14);
        float a = clamp01(1.2f - std::fabs(off) / (cardH * 3.2f));
        Rectangle r{ui.W * 0.5f - 300, cy - cardH * 0.5f + off, 600, cardH};
        const SpinPrize& p = spinReel[i];
        ui.rrect(r, 0.1f, alpha(pal::panel2, a));
        ui.rect({r.x, r.y + 10, 8, r.height - 20}, alpha(p.c, a));
        ui.text(p.title, r.x + 34, r.y + 20, 36, alpha(WHITE, a), F_DISPLAY);
        ui.text(p.sub, r.x + 34, r.y + 68, 20, alpha(pal::muted, a));
    }
    EndScissorMode();
    if (k >= 1 && !spinDone) {
        spinDone = true;
        audio.play(SFX_REWARD);
    }
    if (spinDone) {
        const SpinPrize& p = spinReel[spinResult];
        ui.text("Вы выиграли: " + p.title, ui.W * 0.5f, ui.H - 170, 30, pal::yellow, F_BOLD, 1, true);
        Rectangle b{ui.W * 0.5f - 170, ui.H - 110, 340, 52};
        if (ui.button(b, "Забрать", true, 26)) {
            prof.wheelspins = std::max(0, prof.wheelspins - 1);
            if (!p.carId.empty()) {
                if (prof.owns(p.carId)) {
                    const CarSpec* s = findCar(p.carId);
                    int cr = s ? s->price / 2 : 10000;
                    addCredits(cr);
                    ui.toast("Такая машина уже есть", "Компенсация: " + fmtCr(cr), pal::cyan, 3.0f);
                } else {
                    const CarSpec* s = findCar(p.carId);
                    prof.garage.push_back({p.carId, s ? s->color : 0xffffffff});
                    ui.toast("Новая машина в гараже!", p.title, pal::green, 3.0f);
                }
            } else addCredits(p.credits);
            save();
            audio.play(SFX_UI_OK);
            state = GS_PAUSE;
            tab = TAB_WHEELSPIN;
        }
    }
}

// ------------------------------------------------------------------ прочее
void Game::drawRewindOverlay() {
    ui.rect({0, 0, ui.W, ui.H}, Color{40, 90, 160, 40});
    for (float y = 0; y < ui.H; y += 6) ui.rect({0, y, ui.W, 1.5f}, Color{255, 255, 255, 10});
    float cx = ui.W * 0.5f - 150;
    for (int k = 0; k < 2; k++) ui.tri({cx + k * 22.0f, 92}, {cx + 22 + k * 22.0f, 76}, {cx + 22 + k * 22.0f, 108}, WHITE);
    ui.text("ПЕРЕМОТКА", ui.W * 0.5f + 24, 70, 40, WHITE, F_DISPLAY, 1, true);
}

void Game::drawFade(float dt) {
    if (fade <= 0) return;
    ui.rect({0, 0, ui.W, ui.H}, alpha(BLACK, fade));
    fade = std::max(0.0f, fade - dt * 1.8f);
}

void Game::drawDebug() {
    char buf[512];
    std::snprintf(buf, sizeof buf, "FPS %.0f · вызовов %d · инстансов %d · частиц %d · следов %d\nпоз %.1f %.1f %.1f · %.1f км/ч · передача %d · %.0f об/мин\nпокрытие %s · колёс на земле %d · время %.2f",
                  fpsAvg, rend.drawCalls, rend.instanceCount, (int)fx.parts.size(), (int)fx.skids.size(), player.pos.x, player.pos.y, player.pos.z, player.kmh(), player.gear, player.rpm,
                  surfaceName(player.surfUnder), player.wheelsOnGround, prof.timeOfDay);
    ui.rect({ui.W - 640, ui.H - 330, 610, 80}, alpha(BLACK, 0.6f));
    ui.textBox(buf, {ui.W - 630, ui.H - 324, 600, 80}, 15, pal::green, F_REG);
}

void Game::drawHelp() {
    Rectangle r{ui.W * 0.5f - 420, ui.H * 0.5f - 250, 840, 500};
    ui.rrect(r, 0.05f, alpha(pal::panel, 0.96f));
    ui.text("КАК ИГРАТЬ", r.x + 30, r.y + 24, 32, pal::orange, F_DISPLAY);
    ui.textBox("Катайтесь по побережью, ищите события (цветные лучи света), зарабатывайте кредиты и опыт. "
               "Дрифт, прыжки, обгоны, высокая скорость и разрушения собираются в цепочку навыков — не врезайтесь, иначе она сгорит. "
               "Радары и зоны скорости отмечены звёздами на карте. Оранжевые доски опыта спрятаны у дорог — разбейте все 30. "
               "Перемотка (R) спасает после ошибки. В меню (Esc): карта с быстрым перемещением, гараж, автосалон и колесо удачи.",
               {r.x + 30, r.y + 80, r.width - 60, 200}, 21, WHITE);
    ui.text("W/S — газ/тормоз · A/D — руль · Пробел — ручник · R — перемотка · C — камера · Enter — событие", r.x + 30, r.y + 330, 19, pal::muted);
    ui.text("F1 — закрыть", r.x + 30, r.y + r.height - 44, 19, pal::yellow, F_BOLD);
}

}  // namespace cl
