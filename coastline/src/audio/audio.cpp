#include "audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "raylib.h"

namespace cl {

namespace {

const float SR = 44100.0f;
const float TWO_PI = 6.28318530718f;

AudioStream g_stream{};

// Параметры из игрового потока (атомарные, без блокировок)
std::atomic<float> aRpm{900}, aIdle{900}, aRed{7000}, aLoad{0}, aThr{0}, aTone{1}, aSpeed{0}, aSqueal{0}, aRumble{0}, aWind{0}, aWater{0}, aEngVol{0};
std::atomic<int> aCyl{4};
std::atomic<bool> aElectric{false}, aTurbo{false};
std::atomic<float> aMaster{0.8f}, aMusic{0.5f}, aSfx{0.85f}, aIntensity{0};
std::atomic<bool> aMusicOn{true};

// Очередь звуковых событий: один писатель, один читатель
struct Cmd { uint8_t sfx; float strength; };
Cmd g_q[64];
std::atomic<uint32_t> g_qHead{0}, g_qTail{0};

uint32_t g_seed = 12345;
inline float noise() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return ((g_seed >> 9) & 0x7fffff) / 4194304.0f - 1.0f;
}

struct OnePole {
    float y = 0;
    float lp(float x, float cutoff) {
        float a = 1.0f - std::exp(-TWO_PI * cutoff / SR);
        y += (x - y) * a;
        return y;
    }
};

// --- голоса эффектов
struct Voice {
    bool on = false;
    uint8_t kind = 0;
    float t = 0, dur = 0.2f, amp = 1, f0 = 440, f1 = 440, ph = 0;
    OnePole f;
};
Voice g_voices[24];

void startVoice(uint8_t kind, float strength) {
    Voice* v = nullptr;
    for (auto& c : g_voices)
        if (!c.on) { v = &c; break; }
    if (!v) v = &g_voices[0];
    *v = Voice{};
    v->on = true;
    v->kind = kind;
    v->amp = strength;
    switch (kind) {
        case SFX_UI_MOVE: v->dur = 0.05f; v->f0 = v->f1 = 1320; v->amp *= 0.25f; break;
        case SFX_UI_OK: v->dur = 0.16f; v->f0 = 880; v->f1 = 1320; v->amp *= 0.3f; break;
        case SFX_UI_BACK: v->dur = 0.14f; v->f0 = 660; v->f1 = 440; v->amp *= 0.3f; break;
        case SFX_REWARD: v->dur = 0.9f; v->f0 = 523; v->amp *= 0.35f; break;
        case SFX_BEEP: v->dur = 0.22f; v->f0 = v->f1 = 660; v->amp *= 0.35f; break;
        case SFX_GO: v->dur = 0.6f; v->f0 = v->f1 = 1320; v->amp *= 0.35f; break;
        case SFX_IMPACT: v->dur = 0.35f; v->amp *= 0.9f; break;
        case SFX_SMASH: v->dur = 0.3f; v->amp *= 0.7f; break;
        case SFX_SHIFT: v->dur = 0.07f; v->amp *= 0.35f; break;
        case SFX_BACKFIRE: v->dur = 0.09f; v->amp *= 0.6f; break;
        case SFX_SPLASH: v->dur = 0.6f; v->amp *= 0.5f; break;
        case SFX_SKILL: v->dur = 0.25f; v->f0 = 1046; v->f1 = 1568; v->amp *= 0.2f; break;
        case SFX_LEVEL: v->dur = 1.3f; v->f0 = 392; v->amp *= 0.35f; break;
        case SFX_CHECKPOINT: v->dur = 0.3f; v->f0 = 988; v->f1 = 1480; v->amp *= 0.3f; break;
        case SFX_WHOOSH: v->dur = 0.5f; v->amp *= 0.4f; break;
    }
}

float runVoice(Voice& v) {
    float t = v.t / v.dur;
    if (t >= 1.0f) { v.on = false; return 0; }
    float s = 0;
    float env = (1.0f - t);
    switch (v.kind) {
        case SFX_UI_MOVE: case SFX_UI_OK: case SFX_UI_BACK: case SFX_SKILL: case SFX_CHECKPOINT: case SFX_BEEP: case SFX_GO: {
            float f = v.f0 + (v.f1 - v.f0) * std::min(t * 3.0f, 1.0f);
            v.ph += f / SR;
            s = std::sin(TWO_PI * v.ph) * 0.8f + std::sin(TWO_PI * v.ph * 2.0f) * 0.15f;
            env = std::min(v.t * 300.0f, 1.0f) * std::pow(1.0f - t, 1.5f);
            break;
        }
        case SFX_REWARD: case SFX_LEVEL: {
            // арпеджио мажорного аккорда
            static const float mul[4] = {1.0f, 1.26f, 1.5f, 2.0f};
            int k = std::min((int)(t * 4), 3);
            float f = v.f0 * mul[k];
            v.ph += f / SR;
            s = std::sin(TWO_PI * v.ph) * 0.7f + std::sin(TWO_PI * v.ph * 3.0f) * 0.12f;
            float lt = t * 4 - k;
            env = std::min(lt * 60.0f, 1.0f) * std::exp(-lt * 3.0f) * (1.0f - t * 0.5f);
            break;
        }
        case SFX_IMPACT: {
            float n = v.f.lp(noise(), 900.0f * (1.0f - t) + 120.0f);
            v.ph += (60.0f + 40.0f * (1 - t)) / SR;
            s = n * 1.4f + std::sin(TWO_PI * v.ph) * 0.8f * (1.0f - t);
            env = std::exp(-t * 7.0f);
            break;
        }
        case SFX_SMASH: {
            float n = v.f.lp(noise(), 2200.0f);
            s = n * (0.6f + 0.4f * std::sin(v.t * 900.0f));
            env = std::exp(-t * 6.0f);
            break;
        }
        case SFX_SHIFT: s = v.f.lp(noise(), 1800.0f) * 0.9f; env = std::exp(-t * 5.0f); break;
        case SFX_BACKFIRE: {
            v.ph += 70.0f / SR;
            s = v.f.lp(noise(), 600.0f) * 1.5f + std::sin(TWO_PI * v.ph) * 0.6f;
            env = std::exp(-t * 9.0f);
            break;
        }
        case SFX_SPLASH: s = v.f.lp(noise(), 1400.0f * (1.0f - t) + 300.0f); env = std::sin(t * 3.14159f) * (1.0f - t); break;
        case SFX_WHOOSH: s = v.f.lp(noise(), 400.0f + 1600.0f * std::sin(t * 3.14159f)); env = std::sin(t * 3.14159f); break;
    }
    v.t += 1.0f / SR;
    return s * env * v.amp;
}

// --- мотор
struct EngineState {
    double ph = 0, phT = 0, phW = 0;
    OnePole body, bright, sq1, sq2, wind, rumble, water;
    float rpmS = 900, loadS = 0, lastThr = 0, blowoff = 0;
} g_eng;

float runEngine() {
    EngineState& e = g_eng;
    float vol = aEngVol.load(std::memory_order_relaxed);
    float rpm = aRpm.load(std::memory_order_relaxed), load = aLoad.load(std::memory_order_relaxed);
    float thr = aThr.load(std::memory_order_relaxed);
    e.rpmS += (rpm - e.rpmS) * 0.004f;
    e.loadS += (load - e.loadS) * 0.002f;
    int cyl = aCyl.load(std::memory_order_relaxed);
    float tone = aTone.load(std::memory_order_relaxed);
    float out = 0;
    if (aElectric.load(std::memory_order_relaxed)) {
        float f = e.rpmS * 0.11f + 40.0f;
        e.ph += f / SR;
        float p = (float)(e.ph - std::floor(e.ph));
        float s = std::sin(TWO_PI * p) * 0.5f + std::sin(TWO_PI * p * 2.0f) * 0.25f + std::sin(TWO_PI * p * 3.01f) * 0.12f * e.loadS;
        s += e.bright.lp(noise(), 3000.0f) * 0.05f;
        out = s * (0.25f + e.loadS * 0.35f);
    } else {
        // частота вспышек: обороты/60 × цилиндры/2
        float f = e.rpmS / 60.0f * (float)cyl * 0.5f * tone;
        e.ph += f / SR;
        float p = (float)(e.ph - std::floor(e.ph));
        float half = (float)(e.ph * 0.5 - std::floor(e.ph * 0.5));
        // импульс вспышки: быстро нарастает, медленно спадает
        float pulse = std::exp(-p * (6.0f - e.loadS * 2.0f)) - 0.3f;
        float s = pulse * 0.9f;
        s += std::sin(TWO_PI * p) * 0.45f;
        s += std::sin(TWO_PI * p * 2.0f + 0.4f) * (0.18f + 0.25f * e.loadS);
        s += std::sin(TWO_PI * p * 3.0f + 1.1f) * (0.06f + 0.18f * e.loadS);
        if (cyl >= 8) s += std::sin(TWO_PI * half) * 0.35f;  // «бормотание» V8
        if (cyl <= 4) s += std::sin(TWO_PI * p * 0.5f) * 0.12f;
        float comb = noise() * std::exp(-p * 10.0f) * (0.25f + e.loadS * 0.5f);
        s += comb;
        float cutoff = 380.0f + e.rpmS * 0.28f + e.loadS * 1500.0f;
        s = e.body.lp(s, cutoff);
        out = s * (0.35f + e.loadS * 0.5f);
        if (aTurbo.load(std::memory_order_relaxed)) {
            e.phT += (e.rpmS * 1.1f + 900.0f) / SR;
            out += std::sin(TWO_PI * (float)(e.phT - std::floor(e.phT))) * 0.035f * e.loadS * (e.rpmS / 7000.0f);
            if (e.lastThr > 0.6f && thr < 0.2f && e.rpmS > 3500) e.blowoff = 1.0f;
            if (e.blowoff > 0) {
                out += e.bright.lp(noise(), 4000.0f) * e.blowoff * 0.25f;
                e.blowoff -= 1.0f / (SR * 0.35f);
            }
        }
    }
    e.lastThr = thr;
    out *= vol;
    // шины
    float sq = aSqueal.load(std::memory_order_relaxed);
    if (sq > 0.01f) {
        float n = noise();
        float b = e.sq1.lp(n, 1400.0f) - e.sq2.lp(n, 700.0f);
        e.phW += (620.0f + sq * 180.0f) / SR;
        float trem = 0.75f + 0.25f * std::sin(TWO_PI * (float)(e.phW - std::floor(e.phW)));
        out += b * 2.2f * sq * trem * vol;
    }
    // ветер и покрытие
    float wind = aWind.load(std::memory_order_relaxed);
    out += e.wind.lp(noise(), 300.0f + wind * 900.0f) * wind * 0.5f * vol;
    float rum = aRumble.load(std::memory_order_relaxed);
    out += e.rumble.lp(noise(), 140.0f) * rum * 1.3f * vol;
    float wat = aWater.load(std::memory_order_relaxed);
    out += e.water.lp(noise(), 900.0f) * wat * 0.6f * vol;
    return out;
}

// --- музыка: спокойный фестивальный синти-поп, 100 уд/мин, Am–F–C–G
struct Music {
    double t = 0;
    double ph[16] = {};
    OnePole pad, bass, hat, lead;
    float kickPh = 0;
} g_mus;

float g_noteHz[128];
bool g_noteInit = false;
inline float noteHz(int midi) {
    if (!g_noteInit) {
        for (int i = 0; i < 128; i++) g_noteHz[i] = 440.0f * std::pow(2.0f, (i - 69) / 12.0f);
        g_noteInit = true;
    }
    return g_noteHz[midi & 127];
}
inline float saw(double& ph, float f) {
    ph += f / SR;
    ph -= std::floor(ph);
    return (float)(ph * 2.0 - 1.0);
}
inline float sine(double& ph, float f) {
    ph += f / SR;
    ph -= std::floor(ph);
    return std::sin(TWO_PI * (float)ph);
}

float runMusic() {
    Music& m = g_mus;
    const float bpm = 100.0f;
    double beat = m.t * bpm / 60.0;
    int bar = (int)(beat / 4.0);
    float inBeat = (float)(beat - std::floor(beat));
    int beatN = (int)beat % 4;
    static const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {48, 52, 55}, {55, 59, 62}};
    static const int roots[4] = {45, 41, 48, 43};
    const int* ch = chords[bar % 4];
    float intensity = aIntensity.load(std::memory_order_relaxed);
    // пэд
    float pad = 0;
    for (int i = 0; i < 3; i++) {
        pad += saw(m.ph[i], noteHz(ch[i]) * 1.003f) + saw(m.ph[i + 3], noteHz(ch[i]) * 0.997f);
    }
    pad = m.pad.lp(pad * 0.12f, 900.0f + 500.0f * std::sin((float)m.t * 0.3f));
    // бас восьмыми
    float eighth = (float)(beat * 2.0 - std::floor(beat * 2.0));
    float bassEnv = std::exp(-eighth * 4.0f);
    float bs = sine(m.ph[6], noteHz(roots[bar % 4])) * 0.8f + saw(m.ph[7], noteHz(roots[bar % 4])) * 0.2f;
    bs = m.bass.lp(bs, 500.0f) * bassEnv * 0.55f;
    // бочка на каждую долю, малый на 2 и 4, хэт восьмыми
    float kick = 0;
    {
        float kf = 50.0f + 120.0f * std::exp(-inBeat * 30.0f);
        m.kickPh += kf / SR;
        kick = std::sin(TWO_PI * m.kickPh) * std::exp(-inBeat * 9.0f) * 0.9f;
    }
    float snare = 0;
    if (beatN == 1 || beatN == 3) snare = noise() * std::exp(-inBeat * 14.0f) * 0.35f;
    float hatN = noise();
    float hat = (hatN - m.hat.lp(hatN, 6000.0f)) * std::exp(-eighth * 30.0f) * 0.18f;
    // арпеджио
    static const int arp[8] = {0, 1, 2, 1, 0, 2, 1, 2};
    int step = (int)(beat * 2.0) % 8;
    float leadEnv = std::exp(-eighth * 6.0f);
    float ld = sine(m.ph[8], noteHz(ch[arp[step]] + 12)) * 0.6f + sine(m.ph[9], noteHz(ch[arp[step]] + 24)) * 0.15f;
    ld = m.lead.lp(ld, 2500.0f) * leadEnv * 0.22f;
    float drums = (kick + snare + hat) * (0.55f + 0.45f * intensity);
    m.t += 1.0 / SR;
    if (m.t > 2400.0) m.t -= 2400.0;  // 1000 тактов — цикл аккордов не рвётся
    return (pad + bs + drums + ld * (0.6f + 0.4f * intensity)) * 0.5f;
}

void callback(void* buffer, unsigned int frames) {
    float* out = (float*)buffer;
    // забираем события
    uint32_t head = g_qHead.load(std::memory_order_acquire);
    uint32_t tail = g_qTail.load(std::memory_order_relaxed);
    while (tail != head) {
        const Cmd& c = g_q[tail % 64];
        startVoice(c.sfx, c.strength);
        tail++;
    }
    g_qTail.store(tail, std::memory_order_release);
    float master = aMaster.load(), sfx = aSfx.load(), mus = aMusicOn.load() ? aMusic.load() : 0.0f;
    for (unsigned int i = 0; i < frames; i++) {
        float s = runEngine() * sfx;
        float fx = 0;
        for (auto& v : g_voices)
            if (v.on) fx += runVoice(v);
        s += fx * sfx;
        if (mus > 0.001f) s += runMusic() * mus * 0.6f;
        s *= master;
        // мягкое ограничение
        s = s / (1.0f + std::fabs(s));
        out[i] = s;
    }
}

}  // namespace

bool Audio::init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return false;
    SetAudioStreamBufferSizeDefault(2048);
    g_stream = LoadAudioStream((unsigned int)SR, 32, 1);
    SetAudioStreamCallback(g_stream, callback);
    PlayAudioStream(g_stream);
    ok_ = true;
    return true;
}

void Audio::shutdown() {
    if (!ok_) return;
    StopAudioStream(g_stream);
    UnloadAudioStream(g_stream);
    CloseAudioDevice();
    ok_ = false;
}

void Audio::setVolumes(float master, float music, float sfx, bool musicOn) {
    aMaster = master;
    aMusic = music;
    aSfx = sfx;
    aMusicOn = musicOn;
}

void Audio::setEngine(const EngineSound& e) {
    aRpm.store(e.rpm, std::memory_order_relaxed);
    aIdle.store(e.idle, std::memory_order_relaxed);
    aRed.store(e.redline, std::memory_order_relaxed);
    aLoad.store(e.load, std::memory_order_relaxed);
    aThr.store(e.throttle, std::memory_order_relaxed);
    aTone.store(e.tone, std::memory_order_relaxed);
    aCyl.store(e.cylinders, std::memory_order_relaxed);
    aElectric.store(e.electric, std::memory_order_relaxed);
    aTurbo.store(e.turbo, std::memory_order_relaxed);
    aSpeed.store(e.speed, std::memory_order_relaxed);
    aSqueal.store(e.squeal, std::memory_order_relaxed);
    aRumble.store(e.rumble, std::memory_order_relaxed);
    aWind.store(e.wind, std::memory_order_relaxed);
    aWater.store(e.water, std::memory_order_relaxed);
    aEngVol.store(e.volume, std::memory_order_relaxed);
}

void Audio::play(Sfx s, float strength) {
    if (!ok_) return;
    uint32_t head = g_qHead.load(std::memory_order_relaxed);
    uint32_t tail = g_qTail.load(std::memory_order_acquire);
    if (head - tail >= 64) return;
    g_q[head % 64] = {s, strength};
    g_qHead.store(head + 1, std::memory_order_release);
}

void Audio::setMusicIntensity(float k) { aIntensity = k; }

}  // namespace cl
