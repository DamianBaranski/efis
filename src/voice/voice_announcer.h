/// \file voice_announcer.h
/// Single speech queue shared by every navigation phrase.
#ifndef VOICE_ANNOUNCER_H
#define VOICE_ANNOUNCER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/// One installed voice the sound page can select.
struct VoiceChoice
{
    std::string id;
    std::string label;
};

/// Speaks short navigation phrases one at a time.
/// Desktop prefers Piper (`en_GB-alan-medium`) when the model is installed,
/// then speech-dispatcher or espeak-ng. Android uses the system UK voice.
class VoiceAnnouncer
{
public:
    /// Process-wide speaker.
    static VoiceAnnouncer &instance();

    /// priority 0 waits its turn. priority 1 or higher cuts off whatever is speaking.
    void say(std::string text, int priority = 0);

    /// Refreshes the installed-voice list.
    void pollVoices();
    /// Moves the selected voice by delta entries.
    void stepVoice(int delta);
    /// Label of the selected voice.
    std::string voiceLabel();

private:
    VoiceAnnouncer() = default;
    ~VoiceAnnouncer();
    VoiceAnnouncer(const VoiceAnnouncer &) = delete;
    VoiceAnnouncer &operator=(const VoiceAnnouncer &) = delete;

    struct Item
    {
        std::string text;
        int priority = 0;
    };

    void loop();
    void prepare();
    void speak(const Item &item);
    void interrupt();
    bool speakDesktop(const std::string &text, int epoch, int priority);
    bool speakAndroid(const std::string &text, bool flush);
    void reloadVoices();
    void applyVoice(const std::string &id);
    std::string voiceId();

    std::mutex mMutex;
    std::condition_variable mCv;
    std::deque<Item> mQueue;
    std::thread mThread;
    std::string mSpeaking;
    std::atomic<bool> mStarted{false};
    std::atomic<bool> mStop{false};
    std::atomic<int> mEpoch{0};
    std::atomic<int> mPid{0};
    bool mPrepared = false;
    bool mAndroidReady = false;
    std::mutex mVoiceMutex;
    std::vector<VoiceChoice> mVoices;
    int mVoiceIndex = 0;
    std::string mAppliedId;
    uint32_t mVoicePollMs = 0;
};
#endif
