// Интерфейс: шрифты с кириллицей, виджеты, уведомления, текстура карты
#pragma once
#include <string>
#include <vector>

#include "../world/world.h"
#include "raylib.h"

namespace cl {

enum FontKind { F_DISPLAY, F_REG, F_BOLD };

namespace pal {
constexpr Color orange{255, 116, 38, 255};
constexpr Color pink{236, 46, 132, 255};
constexpr Color cyan{48, 196, 255, 255};
constexpr Color yellow{255, 206, 48, 255};
constexpr Color green{90, 220, 120, 255};
constexpr Color red{240, 64, 60, 255};
constexpr Color white{250, 250, 252, 255};
constexpr Color muted{176, 184, 204, 255};
constexpr Color dark{12, 14, 22, 255};
constexpr Color panel{14, 16, 26, 214};
constexpr Color panel2{26, 30, 46, 230};
}  // namespace pal

inline Color alpha(Color c, float a) { c.a = (unsigned char)(c.a * (a < 0 ? 0 : a > 1 ? 1 : a)); return c; }
Color mixColor(Color a, Color b, float t);
Color classColor(int classIdx);

struct Nav {
    bool up = false, down = false, left = false, right = false, ok = false, back = false, tabL = false, tabR = false, any = false;
};

struct Toast {
    std::string title, sub;
    Color c;
    float t = 0, dur = 3;
};

class Ui {
public:
    float S = 1, W = 1280, H = 720;   // масштаб и виртуальный размер экрана
    Vector2 mouse{0, 0};
    bool click = false, mouseMoved = false;
    float wheel = 0;
    Nav nav;
    bool gamepad = false;              // последнее устройство ввода — геймпад
    float time = 0;

    void init();
    void unload();
    void beginFrame(float dt);
    const Font& font(FontKind k, float px) const;
    Vector2 measure(const std::string& s, float size, FontKind k = F_REG) const;
    // align: 0 — слева, 1 — по центру, 2 — справа
    void text(const std::string& s, float x, float y, float size, Color c, FontKind k = F_REG, int align = 0, bool shadow = false) const;
    void textBox(const std::string& s, Rectangle r, float size, Color c, FontKind k = F_REG) const;  // перенос по словам
    void rect(Rectangle r, Color c) const;
    void rrect(Rectangle r, float round, Color c) const;
    void rrectLines(Rectangle r, float round, float thick, Color c) const;
    void gradH(Rectangle r, Color a, Color b) const;
    void gradV(Rectangle r, Color a, Color b) const;
    void line(Vector2 a, Vector2 b, float thick, Color c) const;
    void circle(Vector2 c, float r, Color col) const;
    void ring(Vector2 c, float r0, float r1, float a0, float a1, Color col) const;
    void star(Vector2 c, float r, Color col, bool filled) const;
    void tri(Vector2 a, Vector2 b, Vector2 c, Color col) const;
    // стрелка-шеврон: dir = -1 влево, 1 вправо
    void chevron(Vector2 c, float size, int dir, Color col) const;
    Rectangle px(Rectangle r) const { return {r.x * S, r.y * S, r.width * S, r.height * S}; }
    Vector2 pv(Vector2 v) const { return {v.x * S, v.y * S}; }
    bool hover(Rectangle r) const;
    // кнопка: подсветка при выборе с клавиатуры или наведении мышью
    bool button(Rectangle r, const std::string& label, bool selected, float size = 24, bool enabled = true, Color accent = pal::orange);
    // строка настроек со значением «< значение >»
    int optionRow(Rectangle r, const std::string& label, const std::string& value, bool selected);
    void keycap(const std::string& k, float x, float y, float h) const;
    // уведомления
    void toast(const std::string& title, const std::string& sub, Color c, float dur = 3.2f);
    void drawToasts(float dt);
    std::vector<Toast> toasts;

private:
    Font disp_[2]{}, reg_[3]{}, bold_[3]{};
    int dispSz_[2] = {48, 96}, regSz_[3] = {20, 30, 46}, boldSz_[3] = {22, 34, 58};
    bool loaded_ = false;
    Vector2 lastMouse_{0, 0};
};

// Карта мира для мини-карты и экрана карты
Image buildMapImage(const World& w, int size);

}  // namespace cl
