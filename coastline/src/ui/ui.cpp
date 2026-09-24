#include "ui.h"

#include <cmath>
#include <cstddef>

#include "rlgl.h"

extern const unsigned char asset_RussoOne_Regular[];
extern const size_t asset_RussoOne_Regular_size;
extern const unsigned char asset_PT_Sans_Web_Regular[];
extern const size_t asset_PT_Sans_Web_Regular_size;
extern const unsigned char asset_PT_Sans_Web_Bold[];
extern const size_t asset_PT_Sans_Web_Bold_size;

namespace cl {

Color mixColor(Color a, Color b, float t) {
    auto m = [&](unsigned char x, unsigned char y) { return (unsigned char)(x + (y - x) * t); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
}

Color classColor(int c) {
    static const Color cols[7] = {{96, 190, 255, 255}, {250, 216, 56, 255}, {255, 150, 40, 255}, {240, 70, 60, 255}, {190, 90, 255, 255}, {60, 120, 255, 255}, {40, 230, 150, 255}};
    return cols[c < 0 ? 0 : c > 6 ? 6 : c];
}

static std::vector<int> codepoints() {
    std::vector<int> cp;
    for (int c = 32; c < 127; c++) cp.push_back(c);
    for (int c = 0xA0; c <= 0xFF; c++) cp.push_back(c);
    for (int c = 0x400; c <= 0x45F; c++) cp.push_back(c);
    for (int c = 0x2010; c <= 0x2027; c++) cp.push_back(c);
    for (int c : {0x2116, 0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x221E, 0x20BD}) cp.push_back(c);
    return cp;
}

static Font loadFont(const unsigned char* data, size_t size, int px, std::vector<int>& cps) {
    Font f = LoadFontFromMemory(".ttf", data, (int)size, px, cps.data(), (int)cps.size());
    if (f.texture.id == 0) return GetFontDefault();
    GenTextureMipmaps(&f.texture);
    SetTextureFilter(f.texture, TEXTURE_FILTER_TRILINEAR);
    return f;
}

void Ui::init() {
    std::vector<int> cps = codepoints();
    for (int i = 0; i < 2; i++) disp_[i] = loadFont(asset_RussoOne_Regular, asset_RussoOne_Regular_size, dispSz_[i], cps);
    for (int i = 0; i < 3; i++) reg_[i] = loadFont(asset_PT_Sans_Web_Regular, asset_PT_Sans_Web_Regular_size, regSz_[i], cps);
    for (int i = 0; i < 3; i++) bold_[i] = loadFont(asset_PT_Sans_Web_Bold, asset_PT_Sans_Web_Bold_size, boldSz_[i], cps);
    loaded_ = true;
}

void Ui::unload() {
    if (!loaded_) return;
    for (auto& f : disp_) UnloadFont(f);
    for (auto& f : reg_) UnloadFont(f);
    for (auto& f : bold_) UnloadFont(f);
    loaded_ = false;
}

const Font& Ui::font(FontKind k, float px) const {
    const Font* fs = k == F_DISPLAY ? disp_ : k == F_BOLD ? bold_ : reg_;
    const int* sz = k == F_DISPLAY ? dispSz_ : k == F_BOLD ? boldSz_ : regSz_;
    int n = k == F_DISPLAY ? 2 : 3;
    for (int i = 0; i < n; i++)
        if (sz[i] >= px * 0.92f) return fs[i];
    return fs[n - 1];
}

void Ui::beginFrame(float dt) {
    time += dt;
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    S = std::fmin(sw / 1280.0f, sh / 720.0f);
    if (S <= 0) S = 1;
    W = sw / S;
    H = sh / S;
    Vector2 m = GetMousePosition();
    mouse = {m.x / S, m.y / S};
    mouseMoved = std::fabs(m.x - lastMouse_.x) + std::fabs(m.y - lastMouse_.y) > 1.5f;
    lastMouse_ = m;
    click = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    wheel = GetMouseWheelMove();
    nav = Nav{};
    nav.up = IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP) || IsKeyPressed(KEY_W);
    nav.down = IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN) || IsKeyPressed(KEY_S);
    nav.left = IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT) || IsKeyPressed(KEY_A);
    nav.right = IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT) || IsKeyPressed(KEY_D);
    nav.ok = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE);
    nav.back = IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE);
    nav.tabL = IsKeyPressed(KEY_Q) || IsKeyPressed(KEY_PAGE_UP);
    nav.tabR = IsKeyPressed(KEY_E) || IsKeyPressed(KEY_PAGE_DOWN);
    if (IsGamepadAvailable(0)) {
        static float repeatT = 0;
        static int lastDir = 0;
        float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), ay = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
        int dir = 0;
        if (ay < -0.6f) dir = 1; else if (ay > 0.6f) dir = 2; else if (ax < -0.6f) dir = 3; else if (ax > 0.6f) dir = 4;
        bool fire = false;
        if (dir != lastDir) { fire = dir != 0; repeatT = 0.35f; }
        else if (dir != 0) { repeatT -= dt; if (repeatT <= 0) { fire = true; repeatT = 0.12f; } }
        lastDir = dir;
        auto btn = [](int b) { return IsGamepadButtonPressed(0, b); };
        nav.up |= btn(GAMEPAD_BUTTON_LEFT_FACE_UP) || (fire && dir == 1);
        nav.down |= btn(GAMEPAD_BUTTON_LEFT_FACE_DOWN) || (fire && dir == 2);
        nav.left |= btn(GAMEPAD_BUTTON_LEFT_FACE_LEFT) || (fire && dir == 3);
        nav.right |= btn(GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || (fire && dir == 4);
        nav.ok |= btn(GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        nav.back |= btn(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT);
        nav.tabL |= btn(GAMEPAD_BUTTON_LEFT_TRIGGER_1);
        nav.tabR |= btn(GAMEPAD_BUTTON_RIGHT_TRIGGER_1);
        if (GetGamepadButtonPressed() != GAMEPAD_BUTTON_UNKNOWN || dir != 0) gamepad = true;
    }
    if (GetKeyPressed() != 0 || mouseMoved) gamepad = false;
    nav.any = nav.up || nav.down || nav.left || nav.right || nav.ok || nav.back || nav.tabL || nav.tabR;
}

Vector2 Ui::measure(const std::string& s, float size, FontKind k) const {
    float px = size * S;
    const Font& f = font(k, px);
    float sp = k == F_DISPLAY ? px * 0.02f : 0.0f;
    Vector2 v = MeasureTextEx(f, s.c_str(), px, sp);
    return {v.x / S, v.y / S};
}

void Ui::text(const std::string& s, float x, float y, float size, Color c, FontKind k, int align, bool shadow) const {
    if (s.empty() || c.a == 0) return;
    float px = size * S;
    const Font& f = font(k, px);
    float sp = k == F_DISPLAY ? px * 0.02f : 0.0f;
    float wv = 0;
    if (align) wv = MeasureTextEx(f, s.c_str(), px, sp).x;
    float X = x * S - (align == 1 ? wv * 0.5f : align == 2 ? wv : 0);
    if (shadow) DrawTextEx(f, s.c_str(), {std::round(X + px * 0.05f), std::round(y * S + px * 0.06f)}, px, sp, Color{0, 0, 0, (unsigned char)(c.a * 0.55f)});
    DrawTextEx(f, s.c_str(), {std::round(X), std::round(y * S)}, px, sp, c);
}

void Ui::textBox(const std::string& s, Rectangle r, float size, Color c, FontKind k) const {
    std::vector<std::string> words;
    std::string cur;
    for (char ch : s) {
        if (ch == ' ' || ch == '\n') {
            if (!cur.empty()) words.push_back(cur);
            cur.clear();
            if (ch == '\n') words.push_back("\n");
        } else cur += ch;
    }
    if (!cur.empty()) words.push_back(cur);
    float y = r.y;
    std::string line;
    auto flush = [&]() {
        text(line, r.x, y, size, c, k);
        y += size * 1.28f;
        line.clear();
    };
    for (auto& wd : words) {
        if (wd == "\n") { flush(); continue; }
        std::string t = line.empty() ? wd : line + " " + wd;
        if (measure(t, size, k).x > r.width && !line.empty()) {
            flush();
            line = wd;
        } else line = t;
        if (y > r.y + r.height) return;
    }
    if (!line.empty()) flush();
}

void Ui::rect(Rectangle r, Color c) const { DrawRectangleRec(px(r), c); }
void Ui::rrect(Rectangle r, float round, Color c) const { DrawRectangleRounded(px(r), round, 8, c); }
void Ui::rrectLines(Rectangle r, float round, float thick, Color c) const { DrawRectangleRoundedLinesEx(px(r), round, 8, thick * S, c); }
void Ui::gradH(Rectangle r, Color a, Color b) const { DrawRectangleGradientH((int)(r.x * S), (int)(r.y * S), (int)std::ceil(r.width * S), (int)std::ceil(r.height * S), a, b); }
void Ui::gradV(Rectangle r, Color a, Color b) const { DrawRectangleGradientV((int)(r.x * S), (int)(r.y * S), (int)std::ceil(r.width * S), (int)std::ceil(r.height * S), a, b); }
void Ui::line(Vector2 a, Vector2 b, float thick, Color c) const { DrawLineEx(pv(a), pv(b), thick * S, c); }
void Ui::circle(Vector2 c, float r, Color col) const { DrawCircleV(pv(c), r * S, col); }
void Ui::ring(Vector2 c, float r0, float r1, float a0, float a1, Color col) const { DrawRing(pv(c), r0 * S, r1 * S, a0, a1, 48, col); }
void Ui::tri(Vector2 a, Vector2 b, Vector2 c, Color col) const {
    Vector2 A = pv(a), B = pv(b), C = pv(c);
    // raylib ждёт обход против часовой стрелки
    float cr = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
    if (cr > 0) DrawTriangle(A, C, B, col);
    else DrawTriangle(A, B, C, col);
}

void Ui::chevron(Vector2 c, float s, int dir, Color col) const {
    float d = (float)dir;
    line({c.x - d * s * 0.3f, c.y - s * 0.5f}, {c.x + d * s * 0.25f, c.y}, s * 0.2f, col);
    line({c.x + d * s * 0.25f, c.y}, {c.x - d * s * 0.3f, c.y + s * 0.5f}, s * 0.2f, col);
}

void Ui::star(Vector2 c, float r, Color col, bool filled) const {
    Vector2 pts[10];
    for (int i = 0; i < 10; i++) {
        float a = -PI * 0.5f + i * PI / 5;
        float rr = (i % 2) ? r * 0.45f : r;
        pts[i] = {c.x + std::cos(a) * rr, c.y + std::sin(a) * rr};
    }
    if (filled) {
        for (int i = 0; i < 10; i++) tri(c, pts[i], pts[(i + 1) % 10], col);
    } else {
        for (int i = 0; i < 10; i++) line(pts[i], pts[(i + 1) % 10], std::fmax(1.5f, r * 0.14f), col);
    }
}

bool Ui::hover(Rectangle r) const { return CheckCollisionPointRec(mouse, r); }

bool Ui::button(Rectangle r, const std::string& label, bool selected, float size, bool enabled, Color accent) {
    bool hv = hover(r) && enabled;
    bool on = (selected || hv) && enabled;
    if (on) {
        rrect(r, 0.18f, accent);
        gradV({r.x, r.y, r.width, r.height * 0.5f}, alpha(WHITE, 0.14f), alpha(WHITE, 0.0f));
    } else rrect(r, 0.18f, enabled ? pal::panel2 : alpha(pal::panel2, 0.6f));
    if (selected && enabled) rrectLines({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 0.2f, 2, alpha(WHITE, 0.85f));
    Color tc = enabled ? (on ? WHITE : pal::white) : alpha(pal::muted, 0.6f);
    text(label, r.x + r.width * 0.5f, r.y + (r.height - size) * 0.5f - 1, size, tc, F_BOLD, 1);
    return enabled && ((hv && click) || (selected && nav.ok));
}

int Ui::optionRow(Rectangle r, const std::string& label, const std::string& value, bool selected) {
    bool hv = hover(r);
    rrect(r, 0.2f, selected ? alpha(pal::orange, 0.9f) : (hv ? alpha(pal::panel2, 1) : alpha(pal::panel2, 0.75f)));
    text(label, r.x + 16, r.y + (r.height - 22) * 0.5f, 22, WHITE, F_REG);
    float vx = r.x + r.width - 150;
    Rectangle la{vx - 36, r.y, 32, r.height}, ra{r.x + r.width - 36, r.y, 32, r.height};
    chevron({la.x + 16, r.y + r.height * 0.5f}, 16, -1, selected ? WHITE : pal::muted);
    chevron({ra.x + 16, r.y + r.height * 0.5f}, 16, 1, selected ? WHITE : pal::muted);
    text(value, (la.x + la.width + ra.x) * 0.5f, r.y + (r.height - 22) * 0.5f, 22, WHITE, F_BOLD, 1);
    int d = 0;
    if (selected && nav.left) d = -1;
    if (selected && nav.right) d = 1;
    if (click && hover(la)) d = -1;
    if (click && hover(ra)) d = 1;
    if (click && hv && d == 0) d = 1;
    return d;
}

void Ui::keycap(const std::string& k, float x, float y, float h) const {
    float w = std::fmax(h, measure(k, h * 0.62f, F_BOLD).x + h * 0.6f);
    rrect({x, y, w, h}, 0.3f, alpha(WHITE, 0.92f));
    text(k, x + w * 0.5f, y + h * 0.16f, h * 0.62f, pal::dark, F_BOLD, 1);
}

void Ui::toast(const std::string& title, const std::string& sub, Color c, float dur) {
    for (auto& t : toasts)
        if (t.title == title && t.sub == sub) { t.t = std::fmin(t.t, 0.3f); return; }
    toasts.push_back({title, sub, c, 0, dur});
    if (toasts.size() > 4) toasts.erase(toasts.begin());
}

void Ui::drawToasts(float dt) {
    float y = 96;
    for (size_t i = 0; i < toasts.size();) {
        Toast& t = toasts[i];
        t.t += dt;
        if (t.t > t.dur) { toasts.erase(toasts.begin() + i); continue; }
        float a = std::fmin(1.0f, std::fmin(t.t * 5, (t.dur - t.t) * 3));
        float slide = (1 - std::fmin(1.0f, t.t * 5)) * 40;
        float w = std::fmax(measure(t.title, 26, F_BOLD).x, measure(t.sub, 19).x) + 60;
        float x = W * 0.5f - w * 0.5f + slide;
        rrect({x, y, w, t.sub.empty() ? 44.0f : 70.0f}, 0.25f, alpha(pal::panel, a));
        rect({x, y + 8, 5, (t.sub.empty() ? 44.0f : 70.0f) - 16}, alpha(t.c, a));
        text(t.title, x + 24, y + 8, 26, alpha(WHITE, a), F_BOLD);
        if (!t.sub.empty()) text(t.sub, x + 24, y + 40, 19, alpha(pal::muted, a));
        y += t.sub.empty() ? 52 : 78;
        i++;
    }
}

// ------------------------------------------------------------------ карта
Image buildMapImage(const World& w, int size) {
    Image im = GenImageColor(size, size, Color{20, 70, 110, 255});
    Color* px = (Color*)im.data;
    const float span = 3072.0f;
    V3 ld = norm(V3{-0.5f, 0.75f, -0.45f});
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            float wx = World::ORIGIN + (x + 0.5f) / size * span, wz = World::ORIGIN + (y + 0.5f) / size * span;
            float h = w.terrainH(wx, wz);
            Color c;
            if (h < 0) {
                float d = clamp01(-h / 14.0f);
                c = mixColor(Color{64, 170, 196, 255}, Color{18, 64, 108, 255}, d);
            } else {
                int i = clampf((wx - World::ORIGIN) / World::CELL + 0.5f, 0, World::N - 1), j = clampf((wz - World::ORIGIN) / World::CELL + 0.5f, 0, World::N - 1);
                int k = w.idx(i, j);
                float sand = w.tint[k * 4 + 1] / 255.0f, forest = w.tint[k * 4 + 2] / 255.0f, paved = w.tint[k * 4 + 3] / 255.0f;
                V3 n = w.terrainN(wx, wz);
                Color g = mixColor(Color{128, 172, 88, 255}, Color{96, 140, 72, 255}, clamp01(h / 120.0f));
                g = mixColor(g, Color{62, 108, 58, 255}, forest * 0.8f);
                float rock = clamp01((1.0f - n.y - 0.17f) * 8.0f) + clamp01((h - 170.0f) / 60.0f) * 0.7f;
                g = mixColor(g, Color{150, 146, 136, 255}, clamp01(rock));
                g = mixColor(g, Color{232, 216, 170, 255}, clamp01(sand + (h < 1.2f ? 1.0f : 0.0f)));
                g = mixColor(g, Color{206, 200, 190, 255}, paved);
                float sh = 0.72f + 0.42f * clampf(dot(n, ld), 0, 1);
                c = {(unsigned char)clampf(g.r * sh, 0, 255), (unsigned char)clampf(g.g * sh, 0, 255), (unsigned char)clampf(g.b * sh, 0, 255), 255};
            }
            px[y * size + x] = c;
        }
    float k = size / span;
    auto dot = [&](float wx, float wz, float r, Color c) {
        float cx = (wx - World::ORIGIN) * k, cz = (wz - World::ORIGIN) * k;
        int x0 = (int)std::floor(cx - r), x1 = (int)std::ceil(cx + r), y0 = (int)std::floor(cz - r), y1 = (int)std::ceil(cz + r);
        for (int y = std::max(y0, 0); y <= std::min(y1, size - 1); y++)
            for (int x = std::max(x0, 0); x <= std::min(x1, size - 1); x++) {
                float d = std::sqrt(sq(x + 0.5f - cx) + sq(y + 0.5f - cz));
                float a = clamp01(r + 0.5f - d);
                if (a <= 0) continue;
                px[y * size + x] = mixColor(px[y * size + x], c, a * c.a / 255.0f);
            }
    };
    // тропы кросс-кантри
    for (const Trail& t : w.trails)
        for (size_t i = 0; i < t.pts.size(); i += 2)
            if ((i / 6) % 2 == 0) dot(t.pts[i].x, t.pts[i].z, 0.9f, Color{150, 110, 70, 200});
    // здания
    for (const Building& b : w.buildings) {
        float cs = std::cos(b.yaw), sn = std::sin(b.yaw);
        for (float u = -b.hx; u <= b.hx; u += 1.5f)
            for (float v = -b.hz; v <= b.hz; v += 1.5f) dot(b.c.x + u * cs + v * sn, b.c.z - u * sn + v * cs, 0.6f, Color{226, 214, 204, 255});
    }
    // дороги: сначала контур, затем заливка
    for (int pass = 0; pass < 2; pass++)
        for (const Road& r : w.roads) {
            float rad = std::fmax(r.halfW() * k, 0.8f);
            Color c = r.type == ROAD_HIGHWAY ? Color{255, 214, 110, 255} : r.type == ROAD_DIRT ? Color{206, 170, 120, 255} : r.type == ROAD_RUNWAY ? Color{130, 132, 138, 255} : Color{250, 250, 246, 255};
            if (pass == 0) { c = Color{40, 40, 46, 200}; rad += 0.9f; }
            for (size_t i = 0; i + 1 < r.pts.size() + (r.loop ? 1 : 0); i++) {
                V3 a = r.pts[i].p, b = r.pts[(i + 1) % r.pts.size()].p;
                float L = std::sqrt(sq(b.x - a.x) + sq(b.z - a.z));
                int steps = std::max(1, (int)(L * k / 0.7f));
                for (int s = 0; s < steps; s++) {
                    float t = (float)s / steps;
                    dot(a.x + (b.x - a.x) * t, a.z + (b.z - a.z) * t, rad, c);
                }
            }
        }
    return im;
}

}  // namespace cl
