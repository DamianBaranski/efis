/// \file terrain_download.cpp
/// Downloads missing FlightGear .btg.gz tiles on a background thread.
#include "terrain_download.h"
#include "sdl_compat.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#ifdef EFIS_HAS_CURL
#include <curl/curl.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#endif
#include <sys/stat.h>
#include <thread>

namespace
{
constexpr const char *kMirrors[] = {
    "https://de3mirror.flightgear.org/ws2/Terrain",
    "https://us1mirror.flightgear.org/terrasync/ws2/Terrain",
    "https://download.flightgear.org/ws2/Terrain",
};

bool fileExists(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}

bool isGzip(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    unsigned char magic[2] = {0, 0};
    in.read(reinterpret_cast<char *>(magic), 2);
    return in.gcount() == 2 && magic[0] == 0x1f && magic[1] == 0x8b;
}

std::string relativeTerrain(const std::string &path)
{
    const std::string marker = "resources/terrain/";
    const auto pos = path.find(marker);
    if (pos == std::string::npos)
    {
        return {};
    }
    const std::string rel = path.substr(pos + marker.size());
    if (rel.empty() || rel.find("..") != std::string::npos || rel.find(".btg.gz") == std::string::npos)
    {
        return {};
    }
    return rel;
}

#ifdef __ANDROID__
JavaVM *gJvm = nullptr;
jclass gEfisActivity = nullptr;
jmethodID gDownloadUrl = nullptr;

void androidHttpInit()
{
    if (gDownloadUrl)
    {
        return;
    }
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    if (!env)
    {
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
        std::cerr << "MAPDL: EfisActivity class missing" << std::endl;
        return;
    }
    gEfisActivity = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    gDownloadUrl = env->GetStaticMethodID(gEfisActivity, "downloadUrl",
                                          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
    if (!gDownloadUrl && env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
}

bool httpToFile(const std::string &url, const std::string &path)
{
    if (!gJvm || !gEfisActivity || !gDownloadUrl)
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
    jstring jUrl = env->NewStringUTF(url.c_str());
    jstring jPath = env->NewStringUTF(path.c_str());
    jstring jKey = env->NewStringUTF("");
    const jboolean ok = env->CallStaticBooleanMethod(gEfisActivity, gDownloadUrl, jUrl, jPath, jKey);
    if (env->ExceptionCheck())
    {
        env->ExceptionClear();
    }
    env->DeleteLocalRef(jUrl);
    env->DeleteLocalRef(jPath);
    env->DeleteLocalRef(jKey);
    if (attached)
    {
        gJvm->DetachCurrentThread();
    }
    return ok == JNI_TRUE;
}
#elif defined(EFIS_HAS_CURL)
size_t writeFile(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::ofstream *>(userdata);
    const size_t bytes = size * nmemb;
    out->write(ptr, static_cast<std::streamsize>(bytes));
    return out->good() ? bytes : 0;
}

void makeParentDirs(const std::string &path)
{
    std::string dir;
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '/')
        {
            dir = path.substr(0, i);
            if (!dir.empty())
            {
                mkdir(dir.c_str(), 0755);
            }
        }
    }
}

bool httpToFile(const std::string &url, const std::string &path)
{
    makeParentDirs(path);
    const std::string tmp = path + ".part";
    std::ofstream out(tmp, std::ios::binary);
    if (!out)
    {
        return false;
    }
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        return false;
    }
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: efis-terrain/1.0");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    const CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    out.close();
    if (res != CURLE_OK || status != 200)
    {
        std::remove(tmp.c_str());
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}
#else
bool httpToFile(const std::string &url, const std::string &path)
{
    (void)url;
    (void)path;
    return false;
}
#endif
}

TerrainDownload &TerrainDownload::instance()
{
    static TerrainDownload downloader;
    return downloader;
}

TerrainDownload::TerrainDownload() = default;

void TerrainDownload::prepare()
{
#ifdef __ANDROID__
    androidHttpInit();
#endif
}

void TerrainDownload::request(const std::string &absolutePath)
{
    if (fileExists(absolutePath))
    {
        return;
    }
    const std::string rel = relativeTerrain(absolutePath);
    if (rel.empty())
    {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mQueued.count(absolutePath) != 0 || mFailed.count(absolutePath) != 0 || mReady.count(absolutePath) != 0)
        {
            return;
        }
        mQueued.insert(absolutePath);
        mQueue.push_back(Job{absolutePath, rel});
    }
    std::cout << "MAPDL queue " << rel << std::endl;
    ensureWorker();
    mCv.notify_one();
}

bool TerrainDownload::takeReady(const std::string &absolutePath)
{
    std::lock_guard<std::mutex> lock(mMutex);
    const auto it = mReady.find(absolutePath);
    if (it == mReady.end())
    {
        return false;
    }
    mReady.erase(it);
    return true;
}

TerrainDownload::Stats TerrainDownload::stats() const
{
    Stats s;
    s.ok = mOk.load();
    s.fail = mFail.load();
    s.inFlight = mInFlight.load();
    std::lock_guard<std::mutex> lock(mMutex);
    s.queued = static_cast<int>(mQueue.size());
    return s;
}

void TerrainDownload::ensureWorker()
{
    bool expected = false;
    if (mWorkerStarted.compare_exchange_strong(expected, true))
    {
        std::thread(&TerrainDownload::workerLoop, this).detach();
    }
}

bool TerrainDownload::downloadJob(const Job &job)
{
    for (const char *mirror : kMirrors)
    {
        const std::string url = std::string(mirror) + "/" + job.rel;
        std::cout << "MAPDL get " << url << std::endl;
        if (!httpToFile(url, job.path) || !isGzip(job.path))
        {
            std::remove(job.path.c_str());
            continue;
        }
        return true;
    }
    return false;
}

void TerrainDownload::workerLoop()
{
    while (true)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [this] { return !mQueue.empty(); });
            job = mQueue.front();
            mQueue.pop_front();
            mQueued.erase(job.path);
            ++mInFlight;
        }
        bool ok = false;
        try
        {
            ok = downloadJob(job);
        }
        catch (...)
        {
            ok = false;
        }
        if (ok && fileExists(job.path))
        {
            ++mOk;
            std::cout << "MAPDL ok " << job.rel << std::endl;
            std::lock_guard<std::mutex> lock(mMutex);
            mReady.insert(job.path);
        }
        else
        {
            ++mFail;
            std::remove(job.path.c_str());
            std::cout << "MAPDL fail " << job.rel << std::endl;
            std::lock_guard<std::mutex> lock(mMutex);
            mFailed.insert(job.path);
        }
        --mInFlight;
    }
}
