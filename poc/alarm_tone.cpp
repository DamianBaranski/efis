/// \file alarm_tone.cpp
/// Sine and triangle chords for GPWS. Each pulse swells, then fades to silence.
#include "alarm_tone.h"
#include "sdl_compat.h"

#include <atomic>
#include <cmath>
#include <iostream>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr int kRate = 44100;
constexpr int kVoices = 4;
constexpr int kCautionAttack = 3087;
constexpr int kCautionFade = 28665;
constexpr int kCautionPeriod = 44100;
constexpr int kWarningAttack = 1764;
constexpr int kWarningFade = 16758;
constexpr int kWarningPeriod = 25578;
constexpr int kStopFade = 15435;
constexpr int kDemoCautions = 1;
constexpr int kDemoWarnings = 2;

float midiHz(int note, float cents)
{
    return 440.0f * std::pow(2.0f, (static_cast<float>(note) + cents / 100.0f - 69.0f) / 12.0f);
}

float sineAt(float phase)
{
    return std::sin(phase * 2.0f * kPi);
}

float triangleAt(float phase)
{
    return 4.0f * std::fabs(phase - 0.5f) - 1.0f;
}

/// Sine keeps the body. Triangle adds the odd harmonics that make the chord brighter.
float toneAt(float phase, float triangle)
{
    return sineAt(phase) * (1.0f - triangle) + triangleAt(phase) * triangle;
}

struct Partial
{
    float hz = 0.0f;
    float gain = 0.0f;
    float triangle = 0.0f;
};

struct Chord
{
    Partial part[kVoices];
};

/// Major 9th, or dominant 7 with a sharp 9 when warning is set.
/// The root carries enough triangle to stay firm. The ninth stays the bright edge.
Chord buildChord(int root, bool warning)
{
    if (root < 60)
    {
        root += 12;
    }
    const int interval[kVoices] = {0, 4, warning ? 10 : 11, warning ? 15 : 14};
    const float gain[kVoices] = {0.46f, 0.26f, 0.26f, 0.16f};
    const float cents[kVoices] = {0.0f, 0.0f, 0.0f, 6.0f};
    const float triangle[kVoices] = {0.48f, 0.50f, warning ? 0.55f : 0.42f, warning ? 0.72f : 0.58f};
    Chord chord;
    for (int i = 0; i < kVoices; ++i)
    {
        chord.part[i].hz = midiHz(root + interval[i], cents[i]);
        chord.part[i].gain = gain[i];
        chord.part[i].triangle = triangle[i];
    }
    return chord;
}

int rootFor(int chord)
{
    switch (static_cast<AlarmChord>(chord))
    {
    case AlarmChord::Terrain:
        return 60;
    case AlarmChord::Obstacle:
        return 62;
    case AlarmChord::Closure:
        return 65;
    case AlarmChord::Sink:
        return 55;
    case AlarmChord::TooLow:
        return 58;
    case AlarmChord::DontSink:
        return 57;
    case AlarmChord::FiveHundred:
        return 64;
    case AlarmChord::Bank:
        return 63;
    case AlarmChord::None:
        break;
    }
    return 60;
}

/// Smooth 0 at the start and 1 at the end, with a flat derivative at both.
float bloom(float u)
{
    if (u <= 0.0f)
    {
        return 0.0f;
    }
    if (u >= 1.0f)
    {
        return 1.0f;
    }
    return 0.5f - 0.5f * std::cos(kPi * u);
}

/// Rise, then a long fall to silence. The quiet gap is the rest of the period.
float pulse(int level, int sample)
{
    const bool warning = level >= 2;
    const int period = warning ? kWarningPeriod : kCautionPeriod;
    const int attack = warning ? kWarningAttack : kCautionAttack;
    const int fade = warning ? kWarningFade : kCautionFade;
    const int at = sample % period;
    if (at < attack)
    {
        return bloom(static_cast<float>(at) / static_cast<float>(attack));
    }
    const int fall = at - attack;
    if (fall < fade)
    {
        return 1.0f - bloom(static_cast<float>(fall) / static_cast<float>(fade));
    }
    return 0.0f;
}

struct Player
{
    std::atomic<unsigned> command{0};
    std::atomic<unsigned> stingArm{0};
    std::atomic<unsigned> stingChord{0};
    std::atomic<unsigned> demoArm{0};

    float phase[kVoices] = {0.05f, 0.27f, 0.51f, 0.74f};
    float env = 0.0f;
    Chord voiced{};
    int chord = 0;
    int level = 0;
    int voicedChord = -1;
    int voicedLevel = -1;
    int cycle = 0;
    int demoLeft = 0;
    int stingLeft = 0;
    int stingId = 0;
    int stopAt = 0;
    float stopFrom = 0.0f;
    bool stopping = false;

    void render(float *out, int frames)
    {
        const unsigned packed = command.load(std::memory_order_relaxed);
        int liveChord = static_cast<int>(packed & 0xffu);
        int liveLevel = static_cast<int>((packed >> 8) & 0xffu);
        if (liveChord == 0 || liveLevel <= 0)
        {
            liveChord = 0;
            liveLevel = 0;
        }

        if (demoArm.exchange(0, std::memory_order_relaxed) != 0 && liveLevel == 0)
        {
            demoLeft = kCautionPeriod * kDemoCautions + kWarningPeriod * kDemoWarnings;
        }
        if (stingArm.exchange(0, std::memory_order_relaxed) != 0)
        {
            if (liveLevel == 0 && demoLeft == 0)
            {
                stingId = static_cast<int>(stingChord.load(std::memory_order_relaxed));
                stingLeft = kCautionPeriod;
            }
        }

        for (int i = 0; i < frames; ++i)
        {
            int chordNow = liveChord;
            int levelNow = liveLevel;
            if (liveLevel == 0 && demoLeft > 0)
            {
                const int warningStart = kCautionPeriod * kDemoCautions;
                const int into = (warningStart + kWarningPeriod * kDemoWarnings) - demoLeft;
                chordNow = static_cast<int>(AlarmChord::Terrain);
                levelNow = into < warningStart ? 1 : 2;
                --demoLeft;
            }
            else if (liveLevel == 0 && stingLeft > 0)
            {
                chordNow = stingId;
                levelNow = 1;
                --stingLeft;
            }
            else
            {
                demoLeft = 0;
                stingLeft = 0;
            }

            if (chordNow != chord || levelNow != level)
            {
                chord = chordNow;
                level = levelNow;
                cycle = 0;
            }

            if (chordNow != 0 && (chordNow != voicedChord || levelNow != voicedLevel))
            {
                voiced = buildChord(rootFor(chordNow), levelNow >= 2);
                voicedChord = chordNow;
                voicedLevel = levelNow;
            }

            float target = 0.0f;
            if (levelNow > 0)
            {
                stopping = false;
                target = pulse(levelNow, cycle);
            }
            else if (env > 0.0008f)
            {
                if (!stopping)
                {
                    stopping = true;
                    stopFrom = env;
                    stopAt = 0;
                }
                if (stopAt < kStopFade)
                {
                    const float u = static_cast<float>(stopAt) / static_cast<float>(kStopFade);
                    target = stopFrom * (1.0f - bloom(u));
                    ++stopAt;
                }
            }
            else
            {
                stopping = false;
            }
            env += (target - env) * 0.02f;
            float sample = 0.0f;
            if (env > 0.0008f && voicedChord > 0)
            {
                const float master = voicedLevel >= 2 ? 0.55f : 0.40f;
                for (int v = 0; v < kVoices; ++v)
                {
                    sample += toneAt(phase[v], voiced.part[v].triangle) * voiced.part[v].gain;
                    phase[v] += voiced.part[v].hz / static_cast<float>(kRate);
                    if (phase[v] >= 1.0f)
                    {
                        phase[v] -= std::floor(phase[v]);
                    }
                }
                sample = std::tanh(sample * env * master);
            }
            out[i] = sample;
            ++cycle;
        }
    }
};

Player &player()
{
    static Player tone;
    return tone;
}
}

AlarmTone &AlarmTone::instance()
{
    static AlarmTone tone;
    return tone;
}

void AlarmTone::open()
{
    if (mDevice != 0 || mFailed)
    {
        return;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
    {
        mFailed = true;
        std::cerr << "GPWS tone: audio init failed: " << SDL_GetError() << std::endl;
        return;
    }
    SDL_AudioSpec want{};
    want.freq = kRate;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = &AlarmTone::callback;
    want.userdata = this;
    mDevice = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (mDevice == 0)
    {
        mFailed = true;
        std::cerr << "GPWS tone: device open failed: " << SDL_GetError() << std::endl;
        return;
    }
    SDL_PauseAudioDevice(mDevice, 0);
}

void AlarmTone::set(AlarmChord chord, int level)
{
    if (chord == AlarmChord::None || level <= 0)
    {
        player().command.store(0, std::memory_order_relaxed);
        return;
    }
    open();
    const unsigned packed = static_cast<unsigned>(chord) | (static_cast<unsigned>(level) << 8);
    player().command.store(packed, std::memory_order_relaxed);
}

void AlarmTone::sting(AlarmChord chord)
{
    if (chord == AlarmChord::None)
    {
        return;
    }
    open();
    player().stingChord.store(static_cast<unsigned>(chord), std::memory_order_relaxed);
    player().stingArm.store(1, std::memory_order_relaxed);
}

void AlarmTone::demo()
{
    open();
    player().demoArm.store(1, std::memory_order_relaxed);
}

void AlarmTone::callback(void *userdata, unsigned char *stream, int length)
{
    (void)userdata;
    auto *out = reinterpret_cast<float *>(stream);
    const int frames = length / static_cast<int>(sizeof(float));
    if (frames > 0)
    {
        player().render(out, frames);
    }
}
