#include "textures.h"

#include <cmath>
#include <vector>

#include "../core/mathx.h"
#include "rlgl.h"

namespace cl {

// Бесшовный шум значений: решётка с периодом per
static float tnoise(float x, float y, int per, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float fx = x - xi, fy = y - yi;
    auto h = [&](int a, int b) { return hash2f(((a % per) + per) % per + (int)seed * 131, ((b % per) + per) % per + (int)seed * 71); };
    float u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    return mixf(mixf(h(xi, yi), h(xi + 1, yi), u), mixf(h(xi, yi + 1), h(xi + 1, yi + 1), u), v);
}
static float tfbm(float x, float y, int per, int oct, uint32_t seed) {
    float s = 0, a = 0.5f, n = 0;
    for (int i = 0; i < oct; i++) {
        s += tnoise(x, y, per, seed + i) * a;
        n += a;
        x *= 2; y *= 2; per *= 2; a *= 0.5f;
    }
    return s / n;
}

struct Img {
    int w, h;
    std::vector<Color> px;
    Img(int w_, int h_) : w(w_), h(h_), px(w_ * h_, Color{255, 255, 255, 255}) {}
    Color& at(int x, int y) { return px[((y % h + h) % h) * w + ((x % w + w) % w)]; }
    void set(int x, int y, float r, float g, float b, float a = 1) {
        at(x, y) = Color{(unsigned char)(clamp01(r) * 255), (unsigned char)(clamp01(g) * 255), (unsigned char)(clamp01(b) * 255), (unsigned char)(clamp01(a) * 255)};
    }
    Texture2D upload(bool mip = true, bool repeat = true) {
        Image im = {px.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        Texture2D t = LoadTextureFromImage(im);
        if (mip) {
            GenTextureMipmaps(&t);
            SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR);
        } else SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
        SetTextureWrap(t, repeat ? TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
        return t;
    }
};

static Texture2D makeGround(int S, uint32_t seed, float r0, float g0, float b0, float r1, float g1, float b1, float detail, int kind) {
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = (float)x / S, v = (float)y / S;
            float n = tfbm(u * 8, v * 8, 8, 5, seed);
            float fine = hash2f(x * 3 + (int)seed, y * 5 + 1);
            float t = clamp01(n * 1.3f - 0.15f + (fine - 0.5f) * detail);
            float r = mixf(r0, r1, t), g = mixf(g0, g1, t), b = mixf(b0, b1, t);
            if (kind == 1) {  // трава: травинки
                float blade = hash2f(x * 7 + 3, y * 2 + (int)seed);
                if (blade > 0.82f) { r *= 1.15f; g *= 1.18f; }
                if (blade < 0.08f) { r *= 0.75f; g *= 0.8f; b *= 0.8f; }
                float flower = tnoise(u * 32, v * 32, 32, seed + 9);
                if (flower > 0.8f && hash2f(x, y) > 0.985f) { r = 0.95f; g = 0.9f; b = 0.4f; }
            } else if (kind == 2) {  // скала: трещины
                float cr = std::fabs(tfbm(u * 6, v * 6, 6, 4, seed + 5) - 0.5f);
                if (cr < 0.025f) { r *= 0.6f; g *= 0.6f; b *= 0.6f; }
                float strata = std::sin(v * 60 + n * 8) * 0.05f;
                r += strata; g += strata; b += strata;
            } else if (kind == 3) {  // песок: рябь
                float rip = std::sin((v * 40 + tfbm(u * 4, v * 4, 4, 3, seed) * 6) * TAU) * 0.035f;
                r += rip; g += rip; b += rip;
            } else if (kind == 4) {  // гравий: камешки
                float st = tnoise(u * 64, v * 64, 64, seed + 3);
                if (st > 0.72f) { r *= 1.25f; g *= 1.22f; b *= 1.2f; }
                if (st < 0.2f) { r *= 0.7f; g *= 0.7f; b *= 0.72f; }
            }
            im.set(x, y, r, g, b);
        }
    return im.upload();
}

static Texture2D makePaved(int S) {
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = (float)x / S, v = (float)y / S;
            float n = tfbm(u * 6, v * 6, 6, 4, 71);
            float c = 0.5f + (n - 0.5f) * 0.12f + (hash2f(x, y + 7) - 0.5f) * 0.05f;
            // плиты 4 м (текстура 8 м)
            float gx = std::fmod(u * 2, 1.0f), gy = std::fmod(v * 2, 1.0f);
            float joint = (gx < 0.012f || gx > 0.988f || gy < 0.012f || gy > 0.988f) ? 0.55f : 1.0f;
            // пятна
            float st = tnoise(u * 10, v * 10, 10, 90);
            c *= 1.0f - smoothstep(0.7f, 0.9f, st) * 0.06f;
            im.set(x, y, c * joint, c * joint * 0.99f, c * joint * 0.96f);
        }
    return im.upload();
}

// Дорога: u — поперёк (0..1), v — вдоль; alpha = разметка (для блеска)
static Texture2D makeRoad(int type) {
    const int W = 256, H = 512;
    Img im(W, H);
    float width = type == 0 ? 15.0f : type == 1 ? 9.5f : type == 2 ? 11.0f : type == 3 ? 7.5f : 46.0f;
    float period = type == 4 ? 60.0f : 12.0f;  // метров на текстуру по длине
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
            float mx = u * width, my = v * period;  // метры
            float n = tfbm(u * 8, v * 16, 8, 4, 33 + type);
            float grain = hash2f(x * 3 + type, y * 7);
            float r, g, b, a = 0;
            if (type == 3) {
                // грунт: колеи и трава посередине
                float base = 0.46f + (n - 0.5f) * 0.18f + (grain - 0.5f) * 0.08f;
                r = base * 1.12f; g = base * 0.92f; b = base * 0.66f;
                float rut = std::min(std::fabs(mx - width * 0.3f), std::fabs(mx - width * 0.7f));
                if (rut < 0.45f) { r *= 0.82f; g *= 0.82f; b *= 0.82f; }
                float mid = std::fabs(mx - width * 0.5f);
                if (mid < 0.7f && tnoise(u * 20, v * 40, 20, 5) > 0.45f) { r = 0.35f; g = 0.45f; b = 0.2f; }
                float edge = std::min(mx, width - mx);
                if (edge < 0.6f) { r = mixf(0.36f, r, edge / 0.6f); g = mixf(0.44f, g, edge / 0.6f); b = mixf(0.2f, b, edge / 0.6f); }
            } else {
                float base = 0.21f + (n - 0.5f) * 0.06f + (grain - 0.5f) * 0.05f;
                if (type == 4) base += 0.04f;
                r = g = b = base;
                b += 0.01f;
                // заплатки и следы
                float patch = tnoise(u * 3, v * 6, 3, 50 + type);
                if (patch > 0.75f) { r *= 0.85f; g *= 0.85f; b *= 0.85f; }
                float tire = std::fabs(std::fmod(mx, width / (type == 0 ? 4 : 2)) - width / (type == 0 ? 8 : 4));
                if (tire > width / (type == 0 ? 8 : 4) - 0.9f && tire < width / (type == 0 ? 8 : 4) - 0.2f) { r *= 0.9f; g *= 0.9f; b *= 0.9f; }
                auto line = [&](float center, float half) { return std::fabs(mx - center) < half; };
                auto white = [&]() { r = g = b = 0.86f; a = 1; };
                auto yellow = [&]() { r = 0.9f; g = 0.72f; b = 0.18f; a = 1; };
                if (type == 0) {
                    if (line(0.55f, 0.1f) || line(width - 0.55f, 0.1f)) white();
                    bool dash = std::fmod(my, 12.0f) < 4.0f;
                    if (dash && (line(0.5f + 3.5f, 0.08f) || line(width - 0.5f - 3.5f, 0.08f))) white();
                    if (line(width * 0.5f - 0.18f, 0.07f) || line(width * 0.5f + 0.18f, 0.07f)) yellow();
                } else if (type == 1) {
                    if (line(0.35f, 0.08f) || line(width - 0.35f, 0.08f)) white();
                    if (std::fmod(my, 12.0f) < 4.5f && line(width * 0.5f, 0.08f)) white();
                } else if (type == 2) {
                    if (line(0.9f, 0.08f) || line(width - 0.9f, 0.08f)) white();
                    if (std::fmod(my, 6.0f) < 3.0f && line(width * 0.5f, 0.07f)) white();
                    // бордюр
                    if (mx < 0.35f || mx > width - 0.35f) { r = g = b = 0.55f; a = 0; }
                } else {
                    if (line(1.2f, 0.2f) || line(width - 1.2f, 0.2f)) white();
                    if (std::fmod(my, 60.0f) < 30.0f && line(width * 0.5f, 0.35f)) white();
                }
                if (a > 0) {
                    // износ разметки
                    float wear = tnoise(u * 30, v * 60, 30, 77 + type);
                    if (wear > 0.7f) { r = mixf(r, base, 0.5f); g = mixf(g, base, 0.5f); b = mixf(b, base, 0.5f); }
                }
            }
            im.set(x, y, r, g, b, a);
        }
    return im.upload();
}

// Атлас фасадов 2×2: 0 — дом, 1 — магазин, 2 — офис, 3 — стеклянная башня. alpha = окно
static Texture2D makeFacade() {
    const int S = 512, Q = 256;
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            int qx = x / Q, qy = y / Q, style = qy * 2 + qx;
            float u = (float)(x % Q) / Q, v = 1.0f - (float)(y % Q) / Q;  // v вверх
            float wall = 0.9f + (hash2f(x, y) - 0.5f) * 0.06f;
            float r = wall, g = wall, b = wall, a = 0;
            auto window = [&](float x0, float x1, float y0, float y1) { return u > x0 && u < x1 && v > y0 && v < y1; };
            if (style == 0) {
                // окна со ставнями: одна клетка = 4 м × 3.5 м
                if (window(0.3f, 0.7f, 0.3f, 0.78f)) { a = 1; r = 0.25f; g = 0.3f; b = 0.36f; if (std::fabs(u - 0.5f) < 0.015f || std::fabs(v - 0.55f) < 0.015f) { a = 0; r = g = b = 0.95f; } }
                if ((window(0.2f, 0.29f, 0.3f, 0.78f) || window(0.71f, 0.8f, 0.3f, 0.78f))) { r = 0.3f; g = 0.55f; b = 0.55f; }
                if (v < 0.06f) { r = g = b = 0.7f; }
            } else if (style == 1) {
                if (window(0.08f, 0.92f, 0.08f, 0.85f)) { a = 1; r = 0.22f; g = 0.28f; b = 0.32f; if (std::fabs(u - 0.5f) < 0.01f) { a = 0; r = g = b = 0.2f; } }
                if (v > 0.88f) { r = 0.8f; g = 0.2f; b = 0.2f; }
            } else if (style == 2) {
                if (window(0.06f, 0.94f, 0.25f, 0.85f)) { a = 1; r = 0.3f; g = 0.38f; b = 0.45f; }
                if (std::fabs(u - 0.5f) < 0.02f) { a = 0; r = g = b = 0.6f; }
            } else {
                a = 1;
                float refl = 0.35f + v * 0.2f + tnoise(u * 4, v * 4, 4, 3) * 0.1f;
                r = refl * 0.7f; g = refl * 0.85f; b = refl;
                if (u < 0.03f || u > 0.97f || v < 0.04f) { a = 0; r = g = b = 0.55f; }
            }
            im.set(x, y, r, g, b, a);
        }
    return im.upload(true, true);
}

static Texture2D makeRadial(int S, float power) {
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1;
            float d = std::sqrt(dx * dx + dy * dy);
            float a = std::pow(clamp01(1 - d), power);
            im.set(x, y, 1, 1, 1, a);
        }
    return im.upload(false, false);
}

static Texture2D makeSmoke(int S) {
    Img im(S, S);
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++) {
            float u = (float)x / S, v = (float)y / S;
            float dx = u * 2 - 1, dy = v * 2 - 1;
            float d = std::sqrt(dx * dx + dy * dy);
            float n = tfbm(u * 4, v * 4, 4, 4, 17);
            float a = clamp01((1 - d) * 1.6f) * (0.5f + n * 0.8f);
            im.set(x, y, 1, 1, 1, a * a);
        }
    return im.upload(false, false);
}

void TexSet::build(Font display, int quality) {
    int S = quality == 0 ? 256 : 512;
    grass = makeGround(S, 11, 0.24f, 0.38f, 0.13f, 0.42f, 0.56f, 0.22f, 0.25f, 1);
    rock = makeGround(S, 21, 0.36f, 0.34f, 0.31f, 0.58f, 0.55f, 0.5f, 0.3f, 2);
    sand = makeGround(S, 31, 0.78f, 0.7f, 0.52f, 0.92f, 0.85f, 0.68f, 0.15f, 3);
    gravel = makeGround(S, 41, 0.42f, 0.38f, 0.32f, 0.6f, 0.55f, 0.46f, 0.4f, 4);
    paved = makePaved(S);
    for (int t = 0; t < 5; t++) road[t] = makeRoad(t);
    facade = makeFacade();
    particle = makeSmoke(64);
    spark = makeRadial(32, 1.5f);
    glow = makeRadial(64, 2.2f);
    // баннер: логотип фестиваля
    {
        Image im = GenImageGradientLinear(512, 128, 90, Color{255, 120, 40, 255}, Color{230, 40, 120, 255});
        const char* t = "COASTLINE";
        Vector2 sz = MeasureTextEx(display, t, 84, 4);
        ImageDrawTextEx(&im, display, t, Vector2{(512 - sz.x) / 2, (128 - sz.y) / 2 - 6}, 84, 4, WHITE);
        const char* t2 = "ФЕСТИВАЛЬ";
        Vector2 s2 = MeasureTextEx(display, t2, 26, 6);
        ImageDrawTextEx(&im, display, t2, Vector2{(512 - s2.x) / 2, 96}, 26, 6, Color{255, 240, 200, 255});
        logo = LoadTextureFromImage(im);
        GenTextureMipmaps(&logo);
        SetTextureFilter(logo, TEXTURE_FILTER_TRILINEAR);
        UnloadImage(im);
    }
    Color fc[5] = {{255, 90, 40, 255}, {255, 205, 40, 255}, {40, 190, 255, 255}, {255, 255, 255, 255}, {150, 80, 255, 255}};
    for (int i = 0; i < 5; i++) {
        Image im = GenImageColor(64, 32, fc[i]);
        ImageDrawRectangle(&im, 0, 22, 64, 6, Color{255, 255, 255, 200});
        flag[i] = LoadTextureFromImage(im);
        UnloadImage(im);
    }
}

void TexSet::unload() {
    for (Texture2D* t : {&grass, &rock, &sand, &gravel, &paved, &facade, &particle, &spark, &glow, &logo}) UnloadTexture(*t);
    for (auto& t : road) UnloadTexture(t);
    for (auto& t : flag) UnloadTexture(t);
}

}  // namespace cl
