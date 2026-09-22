#include "openaip_client.h"
#include "asset_path.h"
#include "sdl_compat.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#ifdef EFIS_HAS_CURL
#include <curl/curl.h>
#endif
#ifdef __ANDROID__
#include <jni.h>
#endif
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unordered_set>
#include <vector>

namespace
{
constexpr char kBaseUrl[] = "https://api.tiles.openaip.net/api/data";
constexpr char kCacheRel[] = "resources/openaip/cache";
constexpr char kKeyRel[] = "resources/openaip/api.key";

bool fileExists(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
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
        std::cerr << "OpenAIP: no JNI env" << std::endl;
        return;
    }
    env->GetJavaVM(&gJvm);
    jclass local = env->FindClass("com/efis/app/EfisActivity");
    if (!local)
    {
        std::cerr << "OpenAIP: EfisActivity class missing" << std::endl;
        return;
    }
    gEfisActivity = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    gDownloadUrl = env->GetStaticMethodID(gEfisActivity, "downloadUrl",
                                          "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z");
    if (!gDownloadUrl)
    {
        std::cerr << "OpenAIP: downloadUrl missing" << std::endl;
    }
}

bool androidDownloadToFile(const std::string &url, const std::string &path, const std::string &apiKey)
{
    if (!gJvm || !gEfisActivity || !gDownloadUrl)
    {
        androidHttpInit();
    }
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
    jstring jKey = env->NewStringUTF(apiKey.c_str());
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
#endif

constexpr int kDownloadWorkers = 4;

std::string trim(std::string value)
{
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
    {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

std::string readKeyFile(const std::string &path)
{
    std::ifstream file(path);
    if (!file)
    {
        return {};
    }
    std::string key;
    std::getline(file, key);
    return trim(key);
}

std::string loadKey(const char *envName, const char *path)
{
    if (const char *env = std::getenv(envName))
    {
        const std::string key = trim(env);
        if (!key.empty())
        {
            return key;
        }
    }
    return readKeyFile(path);
}

#ifdef EFIS_HAS_CURL
size_t writeFile(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::ofstream *>(userdata);
    const size_t bytes = size * nmemb;
    out->write(ptr, static_cast<std::streamsize>(bytes));
    return out->good() ? bytes : 0;
}
#endif
}

OpenAipClient &OpenAipClient::instance()
{
    static OpenAipClient client;
    return client;
}

OpenAipClient::OpenAipClient() : mCacheRoot(AssetPath::resolve(kCacheRel))
{
#ifdef EFIS_HAS_CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
#ifdef __ANDROID__
    androidHttpInit();
#endif
    const std::string keyPath = AssetPath::resolve(kKeyRel);
    mApiKey = loadKey("OPENAIP_API_KEY", keyPath.c_str());
    if (mApiKey.empty())
    {
        std::cerr << "OpenAIP: no API key (set OPENAIP_API_KEY or " << keyPath << ")" << std::endl;
    }
    else
    {
        std::cout << "OpenAIP tiles: https://www.openaip.net (CC BY-NC 4.0)" << std::endl;
    }
    std::cout << "Basemap: Esri World Imagery cache=" << mCacheRoot << std::endl;
}

std::string OpenAipClient::loadApiKey() const
{
    return loadKey("OPENAIP_API_KEY", AssetPath::resolve(kKeyRel).c_str());
}

std::pair<int, int> OpenAipClient::latLonToTile(float latitude, float longitude, int zoom)
{
    const double n = std::exp2(static_cast<double>(zoom));
    const double latRad = static_cast<double>(latitude) * M_PI / 180.0;
    int x = static_cast<int>(std::floor((static_cast<double>(longitude) + 180.0) / 360.0 * n));
    int y = static_cast<int>(std::floor((1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n));
    const int maxIndex = static_cast<int>(n) - 1;
    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }
    if (x > maxIndex)
    {
        x = maxIndex;
    }
    if (y > maxIndex)
    {
        y = maxIndex;
    }
    return {x, y};
}

void OpenAipClient::latLonToPixels(float latitude, float longitude, int zoom, double &pixelX, double &pixelY)
{
    const double n = std::exp2(static_cast<double>(zoom));
    const double latRad = static_cast<double>(latitude) * M_PI / 180.0;
    pixelX = (static_cast<double>(longitude) + 180.0) / 360.0 * n * 256.0;
    pixelY = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / M_PI) / 2.0 * n * 256.0;
}

bool OpenAipClient::isCached(int z, int x, int y, const std::string &layer) const
{
    return fileExists(cachePath(z, x, y, layer));
}

std::string OpenAipClient::cachePath(int z, int x, int y, const std::string &layer) const
{
    std::ostringstream path;
    path << mCacheRoot << '/' << layer << '/' << z << '/' << x << '/' << y << ".png";
    return path.str();
}

bool OpenAipClient::downloadToFile(const std::string &url, const std::string &path, bool sendApiKey) const
{
#ifdef __ANDROID__
    makeParentDirs(path);
    return androidDownloadToFile(url, path, sendApiKey ? mApiKey : std::string());
#elif !defined(EFIS_HAS_CURL)
    (void)url;
    (void)path;
    (void)sendApiKey;
    return false;
#else
    makeParentDirs(path);
    const std::string tmp = path + ".part";
    std::ofstream out(tmp, std::ios::binary);
    if (!out)
    {
        std::cerr << "OpenAIP: cannot write " << tmp << std::endl;
        return false;
    }

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        return false;
    }
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "User-Agent: efis-openaip/1.0");
    if (sendApiKey && !mApiKey.empty())
    {
        const std::string header = std::string("x-openaip-api-key: ") + mApiKey;
        headers = curl_slist_append(headers, header.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    const CURLcode res = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    out.close();

    if (res != CURLE_OK || (status != 200 && status != 204))
    {
        std::remove(tmp.c_str());
        std::cerr << "Tile failed HTTP " << status << " " << url << std::endl;
        return false;
    }
    if (status == 204)
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
#endif
}

std::string OpenAipClient::fetchTile(int z, int x, int y, const std::string &layer)
{
    const std::string path = cachePath(z, x, y, layer);
    if (fileExists(path))
    {
        return path;
    }
    if (mApiKey.empty())
    {
        return {};
    }
    std::ostringstream url;
    url << kBaseUrl << '/' << layer << '/' << z << '/' << x << '/' << y << ".png";
    std::cout << "OpenAIP fetch " << layer << " " << z << "/" << x << "/" << y << std::endl;
    const bool ok = downloadToFile(url.str(), path, true);
    if (!ok)
    {
        ++mAipFail;
        return {};
    }
    ++mAipOk;
    return path;
}

std::string OpenAipClient::fetchBasemap(int z, int x, int y)
{
    const std::string path = cachePath(z, x, y, mBasemapLayer);
    if (fileExists(path))
    {
        return path;
    }

    std::ostringstream url;
    url << "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/"
        << z << '/' << y << '/' << x;
    std::cout << "SATDBG http z=" << z << " osm=" << x << "," << y << " esri=" << z << "/" << y << "/" << x
              << " path=" << path << std::endl;
    const bool ok = downloadToFile(url.str(), path, false);
    if (!ok)
    {
        ++mSattFail;
        std::cout << "SATDBG http-fail z=" << z << " osm=" << x << "," << y << " fail=" << mSattFail.load()
                  << std::endl;
        return {};
    }
    ++mSattOk;
    std::cout << "SATDBG http-ok z=" << z << " osm=" << x << "," << y << " ok=" << mSattOk.load() << std::endl;
    return path;
}

OpenAipClient::DownloadStats OpenAipClient::downloadStats() const
{
    DownloadStats s;
    s.sattOk = mSattOk.load();
    s.sattFail = mSattFail.load();
    s.aipOk = mAipOk.load();
    s.aipFail = mAipFail.load();
    s.inFlight = mInFlight.load();
    s.hasAipKey = !mApiKey.empty();
    return s;
}

int OpenAipClient::pendingDownloads() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return static_cast<int>(mQueue.size()) + mInFlight.load();
}

uint64_t OpenAipClient::jobId(int z, int x, int y, bool aip)
{
    return (static_cast<uint64_t>(aip) << 63) | (static_cast<uint64_t>(z & 31) << 48) |
           (static_cast<uint64_t>(static_cast<uint32_t>(x) & 0xFFFFFFu) << 24) |
           (static_cast<uint32_t>(y) & 0xFFFFFFu);
}

void OpenAipClient::ensureWorker()
{
    bool expected = false;
    if (mWorkerStarted.compare_exchange_strong(expected, true))
    {
        for (int i = 0; i < kDownloadWorkers; ++i)
        {
            std::thread(&OpenAipClient::workerLoop, this).detach();
        }
    }
}

void OpenAipClient::enqueueMissing(int zoom, int x, int y, bool aip)
{
    if (isCached(zoom, x, y, aip ? "openaip" : mBasemapLayer))
    {
        return;
    }
    const uint64_t id = jobId(zoom, x, y, aip);
    if (mQueued.count(id) != 0)
    {
        return;
    }
    mQueued.insert(id);
    mQueue.push_back(Job{zoom, x, y, aip});
}

void OpenAipClient::workerLoop()
{
    while (true)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mCv.wait(lock, [this] { return !mQueue.empty(); });
            job = mQueue.front();
            mQueue.pop_front();
            mQueued.erase(jobId(job.z, job.x, job.y, job.aip));
            ++mInFlight;
            if (!mQueue.empty())
            {
                mCv.notify_one();
            }
        }
        try
        {
            if (job.aip)
            {
                fetchTile(job.z, job.x, job.y, "openaip");
            }
            else
            {
                fetchBasemap(job.z, job.x, job.y);
            }
        }
        catch (...)
        {
            std::cerr << "OpenAIP: download worker exception" << std::endl;
        }
        --mInFlight;
    }
}

void OpenAipClient::fetchAround(float latitude, float longitude, int zoom, int radius, bool wantBasemap,
                                bool wantOverlay)
{
    const auto tile = latLonToTile(latitude, longitude, zoom);
    const bool haveKey = !mApiKey.empty();
    const bool overlay = wantOverlay && haveKey;
    int cached = 0;
    int missing = 0;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mLastAround.find(zoom);
        if (it != mLastAround.end() && it->second.x == tile.first && it->second.y == tile.second &&
            it->second.base == wantBasemap && it->second.overlay == overlay)
        {
            return;
        }
        const bool tileMoved = it == mLastAround.end() || it->second.x != tile.first || it->second.y != tile.second;
        mLastAround[zoom] = LastAround{tile.first, tile.second, wantBasemap, overlay};

        if (tileMoved)
        {
            std::deque<Job> keep;
            std::unordered_set<uint64_t> keepIds;
            for (const auto &job : mQueue)
            {
                if (job.z != zoom)
                {
                    keep.push_back(job);
                    keepIds.insert(jobId(job.z, job.x, job.y, job.aip));
                }
            }
            mQueue.swap(keep);
            mQueued.swap(keepIds);
        }

        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                if (wantBasemap)
                {
                    if (isCached(zoom, tile.first + dx, tile.second + dy, mBasemapLayer))
                    {
                        ++cached;
                    }
                    else
                    {
                        ++missing;
                        enqueueMissing(zoom, tile.first + dx, tile.second + dy, false);
                    }
                }
                if (overlay)
                {
                    enqueueMissing(zoom, tile.first + dx, tile.second + dy, true);
                }
            }
        }
        std::cout << "SATDBG fetchAround z=" << zoom << " lat=" << latitude << " lon=" << longitude
                  << " cam=" << tile.first << "," << tile.second << " r=" << radius << " cached=" << cached
                  << " missing=" << missing << " queued=" << mQueue.size() << " inFlight=" << mInFlight.load()
                  << " moved=" << (tileMoved ? 1 : 0) << " layer=" << mBasemapLayer << std::endl;
    }
    ensureWorker();
    mCv.notify_all();
}
