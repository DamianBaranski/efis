/// \file voice_announcer.cpp
/// Speaks one phrase at a time on Piper, speech-dispatcher, or Android TTS.
#include "voice_announcer.h"
#include "asset_path.h"
#include "sdl_compat.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <pthread.h>
#include <sstream>
#include <unistd.h>
#ifdef __ANDROID__
#include <jni.h>
#else
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <vector>
extern char **environ;
#endif

namespace
{
#ifndef __ANDROID__
bool fileExists(const std::string &path)
{
    struct stat info{};
    return !path.empty() && stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

bool onPath(const char *name)
{
    const char *path = std::getenv("PATH");
    if (!path || !name || !name[0])
    {
        return false;
    }
    std::string dir;
    for (const char *p = path; *p; ++p)
    {
        if (*p == ':')
        {
            if (fileExists(dir + "/" + name))
            {
                return true;
            }
            dir.clear();
        }
        else
        {
            dir.push_back(*p);
        }
    }
    return fileExists(dir + "/" + name);
}

int sampleRateOf(const std::string &model)
{
    std::ifstream in(model + ".json");
    if (!in)
    {
        return 22050;
    }
    std::string blob((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto pos = blob.find("\"sample_rate\"");
    if (pos == std::string::npos)
    {
        return 22050;
    }
    const auto colon = blob.find(':', pos);
    if (colon == std::string::npos)
    {
        return 22050;
    }
    return std::max(8000, std::atoi(blob.c_str() + colon + 1));
}

std::string piperModel()
{
    if (const char *env = std::getenv("EFIS_PIPER_MODEL"))
    {
        if (fileExists(env))
        {
            return env;
        }
    }
    const std::string packed = AssetPath::resolve("resources/voice/en_GB-alan-medium.onnx");
    if (fileExists(packed))
    {
        return packed;
    }
    if (const char *home = std::getenv("HOME"))
    {
        const std::string local = std::string(home) + "/.local/share/piper/en_GB-alan-medium.onnx";
        if (fileExists(local))
        {
            return local;
        }
    }
    return {};
}

pid_t spawnArgs(char **argv, int stdinFd, int stdoutFd, pid_t group)
{
    posix_spawnattr_t attr;
    posix_spawn_file_actions_t files;
    if (posix_spawnattr_init(&attr) != 0 || posix_spawn_file_actions_init(&files) != 0)
    {
        return -1;
    }
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, group);
    if (stdinFd >= 0)
    {
        posix_spawn_file_actions_adddup2(&files, stdinFd, STDIN_FILENO);
    }
    if (stdoutFd >= 0)
    {
        posix_spawn_file_actions_adddup2(&files, stdoutFd, STDOUT_FILENO);
    }
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, argv[0], &files, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&files);
    posix_spawnattr_destroy(&attr);
    return rc == 0 ? pid : -1;
}

void closePipe(int fd[2])
{
    if (fd[0] >= 0)
    {
        close(fd[0]);
        fd[0] = -1;
    }
    if (fd[1] >= 0)
    {
        close(fd[1]);
        fd[1] = -1;
    }
}
#endif

#ifdef __ANDROID__
JavaVM *gJvm = nullptr;
jclass gActivity = nullptr;
jmethodID gSpeak = nullptr;
jmethodID gVoiceCatalog = nullptr;
jmethodID gSelectVoice = nullptr;

std::string androidVoiceCatalog()
{
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env || !gActivity || !gVoiceCatalog)
    {
        return {};
    }
    jstring value = static_cast<jstring>(env->CallStaticObjectMethod(gActivity, gVoiceCatalog));
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
        return {};
    }
    if (!value)
    {
        return {};
    }
    const char *chars = env->GetStringUTFChars(value, nullptr);
    std::string catalog = chars ? chars : "";
    env->ReleaseStringUTFChars(value, chars);
    env->DeleteLocalRef(value);
    return catalog;
}

void androidSelectVoice(const std::string &id)
{
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env || !gActivity || !gSelectVoice)
    {
        return;
    }
    jstring name = env->NewStringUTF(id.c_str());
    if (!name)
    {
        return;
    }
    env->CallStaticVoidMethod(gActivity, gSelectVoice, name);
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(name);
}
#endif
} // namespace

VoiceAnnouncer &VoiceAnnouncer::instance()
{
    static VoiceAnnouncer announcer;
    return announcer;
}

VoiceAnnouncer::~VoiceAnnouncer()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mStop = true;
    }
    mCv.notify_all();
    interrupt();
    if (mThread.joinable())
    {
        mThread.join();
    }
}

void VoiceAnnouncer::say(std::string text, int priority)
{
    if (text.empty())
    {
        return;
    }
    prepare();
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mStarted)
        {
            mStarted = true;
            mThread = std::thread([this] { loop(); });
        }
        if (priority >= 1)
        {
            mQueue.clear();
            interrupt();
        }
        else
        {
            if (text == mSpeaking)
            {
                return;
            }
            for (const auto &item : mQueue)
            {
                if (item.text == text)
                {
                    return;
                }
            }
        }
        while (mQueue.size() >= 3)
        {
            mQueue.pop_front();
        }
        mQueue.push_back(Item{std::move(text), priority});
    }
    mCv.notify_one();
}

void VoiceAnnouncer::interrupt()
{
    mEpoch.fetch_add(1);
    const int pid = mPid.load();
    if (pid > 0)
    {
        kill(pid, SIGTERM);
        kill(-pid, SIGTERM);
    }
#ifndef __ANDROID__
    if (onPath("spd-say"))
    {
        char name[] = "spd-say";
        char flagN[] = "-N";
        char app[] = "efis";
        char flagC[] = "-C";
        char *argv[] = {name, flagN, app, flagC, nullptr};
        const pid_t cancel = spawnArgs(argv, -1, -1, 0);
        if (cancel > 0)
        {
            int status = 0;
            waitpid(cancel, &status, 0);
        }
    }
#endif
}

void VoiceAnnouncer::loop()
{
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &blocked, nullptr);

    for (;;)
    {
        Item item;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [&] { return mStop.load() || !mQueue.empty(); });
            if (mStop.load() && mQueue.empty())
            {
                return;
            }
            item = std::move(mQueue.front());
            mQueue.pop_front();
            mSpeaking = item.text;
        }
        speak(item);
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mSpeaking.clear();
            mPid = 0;
        }
    }
}

void VoiceAnnouncer::prepare()
{
    if (mPrepared)
    {
        return;
    }
    mPrepared = true;
#ifdef __ANDROID__
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env)
    {
        std::cerr << "Voice engine: android speech unavailable" << std::endl;
        return;
    }
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    env->GetJavaVM(&gJvm);
    jclass local = env->FindClass("com/efis/app/EfisActivity");
    if (!local)
    {
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        std::cerr << "Voice engine: EfisActivity missing" << std::endl;
        return;
    }
    gActivity = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    gSpeak = env->GetStaticMethodID(gActivity, "speak", "(Ljava/lang/String;Z)V");
    if (!gSpeak && env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    gVoiceCatalog = env->GetStaticMethodID(gActivity, "voiceCatalog", "()Ljava/lang/String;");
    if (!gVoiceCatalog && env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    gSelectVoice = env->GetStaticMethodID(gActivity, "selectVoice", "(Ljava/lang/String;)V");
    if (!gSelectVoice && env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    mAndroidReady = gJvm && gActivity && gSpeak;
    std::cout << "Voice engine: " << (mAndroidReady ? "android" : "none") << std::endl;
#else
    if (onPath("piper") && onPath("aplay") && !piperModel().empty())
    {
        std::cout << "Voice engine: piper " << piperModel() << std::endl;
    }
    else if (onPath("spd-say"))
    {
        std::cout << "Voice engine: speech-dispatcher en-GB" << std::endl;
    }
    else if (onPath("espeak-ng") || onPath("espeak"))
    {
        std::cout << "Voice engine: espeak-ng" << std::endl;
    }
    else
    {
        std::cout << "Voice engine: none" << std::endl;
    }
#endif
}

void VoiceAnnouncer::speak(const Item &item)
{
    const int epoch = mEpoch.load();
    std::cout << "Voice: " << item.text << std::endl;
#ifdef __ANDROID__
    if (!speakAndroid(item.text, item.priority >= 1))
    {
        return;
    }
    const int ms = std::clamp(600 + static_cast<int>(item.text.size()) * 62, 700, 5000);
    for (int waited = 0; waited < ms && mEpoch.load() == epoch; waited += 80)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
#else
    speakDesktop(item.text, epoch, item.priority);
#endif
}

bool VoiceAnnouncer::speakAndroid(const std::string &text, bool flush)
{
#ifdef __ANDROID__
    if (!mAndroidReady || !gJvm || !gSpeak)
    {
        return false;
    }
    JNIEnv *env = nullptr;
    bool attached = false;
    if (gJvm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
    {
        if (gJvm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
        {
            return false;
        }
        attached = true;
    }
    jstring jText = env->NewStringUTF(text.c_str());
    if (jText)
    {
        env->CallStaticVoidMethod(gActivity, gSpeak, jText, flush ? JNI_TRUE : JNI_FALSE);
        if (env->ExceptionCheck())
        {
            env->ExceptionClear();
        }
        env->DeleteLocalRef(jText);
    }
    if (attached)
    {
        gJvm->DetachCurrentThread();
    }
    return true;
#else
    (void)text;
    (void)flush;
    return false;
#endif
}

bool VoiceAnnouncer::speakDesktop(const std::string &text, int epoch, int priority)
{
#ifdef __ANDROID__
    (void)text;
    (void)epoch;
    (void)priority;
    return false;
#else
    const std::string model = piperModel();
    if (onPath("piper") && onPath("aplay") && !model.empty())
    {
        int textPipe[2] = {-1, -1};
        int audioPipe[2] = {-1, -1};
        if (pipe(textPipe) == 0 && pipe(audioPipe) == 0)
        {
            for (int fd : {textPipe[0], textPipe[1], audioPipe[0], audioPipe[1]})
            {
                fcntl(fd, F_SETFD, FD_CLOEXEC);
            }
            std::string modelCopy = model;
            char piperBin[] = "piper";
            char modelFlag[] = "--model";
            char rawFlag[] = "--output-raw";
            char *piperArgv[] = {piperBin, modelFlag, modelCopy.data(), rawFlag, nullptr};
            const pid_t piper = spawnArgs(piperArgv, textPipe[0], audioPipe[1], 0);
            if (piper > 0)
            {
                char rate[16];
                std::snprintf(rate, sizeof(rate), "%d", sampleRateOf(model));
                char aplayBin[] = "aplay";
                char quiet[] = "-q";
                char rateFlag[] = "-r";
                char formatFlag[] = "-f";
                char format[] = "S16_LE";
                char typeFlag[] = "-t";
                char type[] = "raw";
                char dash[] = "-";
                char *aplayArgv[] = {aplayBin, quiet, rateFlag, rate, formatFlag, format, typeFlag, type, dash, nullptr};
                const pid_t player = spawnArgs(aplayArgv, audioPipe[0], -1, piper);
                close(textPipe[0]);
                textPipe[0] = -1;
                close(audioPipe[0]);
                audioPipe[0] = -1;
                close(audioPipe[1]);
                audioPipe[1] = -1;
                mPid = piper;
                const std::string line = text + "\n";
                const char *data = line.c_str();
                size_t left = line.size();
                while (left > 0 && mEpoch.load() == epoch)
                {
                    const ssize_t wrote = write(textPipe[1], data, left);
                    if (wrote <= 0)
                    {
                        break;
                    }
                    data += wrote;
                    left -= static_cast<size_t>(wrote);
                }
                close(textPipe[1]);
                textPipe[1] = -1;
                int status = 0;
                while (waitpid(piper, &status, 0) < 0 && errno == EINTR)
                {
                }
                if (player > 0)
                {
                    while (waitpid(player, &status, 0) < 0 && errno == EINTR)
                    {
                    }
                }
                mPid = 0;
                closePipe(textPipe);
                closePipe(audioPipe);
                return mEpoch.load() == epoch;
            }
        }
        closePipe(textPipe);
        closePipe(audioPipe);
    }

    std::vector<std::string> args;
    const std::string chosen = voiceId();
    if (onPath("spd-say"))
    {
        args = {"spd-say", "-N", "efis", "-l", "en-GB", "-t", chosen.empty() ? "male1" : chosen, "-r", "-20", "-w", "-P",
                priority >= 1 ? "important" : "text", text};
    }
    else if (onPath("espeak-ng") || onPath("espeak"))
    {
        std::string variant = "en-gb";
        if (chosen.find("female1") != std::string::npos)
        {
            variant = "en-gb+f3";
        }
        else if (chosen.find("female2") != std::string::npos)
        {
            variant = "en-gb+f2";
        }
        else if (chosen.find("female3") != std::string::npos)
        {
            variant = "en-gb+f1";
        }
        else if (chosen.find("male2") != std::string::npos)
        {
            variant = "en-gb+m2";
        }
        else if (chosen.find("male3") != std::string::npos)
        {
            variant = "en-gb+m1";
        }
        else if (chosen.find("male1") != std::string::npos)
        {
            variant = "en-gb+m3";
        }
        args = {onPath("espeak-ng") ? "espeak-ng" : "espeak", "-v", variant, "-s", "145", text};
    }
    else
    {
        return false;
    }
    if (mEpoch.load() != epoch)
    {
        return false;
    }
    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (auto &arg : args)
    {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);
    const pid_t pid = spawnArgs(argv.data(), -1, -1, 0);
    if (pid <= 0)
    {
        return false;
    }
    mPid = pid;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
    {
    }
    mPid = 0;
    return mEpoch.load() == epoch;
#endif
}

void VoiceAnnouncer::pollVoices()
{
#ifndef __ANDROID__
    if (!mVoices.empty())
    {
        return;
    }
#else
    const uint32_t now = SDL_GetTicks();
    if (!mVoices.empty() && mVoicePollMs != 0 && now - mVoicePollMs < 700)
    {
        return;
    }
    mVoicePollMs = now;
#endif
    reloadVoices();
}

void VoiceAnnouncer::stepVoice(int delta)
{
    pollVoices();
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mVoiceMutex);
        if (mVoices.empty())
        {
            return;
        }
        const int count = static_cast<int>(mVoices.size());
        mVoiceIndex = (mVoiceIndex + (delta % count) + count) % count;
        id = mVoices[static_cast<size_t>(mVoiceIndex)].id;
    }
    applyVoice(id);
}

std::vector<std::string> VoiceAnnouncer::voiceLabels()
{
    pollVoices();
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    std::vector<std::string> labels;
    labels.reserve(mVoices.size());
    for (const VoiceChoice &choice : mVoices)
    {
        labels.push_back(choice.label.empty() ? "DEFAULT" : choice.label);
    }
    return labels;
}

int VoiceAnnouncer::voiceIndex()
{
    pollVoices();
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    if (mVoices.empty())
    {
        return 0;
    }
    return std::clamp(mVoiceIndex, 0, static_cast<int>(mVoices.size()) - 1);
}

void VoiceAnnouncer::selectVoice(int index)
{
    pollVoices();
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mVoiceMutex);
        if (mVoices.empty() || index < 0 || index >= static_cast<int>(mVoices.size()))
        {
            return;
        }
        mVoiceIndex = index;
        id = mVoices[static_cast<size_t>(mVoiceIndex)].id;
    }
    applyVoice(id);
}

std::string VoiceAnnouncer::voiceLabel()
{
    pollVoices();
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    if (mVoices.empty() || mVoiceIndex < 0 || mVoiceIndex >= static_cast<int>(mVoices.size()))
    {
        return "DEFAULT";
    }
    return mVoices[static_cast<size_t>(mVoiceIndex)].label;
}

std::string VoiceAnnouncer::voiceId()
{
    std::lock_guard<std::mutex> lock(mVoiceMutex);
    if (mVoices.empty() || mVoiceIndex < 0 || mVoiceIndex >= static_cast<int>(mVoices.size()))
    {
        return "male1";
    }
    const std::string &id = mVoices[static_cast<size_t>(mVoiceIndex)].id;
    return id.empty() ? "male1" : id;
}

void VoiceAnnouncer::applyVoice(const std::string &id)
{
    if (id == mAppliedId)
    {
        return;
    }
    mAppliedId = id;
#ifdef __ANDROID__
    prepare();
    androidSelectVoice(id);
#else
    (void)id;
#endif
}

void VoiceAnnouncer::reloadVoices()
{
    std::vector<VoiceChoice> next;
#ifdef __ANDROID__
    prepare();
    std::istringstream lines(androidVoiceCatalog());
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.empty())
        {
            continue;
        }
        const auto tab = line.find('\t');
        VoiceChoice choice;
        if (tab == std::string::npos)
        {
            choice.id = line;
            choice.label = line;
        }
        else
        {
            choice.id = line.substr(0, tab);
            choice.label = line.substr(tab + 1);
        }
        next.push_back(std::move(choice));
    }
#else
    next = {{"male1", "Male"},   {"female1", "Female"}, {"male2", "Male 2"},
            {"female2", "Female 2"}, {"male3", "Male 3"}, {"female3", "Female 3"}};
#endif
    if (next.empty())
    {
        next.push_back({"", "DEFAULT"});
    }

    std::string previous;
    {
        std::lock_guard<std::mutex> lock(mVoiceMutex);
        if (!mVoices.empty() && mVoiceIndex >= 0 && mVoiceIndex < static_cast<int>(mVoices.size()))
        {
            previous = mVoices[static_cast<size_t>(mVoiceIndex)].id;
        }
    }
    int index = 0;
    if (!previous.empty())
    {
        for (int i = 0; i < static_cast<int>(next.size()); ++i)
        {
            if (next[static_cast<size_t>(i)].id == previous)
            {
                index = i;
                break;
            }
        }
    }
    std::string apply;
    {
        std::lock_guard<std::mutex> lock(mVoiceMutex);
        mVoices = std::move(next);
        mVoiceIndex = index;
        apply = mVoices[static_cast<size_t>(mVoiceIndex)].id;
    }
    applyVoice(apply);
}
