/// \file planning_map.cpp
/// Rasterizes the planner chart to one texture: night vector or Esri satellite
/// tiles, then airspaces, navaids, and the route. Drawn with Render2D.
#include "planning_map.h"

#include "asset_path.h"
#include "nav_db.h"
#include "openaip_client.h"

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <GLES3/gl3.h>
#include <glm/mat4x4.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr int kZoomMin = 5;
constexpr int kZoomMax = 14;
constexpr int kMaxFixesDrawn = 200;
constexpr int kMaxAirspaceFills = 48;
const char *kAtlasName = "planmap-atlas";

struct TexPt
{
    float x = 0.0f;
    float y = 0.0f;
};

/// Same projection formula the POC uses for draw, pan, and cull.
double zoomScale(float zoom)
{
    return std::pow(1.8, static_cast<double>(zoom)) * 22.0;
}

void putPixel(SDL_Surface *surface, int x, int y, Uint32 pixel)
{
    if (surface == nullptr || x < 0 || y < 0 || x >= surface->w || y >= surface->h)
    {
        return;
    }
    Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) + y * surface->pitch);
    row[x] = pixel;
}

void fillRing(SDL_Surface *surface, const TexPt *pts, int count, Uint32 pixel)
{
    if (surface == nullptr || pts == nullptr || count < 3)
    {
        return;
    }
    const int w = surface->w;
    const int h = surface->h;
    std::vector<float> hits;
    hits.reserve(static_cast<size_t>(count));
    for (int y = 0; y < h; ++y)
    {
        const float scan = static_cast<float>(y) + 0.5f;
        hits.clear();
        int prev = count - 1;
        for (int i = 0; i < count; ++i)
        {
            const float y0 = pts[prev].y;
            const float y1 = pts[i].y;
            const bool cross = (y0 <= scan && y1 > scan) || (y1 <= scan && y0 > scan);
            if (cross)
            {
                const float x0 = pts[prev].x;
                const float x1 = pts[i].x;
                const float t = (scan - y0) / (y1 - y0);
                hits.push_back(x0 + t * (x1 - x0));
            }
            prev = i;
        }
        if (hits.size() < 2)
        {
            continue;
        }
        std::sort(hits.begin(), hits.end());
        Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(surface->pixels) + y * surface->pitch);
        for (size_t i = 0; i + 1 < hits.size(); i += 2)
        {
            int x0 = static_cast<int>(std::floor(hits[i] + 0.5f));
            int x1 = static_cast<int>(std::floor(hits[i + 1] + 0.5f));
            if (x0 < 0)
            {
                x0 = 0;
            }
            if (x1 > w)
            {
                x1 = w;
            }
            for (int x = x0; x < x1; ++x)
            {
                row[x] = pixel;
            }
        }
    }
}

void paintDisk(SDL_Surface *surface, int cx, int cy, int radius, Uint32 pixel)
{
    const int r2 = radius * radius;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            if (x * x + y * y <= r2)
            {
                putPixel(surface, cx + x, cy + y, pixel);
            }
        }
    }
}

bool isPublishedIcao(const std::string &ident)
{
    if (ident.size() != 4)
    {
        return false;
    }
    if (ident.rfind("AF", 0) == 0)
    {
        return false;
    }
    for (char c : ident)
    {
        if (c < 'A' || c > 'Z')
        {
            return false;
        }
    }
    return true;
}

TTF_Font *mapLabelFont(int px)
{
    static TTF_Font *small = nullptr;
    static TTF_Font *regular = nullptr;
    TTF_Font **slot = px <= 10 ? &small : &regular;
    if (*slot != nullptr)
    {
        return *slot;
    }
    if (TTF_Init() != 0)
    {
        return nullptr;
    }
    *slot = TTF_OpenFont(AssetPath::resolve("resources/fonts/B612Mono-Regular.ttf").c_str(), px);
    return *slot;
}

std::string shortenUtf8(const std::string &s, size_t maxChars)
{
    size_t chars = 0;
    size_t i = 0;
    while (i < s.size() && chars < maxChars)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if ((c & 0x80) == 0)
        {
            i += 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            i += 3;
        }
        else
        {
            i += 4;
        }
        ++chars;
    }
    if (i >= s.size())
    {
        return s;
    }
    return s.substr(0, i);
}

void paintMapLabel(SDL_Surface *surface, int cx, int cy, const std::string &text, int fontPx, SDL_Color color)
{
    TTF_Font *font = mapLabelFont(fontPx);
    if (surface == nullptr || font == nullptr || text.empty())
    {
        return;
    }
    SDL_Surface *label = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (label == nullptr)
    {
        return;
    }
    SDL_Rect dst;
    dst.x = cx + 5;
    dst.y = cy - label->h / 2;
    dst.w = label->w;
    dst.h = label->h;
    SDL_BlitSurface(label, nullptr, surface, &dst);
    SDL_FreeSurface(label);
}

void paintLine(SDL_Surface *surface, float x0, float y0, float x1, float y1, float width, Uint32 pixel)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::hypot(dx, dy);
    const int radius = std::max(1, static_cast<int>(std::lround(width * 0.5f)));
    if (len < 0.5f)
    {
        paintDisk(surface, static_cast<int>(std::lround(x0)), static_cast<int>(std::lround(y0)), radius, pixel);
        return;
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(len)));
    for (int i = 0; i <= steps; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const int x = static_cast<int>(std::lround(x0 + dx * t));
        const int y = static_cast<int>(std::lround(y0 + dy * t));
        paintDisk(surface, x, y, radius, pixel);
    }
}

void toUpperInPlace(std::string &s)
{
    for (char &c : s)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
}

bool containsFold(const std::string &hay, const char *needle)
{
    return hay.find(needle) != std::string::npos;
}

enum class AspCat
{
    Ctr,
    Rpd,
    Tra,
    Atz,
    Other,
};

AspCat categorize(const AirspaceRing &r)
{
    std::string t = r.type;
    std::string n = r.name;
    toUpperInPlace(t);
    toUpperInPlace(n);
    if (t == "CTR" || t == "TMA" || containsFold(n, "CTR") || containsFold(n, "TMA"))
    {
        return AspCat::Ctr;
    }
    if (t == "ATZ" || t == "MATZ" || containsFold(t, "ATZ") || containsFold(n, "ATZ") || containsFold(n, "MATZ"))
    {
        return AspCat::Atz;
    }
    if (t == "P" || t == "R" || t == "D" || t == "PROHIBITED" || t == "RESTRICTED" || t == "DANGER" ||
        containsFold(t, "PROHIBITED") || containsFold(t, "RESTRICTED") || containsFold(t, "DANGER") ||
        containsFold(t, "WARNING") || n.rfind("EP P", 0) == 0 || n.rfind("EP R", 0) == 0 || n.rfind("EP D", 0) == 0 ||
        n.rfind("LK P", 0) == 0 || n.rfind("LK R", 0) == 0 || n.rfind("LK D", 0) == 0)
    {
        return AspCat::Rpd;
    }
    if (t == "TRA" || t == "TSA" || t == "CBA" || containsFold(t, "TRA") || containsFold(t, "TSA") ||
        containsFold(t, "CBA") || containsFold(n, "TRA") || containsFold(n, "TSA") || containsFold(n, "CBA"))
    {
        return AspCat::Tra;
    }
    return AspCat::Other;
}

/// True when the airspace's category matches the current filter.
bool matchesFilter(const AirspaceRing &r, AirspaceFilter f)
{
    if (f == AirspaceFilter::All)
    {
        return true;
    }
    const AspCat cat = categorize(r);
    switch (f)
    {
    case AirspaceFilter::CtrTma:
        return cat == AspCat::Ctr;
    case AirspaceFilter::Rpd:
        return cat == AspCat::Rpd;
    case AirspaceFilter::Tra:
        return cat == AspCat::Tra;
    case AirspaceFilter::Atz:
        return cat == AspCat::Atz;
    default:
        return true;
    }
}

void styleFor(const AirspaceRing &r, Uint8 &fillR, Uint8 &fillG, Uint8 &fillB, Uint8 &fillA, Uint8 &strokeR,
              Uint8 &strokeG, Uint8 &strokeB, Uint8 &strokeA, float &strokeW)
{
    switch (categorize(r))
    {
    case AspCat::Ctr:
        fillR = 0;
        fillG = 229;
        fillB = 255;
        fillA = 28;
        strokeR = 0;
        strokeG = 229;
        strokeB = 255;
        strokeA = 220;
        strokeW = 2.0f;
        return;
    case AspCat::Rpd:
        fillR = 255;
        fillG = 59;
        fillB = 71;
        fillA = 32;
        strokeR = 255;
        strokeG = 59;
        strokeB = 71;
        strokeA = 200;
        strokeW = 2.0f;
        return;
    case AspCat::Tra:
        fillR = 255;
        fillG = 183;
        fillB = 0;
        fillA = 28;
        strokeR = 255;
        strokeG = 183;
        strokeB = 0;
        strokeA = 200;
        strokeW = 2.0f;
        return;
    case AspCat::Atz:
        fillR = 57;
        fillG = 255;
        fillB = 106;
        fillA = 28;
        strokeR = 57;
        strokeG = 255;
        strokeB = 106;
        strokeA = 180;
        strokeW = 1.5f;
        return;
    default:
        fillR = 136;
        fillG = 153;
        fillB = 168;
        fillA = 24;
        strokeR = 136;
        strokeG = 153;
        strokeB = 168;
        strokeA = 160;
        strokeW = 1.2f;
        return;
    }
}

double tileWestLon(int zoom, int x)
{
    return static_cast<double>(x) / std::exp2(static_cast<double>(zoom)) * 360.0 - 180.0;
}

double tileNorthLat(int zoom, int y)
{
    const double n = std::exp2(static_cast<double>(zoom));
    const double yNorm = static_cast<double>(y) / n;
    return std::atan(std::sinh(kPi * (1.0 - 2.0 * yNorm))) * 180.0 / static_cast<double>(kPi);
}
} // namespace

PlanningMap::PlanningMap(Screen &screen, IDataManager &data, FlightPlan &plan)
    : mScreen(screen), mData(data), mPlan(plan), mPanel(screen), mMarker(screen)
{
}

void PlanningMap::place(int x, int y, int w, int h)
{
    w = std::max(1, w);
    h = std::max(1, h);
    if (x != mX || y != mY || w != mW || h != mH)
    {
        markDirty();
    }
    mX = x;
    mY = y;
    mW = w;
    mH = h;
    if (!mCentered)
    {
        fitRoute();
    }
}

void PlanningMap::fitRoute()
{
    double minLat = 90.0;
    double maxLat = -90.0;
    double minLon = 180.0;
    double maxLon = -180.0;
    int found = 0;
    for (const std::string &ident : mPlan.route())
    {
        const Waypoint *wpt = NavDb::instance().find(ident);
        if (wpt == nullptr)
        {
            continue;
        }
        minLat = std::min(minLat, wpt->lat);
        maxLat = std::max(maxLat, wpt->lat);
        minLon = std::min(minLon, wpt->lon);
        maxLon = std::max(maxLon, wpt->lon);
        ++found;
    }
    if (found == 0)
    {
        mCenterLat = 50.6;
        mCenterLon = 18.3;
        mZoom = 7.0f;
    }
    else
    {
        mCenterLat = (minLat + maxLat) / 2.0;
        mCenterLon = (minLon + maxLon) / 2.0;
        const double latSpan = std::max(0.05, maxLat - minLat);
        const double lonSpan = std::max(0.05, maxLon - minLon);
        const double span = std::max(latSpan, lonSpan);
        const double target = static_cast<double>(std::min(mW, mH)) * 0.35;
        double bestZoom = 5.0;
        for (int z = kZoomMin; z <= 11; ++z)
        {
            const double s = std::pow(1.8, static_cast<double>(z)) * 22.0;
            if (span * s <= target)
            {
                bestZoom = static_cast<double>(z);
            }
        }
        mZoom = static_cast<float>(bestZoom);
    }
    mCentered = true;
    markDirty();
}

void PlanningMap::zoom(int delta)
{
    const float next = std::clamp(mZoom + static_cast<float>(delta), static_cast<float>(kZoomMin),
                                  static_cast<float>(kZoomMax));
    if (next != mZoom)
    {
        mZoom = next;
        markDirty();
    }
}

void PlanningMap::zoomFine(float delta)
{
    zoomAt(delta, mX + mW / 2, mY + mH / 2);
}

void PlanningMap::zoomAt(float delta, int x, int y)
{
    const float next = std::clamp(mZoom + delta, static_cast<float>(kZoomMin), static_cast<float>(kZoomMax));
    if (next == mZoom || mW <= 0 || mH <= 0)
    {
        return;
    }
    const double scale0 = zoomScale(mZoom);
    const double cos0 = std::cos(mCenterLat * kPi / 180.0);
    const double lon = mCenterLon + (static_cast<double>(x) - mX - mW / 2.0) / std::max(1.0e-6, scale0 * cos0);
    const double lat = mCenterLat - (static_cast<double>(y) - mY - mH / 2.0) / std::max(1.0e-6, scale0);
    mZoom = next;
    const double scale1 = zoomScale(mZoom);
    const double cos1 = std::cos(lat * kPi / 180.0);
    mCenterLon = lon - (static_cast<double>(x) - mX - mW / 2.0) / std::max(1.0e-6, scale1 * cos1);
    mCenterLat = lat + (static_cast<double>(y) - mY - mH / 2.0) / std::max(1.0e-6, scale1);
    mCenterLat = std::clamp(mCenterLat, -85.0, 85.0);
    if (mCenterLon > 180.0)
    {
        mCenterLon -= 360.0;
    }
    if (mCenterLon < -180.0)
    {
        mCenterLon += 360.0;
    }
    markDirty();
}

void PlanningMap::toggleBackground()
{
    setBackgroundVector(!mBackgroundVector);
}

void PlanningMap::setBackgroundVector(bool vector)
{
    if (mBackgroundVector == vector)
    {
        return;
    }
    mBackgroundVector = vector;
    mSatCached = -1;
    markDirty();
}

bool PlanningMap::contains(int x, int y) const
{
    return x >= mX && y >= mY && x < mX + mW && y < mY + mH;
}

void PlanningMap::pan(int dx, int dy)
{
    if (mW <= 0 || mH <= 0)
    {
        return;
    }
    const double scale = zoomScale(mZoom);
    const double cosCenter = std::cos(mCenterLat * kPi / 180.0);
    if (std::fabs(scale * cosCenter) < 1.0e-6)
    {
        return;
    }
    mCenterLon -= static_cast<double>(dx) / (scale * cosCenter);
    mCenterLat += static_cast<double>(dy) / scale;
    mCenterLat = std::clamp(mCenterLat, -85.0, 85.0);
    if (mCenterLon > 180.0)
    {
        mCenterLon -= 360.0;
    }
    if (mCenterLon < -180.0)
    {
        mCenterLon += 360.0;
    }
    markDirty();
}

PlanningMap::Point PlanningMap::project(double lat, double lon) const
{
    const double scale = zoomScale(mZoom);
    const double cosCenter = std::cos(mCenterLat * kPi / 180.0);
    const float sx = static_cast<float>(mX + mW / 2 + (lon - mCenterLon) * scale * cosCenter);
    const float sy = static_cast<float>(mY + mH / 2 - (lat - mCenterLat) * scale);
    return {sx, sy};
}

bool PlanningMap::inBox(double minLat, double maxLat, double minLon, double maxLon) const
{
    double viewMinLat = 0.0;
    double viewMaxLat = 0.0;
    double viewMinLon = 0.0;
    double viewMaxLon = 0.0;
    viewBounds(viewMinLat, viewMaxLat, viewMinLon, viewMaxLon);
    if (maxLat < viewMinLat || minLat > viewMaxLat)
    {
        return false;
    }
    if (maxLon < viewMinLon || minLon > viewMaxLon)
    {
        return false;
    }
    return true;
}

void PlanningMap::viewBounds(double &minLat, double &maxLat, double &minLon, double &maxLon) const
{
    const double scale = zoomScale(mZoom);
    const double cosCenter = std::cos(mCenterLat * kPi / 180.0);
    const double halfLatDeg = (mH / 2.0) / std::max(1.0e-6, scale);
    const double halfLonDeg = (mW / 2.0) / std::max(1.0e-6, scale * cosCenter);
    minLat = mCenterLat - halfLatDeg;
    maxLat = mCenterLat + halfLatDeg;
    minLon = mCenterLon - halfLonDeg;
    maxLon = mCenterLon + halfLonDeg;
}

PlanningMap::Point PlanningMap::toSurf(double lat, double lon, int texW, int texH, double minLat, double maxLat,
                                       double minLon, double maxLon) const
{
    const double lonSpan = std::max(1.0e-9, maxLon - minLon);
    const double latSpan = std::max(1.0e-9, maxLat - minLat);
    Point p;
    p.x = static_cast<float>((lon - minLon) / lonSpan * static_cast<double>(texW - 1));
    p.y = static_cast<float>((maxLat - lat) / latSpan * static_cast<double>(texH - 1));
    return p;
}

void PlanningMap::pumpSatellite()
{
    if (mBackgroundVector || mW <= 0 || mH <= 0)
    {
        return;
    }
    double minLat = 0.0;
    double maxLat = 0.0;
    double minLon = 0.0;
    double maxLon = 0.0;
    viewBounds(minLat, maxLat, minLon, maxLon);
    const double lonSpan = std::max(1.0e-6, maxLon - minLon);
    int z = 6;
    for (int cand = 6; cand <= 12; ++cand)
    {
        const double tilesAcross = lonSpan / 360.0 * std::exp2(static_cast<double>(cand));
        if (tilesAcross <= 8.0)
        {
            z = cand;
        }
        else
        {
            break;
        }
    }
    const std::pair<int, int> nw = OpenAipClient::latLonToTile(static_cast<float>(maxLat), static_cast<float>(minLon), z);
    const std::pair<int, int> se = OpenAipClient::latLonToTile(static_cast<float>(minLat), static_cast<float>(maxLon), z);
    const int n = 1 << z;
    int minX = std::max(0, std::min(nw.first, se.first) - 1);
    int maxX = std::min(n - 1, std::max(nw.first, se.first) + 1);
    int minY = std::max(0, std::min(nw.second, se.second) - 1);
    int maxY = std::min(n - 1, std::max(nw.second, se.second) + 1);
    const int camX = (minX + maxX) / 2;
    const int camY = (minY + maxY) / 2;
    int radius = std::max(std::max(maxX - camX, camX - minX), std::max(maxY - camY, camY - minY));
    radius = std::clamp(radius, 1, 8);
    mSatZoom = z;
    mSatMinX = minX;
    mSatMaxX = maxX;
    mSatMinY = minY;
    mSatMaxY = maxY;
    OpenAipClient::instance().fetchAround(static_cast<float>(mCenterLat), static_cast<float>(mCenterLon), z, radius, true,
                                          false);
}

int PlanningMap::countCachedSatTiles() const
{
    if (mBackgroundVector || mSatMaxX < mSatMinX)
    {
        return 0;
    }
    const OpenAipClient &client = OpenAipClient::instance();
    const std::string &layer = client.basemapLayer();
    int count = 0;
    for (int x = mSatMinX; x <= mSatMaxX; ++x)
    {
        for (int y = mSatMinY; y <= mSatMaxY; ++y)
        {
            if (client.isCached(mSatZoom, x, y, layer))
            {
                ++count;
            }
        }
    }
    return count;
}

int PlanningMap::paintSatellite(SDL_Surface *surface, int texW, int texH, double minLat, double maxLat, double minLon,
                                double maxLon)
{
    if (surface == nullptr)
    {
        return 0;
    }
    pumpSatellite();
    const OpenAipClient &client = OpenAipClient::instance();
    const std::string &layer = client.basemapLayer();
    int painted = 0;
    for (int x = mSatMinX; x <= mSatMaxX; ++x)
    {
        for (int y = mSatMinY; y <= mSatMaxY; ++y)
        {
            if (!client.isCached(mSatZoom, x, y, layer))
            {
                continue;
            }
            const std::string path = client.cachePath(mSatZoom, x, y, layer);
            SDL_Surface *tile = IMG_Load(path.c_str());
            if (tile == nullptr)
            {
                continue;
            }
            SDL_Surface *rgba = SDL_ConvertSurfaceFormat(tile, SDL_PIXELFORMAT_RGBA32, 0);
            SDL_FreeSurface(tile);
            if (rgba == nullptr)
            {
                continue;
            }
            SDL_SetSurfaceBlendMode(rgba, SDL_BLENDMODE_NONE);
            const double west = tileWestLon(mSatZoom, x);
            const double east = tileWestLon(mSatZoom, x + 1);
            const double north = tileNorthLat(mSatZoom, y);
            const double south = tileNorthLat(mSatZoom, y + 1);
            const Point nw = toSurf(north, west, texW, texH, minLat, maxLat, minLon, maxLon);
            const Point se = toSurf(south, east, texW, texH, minLat, maxLat, minLon, maxLon);
            SDL_Rect dst;
            dst.x = static_cast<int>(std::floor(std::min(nw.x, se.x)));
            dst.y = static_cast<int>(std::floor(std::min(nw.y, se.y)));
            dst.w = std::max(1, static_cast<int>(std::ceil(std::fabs(se.x - nw.x))));
            dst.h = std::max(1, static_cast<int>(std::ceil(std::fabs(se.y - nw.y))));
            SDL_BlitScaled(rgba, nullptr, surface, &dst);
            SDL_FreeSurface(rgba);
            ++painted;
        }
    }
    return painted;
}

void PlanningMap::rasterize()
{
    const int texW = std::clamp(mW, 8, 1280);
    const int texH = std::clamp(mH, 8, 800);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, texW, texH, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr)
    {
        return;
    }

    const Uint8 bgR = mBackgroundVector ? 13 : 18;
    const Uint8 bgG = mBackgroundVector ? 18 : 27;
    const Uint8 bgB = mBackgroundVector ? 24 : 34;
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, bgR, bgG, bgB, 255));

    double minLat = 0.0;
    double maxLat = 0.0;
    double minLon = 0.0;
    double maxLon = 0.0;
    viewBounds(minLat, maxLat, minLon, maxLon);

    if (!mBackgroundVector)
    {
        paintSatellite(surface, texW, texH, minLat, maxLat, minLon, maxLon);
        mSatCached = countCachedSatTiles();
    }

    const Uint32 gridPx = mBackgroundVector ? SDL_MapRGBA(surface->format, 55, 72, 96, 220)
                                            : SDL_MapRGBA(surface->format, 220, 230, 240, 40);
    auto pick = [](double span) {
        static const double kSteps[] = {0.25, 0.5, 1.0, 2.0, 5.0, 10.0};
        for (double s : kSteps)
        {
            if (span / s <= 8.0)
            {
                return s;
            }
        }
        return 10.0;
    };
    const double latStep = pick(maxLat - minLat);
    const double lonStep = pick(maxLon - minLon);
    for (double lat = std::floor(minLat / latStep) * latStep; lat <= maxLat; lat += latStep)
    {
        const Point a = toSurf(lat, minLon, texW, texH, minLat, maxLat, minLon, maxLon);
        const Point b = toSurf(lat, maxLon, texW, texH, minLat, maxLat, minLon, maxLon);
        paintLine(surface, a.x, a.y, b.x, b.y, 1.0f, gridPx);
    }
    for (double lon = std::floor(minLon / lonStep) * lonStep; lon <= maxLon; lon += lonStep)
    {
        const Point a = toSurf(minLat, lon, texW, texH, minLat, maxLat, minLon, maxLon);
        const Point b = toSurf(maxLat, lon, texW, texH, minLat, maxLat, minLon, maxLon);
        paintLine(surface, a.x, a.y, b.x, b.y, 1.0f, gridPx);
    }

    if (mShowAirspace)
    {
        const std::vector<AirspaceRing> &rings = NavDb::instance().airspaces();
        struct Candidate
        {
            const AirspaceRing *ring = nullptr;
            double area = 0.0;
        };
        std::vector<Candidate> candidates;
        candidates.reserve(256);
        for (const AirspaceRing &r : rings)
        {
            if (!matchesFilter(r, mFilter))
            {
                continue;
            }
            if (!inBox(r.minLat, r.maxLat, r.minLon, r.maxLon))
            {
                continue;
            }
            std::string typeUpper = r.type;
            toUpperInPlace(typeUpper);
            if (typeUpper == "FIR" || typeUpper == "UIR")
            {
                continue;
            }
            Candidate c;
            c.ring = &r;
            c.area = std::max(1.0e-8, (r.maxLat - r.minLat) * (r.maxLon - r.minLon));
            candidates.push_back(c);
        }
        std::vector<Candidate> fillOrder = candidates;
        std::sort(fillOrder.begin(), fillOrder.end(),
                  [](const Candidate &a, const Candidate &b) { return a.area < b.area; });
        const size_t fillLimit = std::min(fillOrder.size(), static_cast<size_t>(kMaxAirspaceFills));
        auto paintOne = [&](const AirspaceRing &r, bool fillOnly) {
            Uint8 fillR = 0;
            Uint8 fillG = 0;
            Uint8 fillB = 0;
            Uint8 fillA = 0;
            Uint8 strokeR = 0;
            Uint8 strokeG = 0;
            Uint8 strokeB = 0;
            Uint8 strokeA = 0;
            float strokeW = 1.5f;
            styleFor(r, fillR, fillG, fillB, fillA, strokeR, strokeG, strokeB, strokeA, strokeW);
            std::vector<TexPt> pts;
            pts.reserve(r.lat.size());
            const size_t step = r.lat.size() > 120 ? std::max<size_t>(1, r.lat.size() / 80) : 1;
            for (size_t i = 0; i < r.lat.size(); i += step)
            {
                const Point p = toSurf(r.lat[i], r.lon[i], texW, texH, minLat, maxLat, minLon, maxLon);
                pts.push_back({p.x, p.y});
            }
            if (pts.size() < 2)
            {
                return;
            }
            if (fillOnly)
            {
                if (mBackgroundVector && r.lat.size() <= 200 && fillA != 0)
                {
                    fillRing(surface, pts.data(), static_cast<int>(pts.size()),
                             SDL_MapRGBA(surface->format, fillR, fillG, fillB, fillA));
                }
                return;
            }
            const Uint32 strokePx = SDL_MapRGBA(surface->format, strokeR, strokeG, strokeB, strokeA);
            for (size_t i = 1; i < pts.size(); ++i)
            {
                paintLine(surface, pts[i - 1].x, pts[i - 1].y, pts[i].x, pts[i].y, strokeW, strokePx);
            }
            paintLine(surface, pts.back().x, pts.back().y, pts.front().x, pts.front().y, strokeW, strokePx);
        };
        if (mBackgroundVector)
        {
            for (size_t i = 0; i < fillLimit; ++i)
            {
                paintOne(*fillOrder[i].ring, true);
            }
        }
        for (const Candidate &cand : candidates)
        {
            paintOne(*cand.ring, false);
        }
    }

    {
        const Uint32 airportPx = SDL_MapRGBA(surface->format, 0, 229, 255, 230);
        const Uint32 stripPx = SDL_MapRGBA(surface->format, 120, 210, 255, 210);
        const Uint32 fixPx = SDL_MapRGBA(surface->format, 153, 177, 192, 200);
        const std::vector<Waypoint> &wpts = NavDb::instance().waypoints();
        int fixDrawn = 0;
        const int airportR = mZoom >= 8.0f ? 3 : (mZoom >= 6.0f ? 2 : 1);
        for (const Waypoint &wpt : wpts)
        {
            if (!inBox(wpt.lat, wpt.lat, wpt.lon, wpt.lon))
            {
                continue;
            }
            const Point p = toSurf(wpt.lat, wpt.lon, texW, texH, minLat, maxLat, minLon, maxLon);
            if (wpt.kind == Waypoint::Kind::Airport)
            {
                const bool published = isPublishedIcao(wpt.ident);
                const int ax = static_cast<int>(std::lround(p.x));
                const int ay = static_cast<int>(std::lround(p.y));
                paintDisk(surface, ax, ay, published ? airportR : std::max(1, airportR - 1),
                          published ? airportPx : stripPx);
                if (published)
                {
                    paintMapLabel(surface, ax, ay, wpt.ident, 12, SDL_Color{0, 229, 255, 255});
                }
                else if (mZoom >= 8.0f && !wpt.name.empty())
                {
                    paintMapLabel(surface, ax, ay, shortenUtf8(wpt.name, 18), 10, SDL_Color{160, 220, 240, 230});
                }
            }
            else if (mZoom >= 8.0f && fixDrawn < kMaxFixesDrawn)
            {
                paintDisk(surface, static_cast<int>(std::lround(p.x)), static_cast<int>(std::lround(p.y)), 1, fixPx);
                ++fixDrawn;
            }
        }
    }

    const Uint32 routePx = SDL_MapRGBA(surface->format, 255, 47, 208, 255);
    const std::vector<std::string> &route = mPlan.route();
    std::vector<Point> routePts(route.size(), Point{0.0f, 0.0f});
    std::vector<char> routeOk(route.size(), 0);
    for (size_t i = 0; i < route.size(); ++i)
    {
        const Waypoint *wpt = NavDb::instance().find(route[i]);
        if (wpt == nullptr)
        {
            continue;
        }
        routePts[i] = toSurf(wpt->lat, wpt->lon, texW, texH, minLat, maxLat, minLon, maxLon);
        routeOk[i] = 1;
    }
    for (size_t i = 1; i < route.size(); ++i)
    {
        if (!routeOk[i - 1] || !routeOk[i])
        {
            continue;
        }
        const float width = static_cast<int>(i) == mPlan.activeLegIndex() ? 4.0f : 3.0f;
        paintLine(surface, routePts[i - 1].x, routePts[i - 1].y, routePts[i].x, routePts[i].y, width, routePx);
    }
    for (size_t i = 0; i < route.size(); ++i)
    {
        if (!routeOk[i])
        {
            continue;
        }
        const int radius = (i == 0 || i + 1 == route.size()) ? 5 : 3;
        paintDisk(surface, static_cast<int>(std::lround(routePts[i].x)), static_cast<int>(std::lround(routePts[i].y)),
                  radius, routePx);
    }

    // Cyan frame so a failed overlay still shows a chart boundary.
    const Uint32 framePx = SDL_MapRGBA(surface->format, 0, 229, 255, 180);
    SDL_Rect top{0, 0, texW, 2};
    SDL_Rect bot{0, texH - 2, texW, 2};
    SDL_Rect left{0, 0, 2, texH};
    SDL_Rect right{texW - 2, 0, 2, texH};
    SDL_FillRect(surface, &top, framePx);
    SDL_FillRect(surface, &bot, framePx);
    SDL_FillRect(surface, &left, framePx);
    SDL_FillRect(surface, &right, framePx);

    mUpload.setTexture(kAtlasName, surface);
    mDirty = false;
    mSeenPlanRevision = mPlan.revision();
}

void PlanningMap::render()
{
    if (mW <= 0 || mH <= 0)
    {
        return;
    }
    if (mPlan.revision() != mSeenPlanRevision)
    {
        markDirty();
    }
    if (!mBackgroundVector)
    {
        pumpSatellite();
        const int cached = countCachedSatTiles();
        if (cached != mSatCached)
        {
            markDirty();
        }
    }
    if (mDirty)
    {
        rasterize();
    }

    const int scrH = mScreen.getHeight();
    const int glY = scrH - (mY + mH);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_SCISSOR_TEST);
    glScissor(mX, glY, mW, mH);

    mPanel.drawTexture(kAtlasName, mX, glY, mW, mH);
    const glm::mat4 identity(1.0f);
    mPanel.setTransformationMatrix(identity);
    mPanel.render();

    const LocationData &loc = mData.getLocationData();
    if (!(loc.latitude == 0.0f && loc.longitude == 0.0f) &&
        inBox(loc.latitude, loc.latitude, loc.longitude, loc.longitude))
    {
        const Point p = project(loc.latitude, loc.longitude);
        const int size = 8;
        const int mx = static_cast<int>(std::lround(p.x)) - size / 2;
        const int my = scrH - static_cast<int>(std::lround(p.y)) - size / 2;
        mMarker.drawRectangle(mx, my, size, size, 0x39FF6AFFu);
        mMarker.setTransformationMatrix(identity);
        mMarker.render();
    }

    glDisable(GL_SCISSOR_TEST);
}
