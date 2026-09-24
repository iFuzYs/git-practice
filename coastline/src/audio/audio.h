// Процедурный звук: мотор, шины, ветер, удары, интерфейс и фоновая музыка.
// Всё синтезируется в реальном времени — звуковых файлов в игре нет.
#pragma once
#include <atomic>
#include <cstdint>

namespace cl {

enum Sfx : uint8_t { SFX_UI_MOVE, SFX_UI_OK, SFX_UI_BACK, SFX_REWARD, SFX_BEEP, SFX_GO, SFX_IMPACT, SFX_SMASH, SFX_SHIFT, SFX_BACKFIRE, SFX_SPLASH, SFX_SKILL, SFX_LEVEL, SFX_CHECKPOINT, SFX_WHOOSH };

struct EngineSound {
    float rpm = 900, idle = 900, redline = 7000;
    float load = 0, throttle = 0;
    int cylinders = 4;
    float tone = 1;
    bool electric = false, turbo = false;
    float speed = 0;       // м/с
    float squeal = 0;      // 0..1
    float rumble = 0;      // грунт/трава
    float wind = 0;
    float water = 0;
    float volume = 1;      // приглушение в меню
};

class Audio {
public:
    bool init();
    void shutdown();
    void setVolumes(float master, float music, float sfx, bool musicOn);
    void setEngine(const EngineSound& e);
    void play(Sfx s, float strength = 1.0f);
    void setMusicIntensity(float k);  // 0 спокойно, 1 гонка
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

}  // namespace cl
