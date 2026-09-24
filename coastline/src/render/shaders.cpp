#include "shaders.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "rlgl.h"

namespace cl {

const int SLOT_SHADOW = 12, SLOT_DEPTHMAP = 13;

#if defined(PLATFORM_WEB)
static const char* HEAD = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
#else
static const char* HEAD = "#version 330\n";
#endif

// ------------------------------------------------------------------ вершинные
static const char* VS_STD = R"(
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec3 vPos;
out vec3 vN;
out vec2 vUV;
out vec4 vCol;
void main() {
    vec4 wp = matModel * vec4(vertexPosition, 1.0);
    vPos = wp.xyz;
    vN = normalize(mat3(matModel) * vertexNormal);
    vUV = vertexTexCoord;
    vCol = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* VS_INST = R"(
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexColor;
// один и тот же слот во всех шейдерах: иначе проходы делят VAO и портят друг другу атрибуты
layout(location = 10) in mat4 instanceTransform;
uniform mat4 mvp;
uniform float uTime;
out vec3 vPos;
out vec3 vN;
out vec2 vUV;
out vec4 vCol;
void main() {
    vec4 wp = instanceTransform * vec4(vertexPosition, 1.0);
    // покачивание листвы (помечена альфой 200..249)
    float a = vertexColor.a * 255.0;
    if (a > 199.5 && a < 249.5) {
        float h = max(vertexPosition.y, 0.0);
        float ph = dot(instanceTransform[3].xz, vec2(0.13, 0.17));
        wp.x += sin(uTime * 1.3 + ph) * 0.035 * h;
        wp.z += cos(uTime * 1.1 + ph * 1.3) * 0.03 * h;
    }
    vPos = wp.xyz;
    vN = normalize(mat3(instanceTransform) * vertexNormal);
    vUV = vertexTexCoord;
    vCol = vertexColor;
    gl_Position = mvp * wp;
}
)";

static const char* VS_DEPTH = R"(
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
static const char* VS_DEPTH_INST = R"(
in vec3 vertexPosition;
// один и тот же слот во всех шейдерах: иначе проходы делят VAO и портят друг другу атрибуты
layout(location = 10) in mat4 instanceTransform;
uniform mat4 mvp;
void main() { gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0); }
)";
static const char* FS_DEPTH = R"(
out vec4 finalColor;
void main() { finalColor = vec4(1.0); }
)";

// ------------------------------------------------------------------ общий свет
static const char* LIGHT = R"(
uniform vec3 uSunDir;
uniform vec3 uSunCol;
uniform vec3 uSkyCol;
uniform vec3 uGndCol;
uniform vec3 uFogCol;
uniform vec3 uFogSun;
uniform float uFogDen;
uniform vec3 uCamPos;
uniform float uNight;
uniform float uTime;
uniform float uExposure;
uniform sampler2D uShadow;
uniform mat4 uShadowVP;
uniform float uShadowOn;
uniform vec3 uHeadPos;
uniform vec3 uHeadDir;
uniform float uHeadOn;
uniform vec3 uPointPos[8];
uniform vec3 uPointCol[8];
uniform int uPointCount;
out vec4 finalColor;

vec3 toLin(vec3 c) { return pow(c, vec3(2.2)); }

float shadowFactor(vec3 wp, float bias) {
    if (uShadowOn < 0.5) return 1.0;
    vec4 lp = uShadowVP * vec4(wp, 1.0);
    vec3 p = lp.xyz / lp.w * 0.5 + 0.5;
    if (p.x <= 0.0 || p.x >= 1.0 || p.y <= 0.0 || p.y >= 1.0 || p.z >= 1.0) return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadow, 0));
    float s = 0.0;
    for (int i = -1; i <= 1; i++)
        for (int j = -1; j <= 1; j++) {
            float d = texture(uShadow, p.xy + vec2(float(i), float(j)) * texel * 1.2).r;
            s += (p.z - bias > d) ? 0.0 : 1.0;
        }
    s /= 9.0;
    vec2 e = min(p.xy, 1.0 - p.xy);
    return mix(1.0, s, clamp(min(e.x, e.y) * 14.0, 0.0, 1.0));
}

vec3 localLights(vec3 wp, vec3 n) {
    vec3 acc = vec3(0.0);
    if (uHeadOn > 0.01) {
        vec3 d = wp - uHeadPos;
        float dist = length(d);
        vec3 L = d / max(dist, 0.001);
        float cone = smoothstep(0.80, 0.96, dot(L, uHeadDir));
        float att = 1.0 / (1.0 + dist * dist * 0.011);
        acc += vec3(1.0, 0.94, 0.82) * cone * att * max(dot(n, -L), 0.0) * uHeadOn * 5.0;
    }
    for (int i = 0; i < 8; i++) {
        if (i >= uPointCount) break;
        vec3 d = uPointPos[i] - wp;
        float dist = length(d);
        float att = max(0.0, 1.0 - dist / 22.0);
        att *= att;
        acc += uPointCol[i] * att * max(dot(n, d / max(dist, 0.001)), 0.0) * 1.6;
    }
    return acc;
}

vec3 shade(vec3 albedo, vec3 n, vec3 wp, float sh, float specK, float gloss) {
    vec3 V = normalize(uCamPos - wp);
    float ndl = max(dot(n, uSunDir), 0.0);
    vec3 amb = mix(uGndCol, uSkyCol, n.y * 0.5 + 0.5);
    vec3 c = albedo * (amb + uSunCol * ndl * sh + localLights(wp, n));
    vec3 H = normalize(uSunDir + V);
    c += uSunCol * pow(max(dot(n, H), 0.0), gloss) * specK * sh;
    return c;
}

vec3 applyFog(vec3 c, vec3 wp) {
    vec3 d = wp - uCamPos;
    float dist = length(d);
    float f = 1.0 - exp(-dist * uFogDen);
    f *= clamp(exp(-(wp.y - 25.0) * 0.0045), 0.3, 1.0);
    vec3 dir = d / max(dist, 0.001);
    float sunAmt = pow(max(dot(dir, uSunDir), 0.0), 6.0) * (1.0 - uNight);
    return mix(c, mix(uFogCol, uFogSun, sunAmt), clamp(f, 0.0, 1.0));
}

vec3 outColor(vec3 x) {
    x *= uExposure;
    vec3 a = x * (2.51 * x + 0.03);
    vec3 b = x * (2.43 * x + 0.59) + 0.14;
    vec3 t = clamp(a / b, 0.0, 1.0);
    // лёгкая «тёплая» цветокоррекция в духе фестивального лета
    t = mix(t, t * vec3(1.04, 1.0, 0.95), 0.6);
    return pow(t, vec3(1.0 / 2.2));
}
)";

// ------------------------------------------------------------------ рельеф
static const char* FS_TERRAIN = R"(
in vec3 vPos;
in vec3 vN;
in vec2 vUV;
in vec4 vCol;
uniform sampler2D tGrass;
uniform sampler2D tRock;
uniform sampler2D tSand;
uniform sampler2D tGravel;
uniform sampler2D tPaved;
void main() {
    vec3 n = normalize(vN);
    vec2 uv = vPos.xz / 9.0;
    vec3 grass = texture(tGrass, uv).rgb;
    float big = texture(tGrass, vPos.xz / 173.0).g;
    grass *= mix(0.8, 1.18, big);
    grass = mix(grass, grass * vec3(0.72, 0.66, 0.5), vCol.b * 0.75);
    float slope = 1.0 - n.y;
    vec3 w = abs(n);
    w /= (w.x + w.y + w.z);
    vec3 rock = texture(tRock, vPos.zy / 13.0).rgb * w.x + texture(tRock, uv * 0.6).rgb * w.y + texture(tRock, vPos.xy / 13.0).rgb * w.z;
    float rk = smoothstep(0.17, 0.3, slope + (big - 0.5) * 0.12);
    rk = max(rk, smoothstep(165.0, 230.0, vPos.y) * 0.65);
    vec3 col = mix(grass, rock, rk);
    col = mix(col, texture(tSand, uv).rgb, vCol.g);
    col = mix(col, texture(tGravel, uv * 1.4).rgb, vCol.r * (1.0 - rk * 0.6));
    col = mix(col, texture(tPaved, vPos.xz / 8.0).rgb, vCol.a);
    float wet = 1.0 - smoothstep(0.0, 1.1, vPos.y);
    col *= mix(1.0, 0.62, wet);
    if (vPos.y < 0.0) col = mix(col, col * vec3(0.55, 0.75, 0.8), clamp(-vPos.y / 3.0, 0.0, 1.0));
    float sh = shadowFactor(vPos + n * 0.08, 0.0012);
    vec3 c = shade(toLin(col), n, vPos, sh, 0.04 + wet * 0.35 + vCol.a * 0.06, 20.0);
    finalColor = vec4(outColor(applyFog(c, vPos)), 1.0);
}
)";

// ------------------------------------------------------------------ дороги
static const char* FS_ROAD = R"(
in vec3 vPos;
in vec3 vN;
in vec2 vUV;
in vec4 vCol;
uniform sampler2D texture0;
void main() {
    vec3 n = normalize(vN);
    vec4 t = texture(texture0, vUV);
    vec3 col = t.rgb * vCol.rgb;
    float sh = shadowFactor(vPos + n * 0.05, 0.001);
    // разметка слегка светится в свете фар
    vec3 c = shade(toLin(col), n, vPos, sh, 0.12 + t.a * 0.2, 30.0);
    finalColor = vec4(outColor(applyFog(c, vPos)), 1.0);
}
)";

// ------------------------------------------------------------------ объекты
static const char* FS_OBJECT = R"(
in vec3 vPos;
in vec3 vN;
in vec2 vUV;
in vec4 vCol;
uniform sampler2D texture0;   // атлас фасадов
void main() {
    vec3 n = normalize(vN);
    if (!gl_FrontFacing) n = -n;
    float a = vCol.a * 255.0;
    vec3 col = vCol.rgb;
    float spec = 0.08, gloss = 16.0;
    vec3 emit = vec3(0.0);
    if (a < 99.5) {
        // фасад: стиль в альфе, окна в атласе
        float style = floor(a / 10.0 + 0.5) - 1.0;
        vec2 cell = vec2(mod(style, 2.0), floor(style / 2.0)) * 0.5;
        vec2 fuv = fract(vUV) * 0.47 + 0.015 + cell;
        vec4 f = texture(texture0, fuv);
        float win = f.a;
        col = mix(col * f.rgb, f.rgb * vec3(0.55, 0.62, 0.72), win);
        spec = mix(0.05, 0.6, win);
        gloss = mix(12.0, 80.0, win);
        // ночью часть окон светится
        vec2 wid = floor(vUV * vec2(2.0, 1.0));
        float r = fract(sin(dot(wid + floor(vPos.xz * 0.05), vec2(12.9898, 78.233))) * 43758.5453);
        float glassK = style > 2.5 ? 0.35 : 1.0;  // стеклянные башни светятся слабее
        if (r > 0.72) emit = vec3(1.0, 0.72, 0.42) * win * uNight * 0.32 * glassK * (0.5 + r * 0.5);
    } else if (a < 149.5) {
        emit = toLin(col) * 2.2;            // всегда светится
    } else if (a < 199.5) {
        emit = toLin(col) * 3.0 * uNight;   // светится ночью
        col *= 0.5;
    } else if (a < 249.5) {
        spec = 0.02;                         // листва
    }
    float sh = shadowFactor(vPos + n * 0.06, 0.0015);
    vec3 c = shade(toLin(col), n, vPos, sh, spec, gloss) + emit;
    finalColor = vec4(outColor(applyFog(c, vPos)), 1.0);
}
)";

// ------------------------------------------------------------------ кузов
static const char* FS_CAR = R"(
in vec3 vPos;
in vec3 vN;
in vec2 vUV;
in vec4 vCol;
uniform vec3 uPaint;
uniform float uLights;   // фары
uniform float uBrake;
uniform float uReverse;
uniform float uDirt;
uniform vec3 uZenith;
uniform vec3 uHorizon;
vec3 sky(vec3 d) {
    float h = clamp(d.y, -0.2, 1.0);
    vec3 c = mix(uHorizon, uZenith, pow(max(h, 0.0), 0.5));
    if (d.y < 0.0) c = mix(uHorizon * 0.55, uGndCol * 2.0, clamp(-d.y * 4.0, 0.0, 1.0));
    return c;
}
void main() {
    vec3 n = normalize(vN);
    vec3 V = normalize(uCamPos - vPos);
    int mat = int(vUV.x + 0.5);
    vec3 base;
    float spec = 0.3, gloss = 60.0, refl = 0.0;
    vec3 emit = vec3(0.0);
    if (mat == 0) {        // краска
        base = toLin(uPaint);
        refl = 0.28;
        spec = 1.2;
        gloss = 180.0;
        float flake = fract(sin(dot(floor(vPos * 70.0), vec3(12.9, 78.2, 37.7))) * 43758.5);
        base *= 0.92 + flake * 0.12;
    } else if (mat == 1) { // стекло
        base = vec3(0.01, 0.012, 0.016);
        refl = 0.3;
        spec = 1.5;
        gloss = 220.0;
    } else if (mat == 2) { // пластик и решётки
        base = toLin(vCol.rgb) * 0.6;
        spec = 0.15;
        gloss = 20.0;
    } else if (mat == 3) { // хром и диски
        base = vec3(0.55, 0.56, 0.58);
        refl = 0.7;
        spec = 1.4;
        gloss = 120.0;
    } else if (mat == 4) { // фары
        base = vec3(0.8, 0.82, 0.85);
        refl = 0.4;
        emit = vec3(1.0, 0.95, 0.85) * (0.15 + uLights * 3.5);
    } else if (mat == 5) { // задние фонари
        base = vec3(0.35, 0.02, 0.02);
        emit = vec3(1.0, 0.05, 0.03) * (0.08 + uLights * 0.9 + uBrake * 3.0);
    } else if (mat == 6) { // резина
        base = vec3(0.025);
        spec = 0.05;
        gloss = 8.0;
    } else if (mat == 7) { // задний ход
        base = vec3(0.8);
        emit = vec3(1.0) * uReverse * 2.0;
    } else {               // салон, днище
        base = toLin(vCol.rgb) * 0.4;
        spec = 0.05;
    }
    float sh = shadowFactor(vPos + n * 0.03, 0.0008);
    vec3 c = shade(base, n, vPos, sh, spec, gloss);
    float fres = pow(1.0 - max(dot(n, V), 0.0), 4.0);
    vec3 r = reflect(-V, n);
    c += sky(r) * (refl + fres * (refl > 0.0 ? 0.6 : 0.1)) * mix(1.0, 0.25, uNight) * (0.55 + 0.45 * sh);
    c = mix(c, c * vec3(0.62, 0.55, 0.45), uDirt * (mat == 0 ? 0.6 : 0.2));
    c += emit;
    finalColor = vec4(outColor(applyFog(c, vPos)), 1.0);
}
)";

// ------------------------------------------------------------------ вода
static const char* FS_WATER = R"(
in vec3 vPos;
in vec3 vN;
in vec2 vUV;
in vec4 vCol;
uniform sampler2D uDepthMap;
uniform vec3 uZenith;
uniform vec3 uHorizon;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float waves(vec2 p) {
    float t = uTime;
    float h = sin(dot(p, vec2(0.08, 0.05)) + t * 1.1) * 0.5;
    h += sin(dot(p, vec2(-0.05, 0.11)) + t * 1.4) * 0.35;
    h += vnoise(p * 0.35 + t * 0.2) * 0.6 + vnoise(p * 1.1 - t * 0.35) * 0.25;
    return h;
}
void main() {
    vec2 duv = (vPos.xz + 1536.0) / 3072.0;
    float depth = (duv.x < 0.0 || duv.y < 0.0 || duv.x > 1.0 || duv.y > 1.0) ? 1.0 : texture(uDepthMap, duv).r;
    float e = 0.35;
    float h0 = waves(vPos.xz);
    float hx = waves(vPos.xz + vec2(e, 0.0));
    float hz = waves(vPos.xz + vec2(0.0, e));
    float dist = length(vPos - uCamPos);
    float amp = mix(0.55, 0.12, clamp(dist / 700.0, 0.0, 1.0));
    vec3 n = normalize(vec3(-(hx - h0) / e * amp, 1.0, -(hz - h0) / e * amp));
    vec3 V = normalize(uCamPos - vPos);
    vec3 shallow = toLin(vec3(0.18, 0.72, 0.68)), deep = toLin(vec3(0.02, 0.2, 0.3));
    vec3 wc = mix(shallow, deep, smoothstep(0.0, 0.6, depth));
    // пресная вода в реке и озере темнее и зеленее
    wc *= (1.0 - uNight * 0.85);
    float sh = shadowFactor(vPos, 0.002);
    vec3 amb = mix(uGndCol, uSkyCol, 0.8);
    vec3 c = wc * (amb * 0.8 + uSunCol * max(dot(n, uSunDir), 0.0) * 0.35 * sh);
    vec3 r = reflect(-V, n);
    float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
    vec3 skyR = mix(uHorizon, uZenith, pow(clamp(r.y, 0.0, 1.0), 0.5)) * mix(1.0, 0.08, uNight);
    c = mix(c, skyR, fres * 0.85);
    vec3 H = normalize(uSunDir + V);
    c += uSunCol * pow(max(dot(n, H), 0.0), 320.0) * 3.0 * sh;
    c += localLights(vPos, n) * 0.3;
    float foam = smoothstep(0.035, 0.0, depth + (h0 - 0.6) * 0.012);
    c = mix(c, vec3(0.9) * (amb + uSunCol * 0.6), foam * 0.7);
    float alpha = clamp(mix(0.45, 0.97, smoothstep(0.0, 0.25, depth)) + fres * 0.2 + foam, 0.0, 1.0);
    finalColor = vec4(outColor(applyFog(c, vPos)), alpha);
}
)";

// ------------------------------------------------------------------ небо
static const char* VS_SKY = R"(
in vec3 vertexPosition;
out vec2 vNdc;
void main() {
    vNdc = vertexPosition.xy;
    gl_Position = vec4(vertexPosition.xy, 0.9999, 1.0);
}
)";
static const char* FS_SKY = R"(
in vec2 vNdc;
uniform mat4 uInvVP;
uniform vec3 uCam;
uniform vec3 uRealSun;
uniform vec3 uMoon;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uSunCol2;
uniform float uNight2;
uniform float uTime2;
uniform float uCloud;
uniform float uExposure2;
out vec4 finalColor;
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { s += vnoise(p) * a; p = p * 2.03 + 11.7; a *= 0.5; }
    return s;
}
void main() {
    vec4 p = uInvVP * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(p.xyz / p.w - uCam);
    float h = d.y;
    vec3 c = mix(uHorizon, uZenith, pow(clamp(h, 0.0, 1.0), 0.45));
    if (h < 0.0) c = uHorizon * mix(1.0, 0.75, clamp(-h * 6.0, 0.0, 1.0));
    float sd = max(dot(d, uRealSun), 0.0);
    // ореол и диск солнца
    c += uSunCol2 * pow(sd, 8.0) * 0.35 * (1.0 - uNight2);
    c += uSunCol2 * pow(sd, 64.0) * 0.8 * (1.0 - uNight2);
    c += vec3(1.0, 0.95, 0.85) * smoothstep(0.9993, 0.9997, sd) * 18.0 * (1.0 - uNight2);
    // облака
    if (h > 0.01) {
        vec2 cp = d.xz / (h + 0.08) * 1.8 + vec2(uTime2 * 0.006, uTime2 * 0.002);
        float n = fbm(cp);
        float cov = smoothstep(1.0 - uCloud * 0.9, 1.05 - uCloud * 0.55, n + 0.25);
        float lit = clamp(fbm(cp + uRealSun.xz * 0.12) * 1.4 - 0.2, 0.0, 1.0);
        vec3 cc = mix(uHorizon * 0.9 + uSunCol2 * 0.35 * (1.0 - uNight2), vec3(1.0) * (0.9 - uNight2 * 0.85), lit);
        cc += uSunCol2 * pow(sd, 5.0) * 0.6 * (1.0 - uNight2);
        c = mix(c, cc, cov * smoothstep(0.01, 0.12, h) * 0.95);
    }
    // звёзды и луна
    if (uNight2 > 0.01 && h > 0.0) {
        vec2 sp = floor(d.xz / (d.y + 0.3) * 260.0);
        float st = step(0.9975, hash(sp)) * (0.5 + 0.5 * sin(uTime2 * 3.0 + hash(sp + 3.0) * 30.0));
        c += vec3(st) * uNight2 * smoothstep(0.02, 0.3, h) * 1.2;
        float md = max(dot(d, uMoon), 0.0);
        c += vec3(0.9, 0.93, 1.0) * smoothstep(0.9990, 0.9994, md) * 2.5 * uNight2;
        c += vec3(0.3, 0.35, 0.5) * pow(md, 40.0) * 0.4 * uNight2;
    }
    vec3 x = c * uExposure2;
    vec3 a = x * (2.51 * x + 0.03);
    vec3 b = x * (2.43 * x + 0.59) + 0.14;
    vec3 t = clamp(a / b, 0.0, 1.0);
    t = mix(t, t * vec3(1.04, 1.0, 0.95), 0.6);
    finalColor = vec4(pow(t, vec3(1.0 / 2.2)), 1.0);
}
)";

// ------------------------------------------------------------------ частицы и ворота
static const char* FS_PARTICLE = R"(
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
out vec4 finalColor;
void main() {
    vec4 t = texture(texture0, fragTexCoord);
    finalColor = t * fragColor;
}
)";
static const char* VS_PARTICLE = R"(
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// ------------------------------------------------------------------ мини-карта
static const char* FS_MINIMAP = R"(
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform vec2 uCenter;   // в UV карты
uniform float uRot;
uniform float uScale;   // доля карты по радиусу
out vec4 finalColor;
void main() {
    vec2 q = fragTexCoord * 2.0 - 1.0;
    float r = length(q);
    if (r > 1.0) discard;
    float c = cos(uRot), s = sin(uRot);
    vec2 rq = vec2(q.x * c - q.y * s, q.x * s + q.y * c);
    vec2 uv = uCenter + rq * uScale;
    vec4 m = texture(texture0, uv);
    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) m = vec4(0.06, 0.2, 0.3, 1.0);
    float edge = smoothstep(0.93, 1.0, r);
    vec3 col = mix(m.rgb, vec3(0.04, 0.04, 0.06), edge * 0.85);
    finalColor = vec4(col, 0.92 * (1.0 - smoothstep(0.985, 1.0, r)));
}
)";

static Shader loadSh(const char* vs, const char* fs, bool light) {
    std::string v = std::string(HEAD) + vs;
    std::string f = std::string(HEAD) + (light ? LIGHT : "") + fs;
    return LoadShaderFromMemory(v.c_str(), f.c_str());
}

void Shaders::load() {
    terrain = loadSh(VS_STD, FS_TERRAIN, true);
    road = loadSh(VS_STD, FS_ROAD, true);
    object = loadSh(VS_STD, FS_OBJECT, true);
    objectInst = loadSh(VS_INST, FS_OBJECT, true);
    objectInst.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(objectInst, "instanceTransform");
    car = loadSh(VS_STD, FS_CAR, true);
    water = loadSh(VS_STD, FS_WATER, true);
    sky = loadSh(VS_SKY, FS_SKY, false);
    particle = loadSh(VS_PARTICLE, FS_PARTICLE, false);
    depth = loadSh(VS_DEPTH, FS_DEPTH, false);
    depthInst = loadSh(VS_DEPTH_INST, FS_DEPTH, false);
    depthInst.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocationAttrib(depthInst, "instanceTransform");
    minimap = LoadShaderFromMemory(nullptr, (std::string(HEAD) + FS_MINIMAP).c_str());
    // семплеры рельефа
    int slots[5] = {1, 2, 3, 4, 5};
    const char* names[5] = {"tGrass", "tRock", "tSand", "tGravel", "tPaved"};
    for (int i = 0; i < 5; i++) SetShaderValue(terrain, GetShaderLocation(terrain, names[i]), &slots[i], SHADER_UNIFORM_INT);
    int dm = SLOT_DEPTHMAP;
    SetShaderValue(water, GetShaderLocation(water, "uDepthMap"), &dm, SHADER_UNIFORM_INT);
    int ss = SLOT_SHADOW;
    for (Shader* s : {&terrain, &road, &object, &objectInst, &car, &water}) SetShaderValue(*s, GetShaderLocation(*s, "uShadow"), &ss, SHADER_UNIFORM_INT);
}

void Shaders::unload() {
    for (Shader* s : {&terrain, &road, &object, &objectInst, &car, &water, &sky, &particle, &depth, &depthInst, &minimap}) UnloadShader(*s);
}

// кэш расположений uniform-переменных: поиск по имени каждый кадр дорог (особенно в WebGL)
static int uloc(Shader s, const char* n) {
    static std::unordered_map<unsigned int, std::unordered_map<std::string, int>> cache;
    auto& m = cache[s.id];
    auto it = m.find(n);
    if (it != m.end()) return it->second;
    int l = GetShaderLocation(s, n);
    m[n] = l;
    return l;
}
static void setV3(Shader s, const char* n, V3 v) {
    float f[3] = {v.x, v.y, v.z};
    int loc = uloc(s, n);
    if (loc >= 0) SetShaderValue(s, loc, f, SHADER_UNIFORM_VEC3);
}
static void setF(Shader s, const char* n, float v) {
    int loc = uloc(s, n);
    if (loc >= 0) SetShaderValue(s, loc, &v, SHADER_UNIFORM_FLOAT);
}

void Shaders::setLight(const FrameLight& L) {
    for (Shader* sp : {&terrain, &road, &object, &objectInst, &car, &water}) {
        Shader s = *sp;
        setV3(s, "uSunDir", L.sunDir);
        setV3(s, "uSunCol", L.sunCol);
        setV3(s, "uSkyCol", L.skyCol);
        setV3(s, "uGndCol", L.gndCol);
        setV3(s, "uFogCol", L.fogCol);
        setV3(s, "uFogSun", L.fogSun);
        setF(s, "uFogDen", L.fogDen);
        setV3(s, "uCamPos", L.camPos);
        setF(s, "uNight", L.night);
        setF(s, "uTime", L.time);
        setF(s, "uExposure", L.exposure);
        setF(s, "uShadowOn", L.shadowOn ? 1.0f : 0.0f);
        int loc = uloc(s, "uShadowVP");
        if (loc >= 0) SetShaderValueMatrix(s, loc, L.shadowVP);
        setV3(s, "uHeadPos", L.headPos);
        setV3(s, "uHeadDir", L.headDir);
        setF(s, "uHeadOn", L.headOn);
        int pc = L.pointCount;
        int lc = uloc(s, "uPointCount");
        if (lc >= 0) SetShaderValue(s, lc, &pc, SHADER_UNIFORM_INT);
        if (pc > 0) {
            float pp[24], pcol[24];
            for (int i = 0; i < 8; i++) {
                pp[i * 3] = L.pointPos[i].x; pp[i * 3 + 1] = L.pointPos[i].y; pp[i * 3 + 2] = L.pointPos[i].z;
                pcol[i * 3] = L.pointCol[i].x; pcol[i * 3 + 1] = L.pointCol[i].y; pcol[i * 3 + 2] = L.pointCol[i].z;
            }
            int l1 = uloc(s, "uPointPos"), l2 = uloc(s, "uPointCol");
            if (l1 >= 0) SetShaderValueV(s, l1, pp, SHADER_UNIFORM_VEC3, 8);
            if (l2 >= 0) SetShaderValueV(s, l2, pcol, SHADER_UNIFORM_VEC3, 8);
        }
    }
    for (Shader* sp : {&car, &water}) {
        setV3(*sp, "uZenith", L.zenith);
        setV3(*sp, "uHorizon", L.horizon);
    }
    setV3(sky, "uRealSun", L.realSun);
    setV3(sky, "uMoon", L.moonDir);
    setV3(sky, "uZenith", L.zenith);
    setV3(sky, "uHorizon", L.horizon);
    setV3(sky, "uSunCol2", L.sunCol);
    setF(sky, "uNight2", L.night);
    setF(sky, "uTime2", L.time);
    setF(sky, "uCloud", L.cloud);
    setF(sky, "uExposure2", L.exposure);
    setV3(sky, "uCam", L.camPos);
    setF(objectInst, "uTime", L.time);
    if (L.shadowTex) bind(SLOT_SHADOW, L.shadowTex);
}

int shaderLoc(Shader s, const char* name) { return uloc(s, name); }

void Shaders::setSkyMatrix(const Matrix& invVP) {
    int loc = uloc(sky, "uInvVP");
    if (loc >= 0) SetShaderValueMatrix(sky, loc, invVP);
}

void Shaders::bind(int slot, unsigned int texId) {
    rlActiveTextureSlot(slot);
    rlEnableTexture(texId);
    rlActiveTextureSlot(0);
}

}  // namespace cl
