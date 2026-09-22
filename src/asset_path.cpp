#include "asset_path.h"
#include "sdl_compat.h"
#include <cstdio>
#include <iostream>
#include <sys/stat.h>

#ifdef __ANDROID__
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <jni.h>
#endif

namespace
{
std::string gRoot = ".";

bool isDir(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

bool isFile(const std::string &path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
}

#ifdef __ANDROID__
void makeDirs(const std::string &path)
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
#endif

#ifdef __ANDROID__
class AndroidLogBuf : public std::streambuf
{
    std::string mLine;

    int overflow(int ch) override
    {
        if (ch == traits_type::eof())
        {
            return sync();
        }
        if (ch == '\n')
        {
            __android_log_write(ANDROID_LOG_INFO, "efis", mLine.c_str());
            mLine.clear();
        }
        else
        {
            mLine.push_back(static_cast<char>(ch));
        }
        return ch;
    }

    int sync() override
    {
        if (!mLine.empty())
        {
            __android_log_write(ANDROID_LOG_INFO, "efis", mLine.c_str());
            mLine.clear();
        }
        return 0;
    }
};

void hookAndroidLog()
{
    static AndroidLogBuf buffer;
    std::cout.rdbuf(&buffer);
    std::cerr.rdbuf(&buffer);
}
#endif
} // namespace

void AssetPath::init()
{
#ifdef __ANDROID__
    hookAndroidLog();
    gRoot = detectAndroidRoot();
    extractPackagedAssets(gRoot);
#else
    gRoot = detectDesktopRoot();
#endif
    std::cout << "Asset root " << gRoot << std::endl;
}

const std::string &AssetPath::root()
{
    return gRoot;
}

std::string AssetPath::resolve(const std::string &rel)
{
    if (rel.empty())
    {
        return gRoot;
    }
    if (rel[0] == '/')
    {
        return rel;
    }
    if (gRoot == "." || gRoot.empty())
    {
        return rel;
    }
    return gRoot + "/" + rel;
}

std::string AssetPath::detectDesktopRoot()
{
    if (isDir("../resources") && isFile("../shader/vertex.gl"))
    {
        return "..";
    }
    if (isDir("resources") && isFile("shader/vertex.gl"))
    {
        return ".";
    }
    return "..";
}

#ifdef __ANDROID__
std::string AssetPath::detectAndroidRoot()
{
    // Prefer internal files/: Honor/scoped storage blocks the app from
    // adb-pushed trees under Android/data (dirs owned by shell, mode 770).
    const char *internal = SDL_AndroidGetInternalStoragePath();
    if (internal && internal[0] != '\0')
    {
        return internal;
    }
    const char *external = SDL_AndroidGetExternalStoragePath();
    if (external && external[0] != '\0')
    {
        return external;
    }
    return ".";
}

void AssetPath::extractPackagedAssets(const std::string &destRoot)
{
    JNIEnv *env = static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (!env || !activity)
    {
        std::cerr << "Android assets: no JNI activity" << std::endl;
        return;
    }

    jclass actClass = env->GetObjectClass(activity);
    jmethodID getAssets = env->GetMethodID(actClass, "getAssets", "()Landroid/content/res/AssetManager;");
    jobject assetManager = env->CallObjectMethod(activity, getAssets);
    AAssetManager *mgr = AAssetManager_fromJava(env, assetManager);
    if (!mgr)
    {
        std::cerr << "Android assets: no AssetManager" << std::endl;
        env->DeleteLocalRef(assetManager);
        env->DeleteLocalRef(actClass);
        env->DeleteLocalRef(activity);
        return;
    }

    AAsset *manifest = AAssetManager_open(mgr, "asset-manifest.txt", AASSET_MODE_BUFFER);
    if (!manifest)
    {
        std::cerr << "Android assets: missing asset-manifest.txt" << std::endl;
        env->DeleteLocalRef(assetManager);
        env->DeleteLocalRef(actClass);
        env->DeleteLocalRef(activity);
        return;
    }

    const size_t size = static_cast<size_t>(AAsset_getLength(manifest));
    std::string list(size, '\0');
    AAsset_read(manifest, list.data(), size);
    AAsset_close(manifest);

    int copied = 0;
    int skipped = 0;
    std::string line;
    for (size_t i = 0; i <= list.size(); ++i)
    {
        if (i == list.size() || list[i] == '\n')
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            {
                line.pop_back();
            }
            if (!line.empty() && line[0] != '#')
            {
                const std::string dest = destRoot + "/" + line;
                const bool shader = line.rfind("shader/", 0) == 0;
                if (isFile(dest) && !shader)
                {
                    ++skipped;
                }
                else
                {
                    AAsset *asset = AAssetManager_open(mgr, line.c_str(), AASSET_MODE_STREAMING);
                    if (asset)
                    {
                        makeDirs(dest);
                        FILE *out = fopen(dest.c_str(), "wb");
                        if (out)
                        {
                            char buf[8192];
                            int n = 0;
                            while ((n = AAsset_read(asset, buf, sizeof(buf))) > 0)
                            {
                                fwrite(buf, 1, static_cast<size_t>(n), out);
                            }
                            fclose(out);
                            ++copied;
                        }
                        AAsset_close(asset);
                    }
                }
            }
            line.clear();
        }
        else
        {
            line.push_back(list[i]);
        }
    }

    std::cout << "Android assets copied " << copied << ", kept " << skipped << std::endl;
    env->DeleteLocalRef(assetManager);
    env->DeleteLocalRef(actClass);
    env->DeleteLocalRef(activity);
}
#endif
