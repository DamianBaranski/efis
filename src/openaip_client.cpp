#include "openaip_client.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace
{
constexpr char kBaseUrl[] = "https://api.tiles.openaip.net/api/data";
constexpr char kCacheRoot[] = "../resources/openaip/cache";
constexpr char kKeyFile[] = "../resources/openaip/api.key";

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

size_t writeFile(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *out = static_cast<std::ofstream *>(userdata);
    const size_t bytes = size * nmemb;
    out->write(ptr, static_cast<std::streamsize>(bytes));
    return out->good() ? bytes : 0;
}
}

OpenAipClient &OpenAipClient::instance()
{
    static OpenAipClient client;
    return client;
}

OpenAipClient::OpenAipClient() : mCacheRoot(kCacheRoot)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    mApiKey = loadKey("OPENAIP_API_KEY", kKeyFile);
    if (mApiKey.empty())
    {
        std::cerr << "OpenAIP: no API key (set OPENAIP_API_KEY or " << kKeyFile << ")" << std::endl;
    }
    else
    {
        std::cout << "OpenAIP tiles: https://www.openaip.net (CC BY-NC 4.0)" << std::endl;
    }
    std::cout << "Basemap: Esri World Imagery" << std::endl;
}

std::string OpenAipClient::loadApiKey() const
{
    return loadKey("OPENAIP_API_KEY", kKeyFile);
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
    if (!downloadToFile(url.str(), path, true))
    {
        return {};
    }
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
    std::cout << "Basemap fetch satellite " << z << "/" << x << "/" << y << std::endl;
    if (!downloadToFile(url.str(), path, false))
    {
        return {};
    }
    return path;
}

void OpenAipClient::fetchAround(float latitude, float longitude, int zoom, int radius)
{
    const auto tile = latLonToTile(latitude, longitude, zoom);
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto it = mLastTile.find(zoom);
        if (it != mLastTile.end() && it->second == tile)
        {
            return;
        }
        mLastTile[zoom] = tile;
    }

    const bool haveKey = !mApiKey.empty();
    std::thread([this, zoom, radius, haveKey, cx = tile.first, cy = tile.second]() {
        for (int dy = -radius; dy <= radius; ++dy)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                fetchBasemap(zoom, cx + dx, cy + dy);
                if (haveKey)
                {
                    fetchTile(zoom, cx + dx, cy + dy, "openaip");
                }
            }
        }
    }).detach();
}
