/// \file asset_path.h
/// Resolves fonts, shaders, and scenery against the platform asset root.
#ifndef ASSET_PATH_H
#define ASSET_PATH_H

#include <string>

/// Resolves fonts, shaders, and scenery against a platform asset root.
/// Desktop: `..` when run from `build/`, otherwise `.`
/// Android: app files dir after APK assets are extracted.
class AssetPath
{
public:
    /// Call once after SDL_Init.
    static void init();

    static const std::string &root();

    /// `rel` is repo-relative, e.g. `resources/fonts/B612Mono-Regular.ttf`.
    static std::string resolve(const std::string &rel);

private:
    static std::string detectDesktopRoot();
#ifdef __ANDROID__
    static std::string detectAndroidRoot();
    static void extractPackagedAssets(const std::string &destRoot);
#endif
};

#endif
