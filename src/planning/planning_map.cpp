/// \file planning_map.cpp
/// Rasterizes the planner chart to one texture: night vector or Esri satellite
/// tiles, then airspaces, navaids, obstacles, and the route. Drawn with Render2D.
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
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr int kZoomMax = 14;
constexpr float kCloseZoom = 8.0f; ///< Same threshold as non-ICAO airport names.
constexpr int kMaxAirspaceFills = 48;
constexpr int kOwnshipPx = 28;
constexpr float kPingPeriodS = 3.0f;
constexpr float kPingR0 = 16.0f;
constexpr float kPingR1 = 92.0f;
constexpr float kPingStroke = 1.0f;
constexpr int kPingSegs = 72;
constexpr int kMaxObstacleLabels = 48;
const char *kAtlasName = "planmap-atlas";
const char *kOwnshipTex = "plan-ownship";

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

void fillTriangle(SDL_Surface *surface, int x0, int y0, int x1, int y1, int x2, int y2, Uint32 pixel)
{
    const int minX = std::max(0, std::min({x0, x1, x2}));
    const int maxX = std::min(surface != nullptr ? surface->w - 1 : 0, std::max({x0, x1, x2}));
    const int minY = std::max(0, std::min({y0, y1, y2}));
    const int maxY = std::min(surface != nullptr ? surface->h - 1 : 0, std::max({y0, y1, y2}));
    if (surface == nullptr || minX > maxX || minY > maxY)
    {
        return;
    }
    const auto edge = [](int ax, int ay, int bx, int by, int cx, int cy) {
        return static_cast<long>(bx - ax) * static_cast<long>(cy - ay) -
               static_cast<long>(by - ay) * static_cast<long>(cx - ax);
    };
    const long area = edge(x0, y0, x1, y1, x2, y2);
    if (area == 0)
    {
        return;
    }
    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const long w0 = edge(x1, y1, x2, y2, x, y);
            const long w1 = edge(x2, y2, x0, y0, x, y);
            const long w2 = edge(x0, y0, x1, y1, x, y);
            if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
            {
                putPixel(surface, x, y, pixel);
            }
        }
    }
}

void paintIfrTriangle(SDL_Surface *surface, int cx, int cy, int size, Uint32 pixel)
{
    const int s = std::max(3, size);
    fillTriangle(surface, cx, cy - s, cx - s, cy + s, cx + s, cy + s, pixel);
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

/// Five-letter ICAO RNAV enroute intersections (ANAKO, GUBKO). Drops SID/STAR
/// numbered procedure waypoints such as PR530.
bool isEnrouteIfrFix(const std::string &ident)
{
    if (ident.size() != 5)
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

bool isSnappableWaypoint(const Waypoint &wpt, bool ifrOn)
{
    if (wpt.kind == Waypoint::Kind::Airport)
    {
        return true;
    }
    if (!ifrOn)
    {
        return false;
    }
    if (wpt.kind == Waypoint::Kind::Vor || wpt.kind == Waypoint::Kind::Ndb)
    {
        return true;
    }
    return isEnrouteIfrFix(wpt.ident);
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

void paintMapLabel(SDL_Surface *surface, int cx, int cy, const std::string &text, int fontPx, SDL_Color color,
                   bool centered = false)
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
    dst.x = centered ? cx - label->w / 2 : cx + 5;
    dst.y = cy - label->h / 2;
    dst.w = label->w;
    dst.h = label->h;
    SDL_BlitSurface(label, nullptr, surface, &dst);
    SDL_FreeSurface(label);
}

void paintHaloLabel(SDL_Surface *surface, int cx, int cy, const std::string &text, int fontPx, SDL_Color color,
                    bool centered = false)
{
    const SDL_Color ink{8, 10, 14, 255};
    paintMapLabel(surface, cx - 1, cy, text, fontPx, ink, centered);
    paintMapLabel(surface, cx + 1, cy, text, fontPx, ink, centered);
    paintMapLabel(surface, cx, cy - 1, text, fontPx, ink, centered);
    paintMapLabel(surface, cx, cy + 1, text, fontPx, ink, centered);
    paintMapLabel(surface, cx, cy, text, fontPx, color, centered);
}

std::string formatLegEte(float minutes)
{
    if (minutes < 0.5f)
    {
        return "--";
    }
    const int total = static_cast<int>(std::lround(minutes));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", total / 60, total % 60);
    return buf;
}

void fillRoundRect(SDL_Surface *surface, int x, int y, int w, int h, int radius, Uint32 pixel)
{
    if (surface == nullptr || w <= 0 || h <= 0)
    {
        return;
    }
    const int r = std::clamp(radius, 1, std::min(w, h) / 2);
    SDL_Rect mid{x + r, y, w - 2 * r, h};
    SDL_Rect side{x, y + r, w, h - 2 * r};
    SDL_FillRect(surface, &mid, pixel);
    SDL_FillRect(surface, &side, pixel);
    paintDisk(surface, x + r, y + r, r, pixel);
    paintDisk(surface, x + w - 1 - r, y + r, r, pixel);
    paintDisk(surface, x + r, y + h - 1 - r, r, pixel);
    paintDisk(surface, x + w - 1 - r, y + h - 1 - r, r, pixel);
}

void blendPixel(SDL_Surface *dst, int x, int y, Uint8 sr, Uint8 sg, Uint8 sb, Uint8 sa)
{
    if (dst == nullptr || sa == 0 || x < 0 || y < 0 || x >= dst->w || y >= dst->h)
    {
        return;
    }
    Uint32 *row = reinterpret_cast<Uint32 *>(static_cast<Uint8 *>(dst->pixels) + y * dst->pitch);
    Uint8 dr = 0;
    Uint8 dg = 0;
    Uint8 db = 0;
    Uint8 da = 0;
    SDL_GetRGBA(row[x], dst->format, &dr, &dg, &db, &da);
    const float a = static_cast<float>(sa) / 255.0f;
    const float ia = 1.0f - a;
    const Uint8 or_ = static_cast<Uint8>(std::lround(static_cast<float>(sr) * a + static_cast<float>(dr) * ia));
    const Uint8 og = static_cast<Uint8>(std::lround(static_cast<float>(sg) * a + static_cast<float>(dg) * ia));
    const Uint8 ob = static_cast<Uint8>(std::lround(static_cast<float>(sb) * a + static_cast<float>(db) * ia));
    const Uint8 oa = static_cast<Uint8>(std::lround(static_cast<float>(sa) + static_cast<float>(da) * ia));
    row[x] = SDL_MapRGBA(dst->format, or_, og, ob, oa);
}

TTF_Font *b612At(int px)
{
    static std::unordered_map<int, TTF_Font *> fonts;
    const auto it = fonts.find(px);
    if (it != fonts.end())
    {
        return it->second;
    }
    if (TTF_Init() != 0)
    {
        fonts[px] = nullptr;
        return nullptr;
    }
    TTF_Font *font = TTF_OpenFont(AssetPath::resolve("resources/fonts/B612Mono-Regular.ttf").c_str(), px);
    if (font != nullptr)
    {
        TTF_SetFontStyle(font, TTF_STYLE_NORMAL);
    }
    fonts[px] = font;
    return font;
}

SDL_Surface *makeLegBadge(const std::string &distTime, const std::string &hdg, bool active)
{
    const SDL_Color ink = active ? SDL_Color{255, 90, 220, 255} : SDL_Color{255, 210, 245, 255};
    TTF_Font *topFont = b612At(12);
    TTF_Font *hdgFont = b612At(11);
    if (topFont == nullptr || hdgFont == nullptr)
    {
        return nullptr;
    }
    SDL_Surface *top = TTF_RenderUTF8_Blended(topFont, distTime.c_str(), ink);
    SDL_Surface *bot = TTF_RenderUTF8_Blended(hdgFont, hdg.c_str(), ink);
    if (top == nullptr || bot == nullptr)
    {
        SDL_FreeSurface(top);
        SDL_FreeSurface(bot);
        return nullptr;
    }
    const int padX = 12;
    const int padY = 7;
    const int gap = 1;
    const int w = std::max(top->w, bot->w) + padX * 2;
    const int h = top->h + bot->h + gap + padY * 2;
    SDL_Surface *box = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    if (box == nullptr)
    {
        SDL_FreeSurface(top);
        SDL_FreeSurface(bot);
        return nullptr;
    }
    SDL_FillRect(box, nullptr, SDL_MapRGBA(box->format, 0, 0, 0, 0));
    const Uint32 fill = SDL_MapRGBA(box->format, 8, 12, 18, 170);
    fillRoundRect(box, 0, 0, w, h, h / 2, fill);
    SDL_SetSurfaceBlendMode(top, SDL_BLENDMODE_BLEND);
    SDL_SetSurfaceBlendMode(bot, SDL_BLENDMODE_BLEND);
    SDL_Rect t1{(w - top->w) / 2, padY, top->w, top->h};
    SDL_Rect t2{(w - bot->w) / 2, padY + top->h + gap, bot->w, bot->h};
    SDL_BlitSurface(top, nullptr, box, &t1);
    SDL_BlitSurface(bot, nullptr, box, &t2);
    SDL_FreeSurface(top);
    SDL_FreeSurface(bot);
    return box;
}

void blitRotated(SDL_Surface *dst, SDL_Surface *src, float cx, float cy, float angleRad)
{
    if (dst == nullptr || src == nullptr)
    {
        return;
    }
    const float c = std::cos(angleRad);
    const float s = std::sin(angleRad);
    const float ocx = static_cast<float>(src->w) * 0.5f;
    const float ocy = static_cast<float>(src->h) * 0.5f;
    const float extX = std::fabs(c) * ocx + std::fabs(s) * ocy;
    const float extY = std::fabs(s) * ocx + std::fabs(c) * ocy;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - extX)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - extY)));
    const int x1 = std::min(dst->w - 1, static_cast<int>(std::ceil(cx + extX)));
    const int y1 = std::min(dst->h - 1, static_cast<int>(std::ceil(cy + extY)));
    for (int y = y0; y <= y1; ++y)
    {
        for (int x = x0; x <= x1; ++x)
        {
            const float dx = static_cast<float>(x) + 0.5f - cx;
            const float dy = static_cast<float>(y) + 0.5f - cy;
            const float sx = c * dx + s * dy + ocx - 0.5f;
            const float sy = -s * dx + c * dy + ocy - 0.5f;
            const int ix = static_cast<int>(std::floor(sx));
            const int iy = static_cast<int>(std::floor(sy));
            const float tx = sx - static_cast<float>(ix);
            const float ty = sy - static_cast<float>(iy);
            auto fetch = [&](int px, int py, float &r, float &g, float &b, float &a) {
                if (px < 0 || py < 0 || px >= src->w || py >= src->h)
                {
                    r = g = b = a = 0.0f;
                    return;
                }
                const Uint32 *row =
                    reinterpret_cast<const Uint32 *>(static_cast<const Uint8 *>(src->pixels) + py * src->pitch);
                Uint8 ir = 0;
                Uint8 ig = 0;
                Uint8 ib = 0;
                Uint8 ia = 0;
                SDL_GetRGBA(row[px], src->format, &ir, &ig, &ib, &ia);
                r = static_cast<float>(ir);
                g = static_cast<float>(ig);
                b = static_cast<float>(ib);
                a = static_cast<float>(ia);
            };
            float r00 = 0;
            float g00 = 0;
            float b00 = 0;
            float a00 = 0;
            float r10 = 0;
            float g10 = 0;
            float b10 = 0;
            float a10 = 0;
            float r01 = 0;
            float g01 = 0;
            float b01 = 0;
            float a01 = 0;
            float r11 = 0;
            float g11 = 0;
            float b11 = 0;
            float a11 = 0;
            fetch(ix, iy, r00, g00, b00, a00);
            fetch(ix + 1, iy, r10, g10, b10, a10);
            fetch(ix, iy + 1, r01, g01, b01, a01);
            fetch(ix + 1, iy + 1, r11, g11, b11, a11);
            const float r0 = r00 + (r10 - r00) * tx;
            const float g0 = g00 + (g10 - g00) * tx;
            const float b0 = b00 + (b10 - b00) * tx;
            const float a0 = a00 + (a10 - a00) * tx;
            const float r1 = r01 + (r11 - r01) * tx;
            const float g1 = g01 + (g11 - g01) * tx;
            const float b1 = b01 + (b11 - b01) * tx;
            const float a1 = a01 + (a11 - a01) * tx;
            const Uint8 r = static_cast<Uint8>(std::lround(r0 + (r1 - r0) * ty));
            const Uint8 g = static_cast<Uint8>(std::lround(g0 + (g1 - g0) * ty));
            const Uint8 b = static_cast<Uint8>(std::lround(b0 + (b1 - b0) * ty));
            const Uint8 a = static_cast<Uint8>(std::lround(a0 + (a1 - a0) * ty));
            if (a == 0)
            {
                continue;
            }
            blendPixel(dst, x, y, r, g, b, a);
        }
    }
}

const char *obstacleKindWord(ObstaclePoint::Kind kind)
{
    switch (kind)
    {
    case ObstaclePoint::Kind::Wind:
        return "WIND";
    case ObstaclePoint::Kind::Chimney:
        return "CHIMNEY";
    case ObstaclePoint::Kind::Tower:
        return "TOWER";
    case ObstaclePoint::Kind::Building:
        return "BLDG";
    default:
        return "OBST";
    }
}

std::string obstacleLabel(const ObstaclePoint &obs)
{
    std::string text = obs.name;
    if (text.empty())
    {
        text = obstacleKindWord(obs.kind);
    }
    else
    {
        text = shortenUtf8(text, 16);
    }
    if (obs.heightM > 1.0f)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), " %.0fm", static_cast<double>(obs.heightM));
        text += buf;
    }
    return text;
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
    Fis,
    Other,
};

bool isFisAirspace(const AirspaceRing &r)
{
    std::string t = r.type;
    std::string n = r.name;
    toUpperInPlace(t);
    toUpperInPlace(n);
    if (t == "MIL_EXERCISE")
    {
        return true;
    }
    return n.rfind("FIS ", 0) == 0 || n.rfind("FIS-", 0) == 0;
}

AspCat categorize(const AirspaceRing &r)
{
    std::string t = r.type;
    std::string n = r.name;
    toUpperInPlace(t);
    toUpperInPlace(n);
    if (isFisAirspace(r))
    {
        return AspCat::Fis;
    }
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
    case AspCat::Fis:
        fillR = 180;
        fillG = 160;
        fillB = 90;
        fillA = 22;
        strokeR = 200;
        strokeG = 180;
        strokeB = 90;
        strokeA = 160;
        strokeW = 1.4f;
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

void appendAnnulus(Triangles &mesh, float cx, float cy, float rInner, float rOuter, int segs)
{
    if (rOuter <= rInner || segs < 8)
    {
        return;
    }
    const GLuint base = static_cast<GLuint>(mesh.vertex.size());
    for (int i = 0; i <= segs; ++i)
    {
        const float a = static_cast<float>(i) / static_cast<float>(segs) * 2.0f * kPi;
        const float c = std::cos(a);
        const float s = std::sin(a);
        VertexTexture outer{};
        outer.vertex.x = cx + c * rOuter;
        outer.vertex.y = cy + s * rOuter;
        VertexTexture inner{};
        inner.vertex.x = cx + c * rInner;
        inner.vertex.y = cy + s * rInner;
        inner.textureCoord.x = 1.0f;
        inner.textureCoord.y = 1.0f;
        mesh.vertex.push_back(outer);
        mesh.vertex.push_back(inner);
    }
    for (int i = 0; i < segs; ++i)
    {
        const GLuint i0 = base + static_cast<GLuint>(i * 2);
        mesh.indices.push_back(i0);
        mesh.indices.push_back(i0 + 1);
        mesh.indices.push_back(i0 + 2);
        mesh.indices.push_back(i0 + 1);
        mesh.indices.push_back(i0 + 3);
        mesh.indices.push_back(i0 + 2);
    }
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
        mCentered = true;
        markDirty();
        return;
    }

    mCenterLat = (minLat + maxLat) / 2.0;
    mCenterLon = (minLon + maxLon) / 2.0;
    const double latSpan = std::max(0.02, maxLat - minLat);
    const double lonSpan = std::max(0.02, maxLon - minLon);
    const double cosLat = std::max(0.15, std::cos(mCenterLat * kPi / 180.0));
    const double mapW = static_cast<double>(std::max(1, mW));
    const double mapH = static_cast<double>(std::max(1, mH));
    constexpr double kFill = 0.82;
    const double scaleLat = (mapH * kFill) / latSpan;
    const double scaleLon = (mapW * kFill) / (lonSpan * cosLat);
    const double scale = std::min(scaleLat, scaleLon);
    const double z = std::log(std::max(1.0e-6, scale / 22.0)) / std::log(1.8);
    mZoom = std::min(static_cast<float>(z), static_cast<float>(kZoomMax));
    mCentered = true;
    markDirty();
}

void PlanningMap::zoom(int delta)
{
    const float next = std::min(mZoom + static_cast<float>(delta), static_cast<float>(kZoomMax));
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
    const float next = std::min(mZoom + delta, static_cast<float>(kZoomMax));
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

void PlanningMap::latLonToScreen(double lat, double lon, int &x, int &y) const
{
    const Point p = project(lat, lon);
    x = static_cast<int>(std::lround(p.x));
    y = static_cast<int>(std::lround(p.y));
}

int PlanningMap::hitRoutePoint(int x, int y, float maxPx) const
{
    int best = -1;
    float bestD2 = maxPx * maxPx;
    NavDb &db = NavDb::instance();
    const std::vector<std::string> &route = mPlan.route();
    for (int i = 0; i < static_cast<int>(route.size()); ++i)
    {
        const Waypoint *wpt = db.find(route[static_cast<size_t>(i)]);
        if (wpt == nullptr)
        {
            continue;
        }
        const Point p = project(wpt->lat, wpt->lon);
        const float dx = p.x - static_cast<float>(x);
        const float dy = p.y - static_cast<float>(y);
        const float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2)
        {
            bestD2 = d2;
            best = i;
        }
    }
    return best;
}

bool PlanningMap::nearestMarked(int x, int y, float maxPx, std::string &ident, double &lat, double &lon) const
{
    double minLat = 0.0;
    double maxLat = 0.0;
    double minLon = 0.0;
    double maxLon = 0.0;
    viewBounds(minLat, maxLat, minLon, maxLon);
    const Waypoint *best = nullptr;
    float bestD2 = maxPx * maxPx;
    for (const Waypoint &wpt : NavDb::instance().waypoints())
    {
        if (!isSnappableWaypoint(wpt, mShowIfr))
        {
            continue;
        }
        if (wpt.lat < minLat || wpt.lat > maxLat || wpt.lon < minLon || wpt.lon > maxLon)
        {
            continue;
        }
        const Point p = project(wpt.lat, wpt.lon);
        const float dx = p.x - static_cast<float>(x);
        const float dy = p.y - static_cast<float>(y);
        const float d2 = dx * dx + dy * dy;
        if (d2 < bestD2)
        {
            bestD2 = d2;
            best = &wpt;
        }
    }
    if (best == nullptr)
    {
        return false;
    }
    ident = best->ident;
    lat = best->lat;
    lon = best->lon;
    return true;
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
            const bool fis = isFisAirspace(r);
            if (fis)
            {
                if (!mShowFis)
                {
                    continue;
                }
            }
            else if (!matchesFilter(r, mFilter))
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
        std::unordered_set<std::string> routeIdents;
        for (const std::string &ident : mPlan.route())
        {
            std::string key = ident;
            toUpperInPlace(key);
            if (!key.empty())
            {
                routeIdents.insert(std::move(key));
            }
        }
        const std::vector<Waypoint> &wpts = NavDb::instance().waypoints();
        const int airportR = mZoom >= kCloseZoom ? 3 : (mZoom >= 6.0f ? 2 : 1);
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
                else if (mZoom >= kCloseZoom && !wpt.name.empty())
                {
                    paintMapLabel(surface, ax, ay, shortenUtf8(wpt.name, 18), 10, SDL_Color{160, 220, 240, 230});
                }
            }
            else if (mShowIfr)
            {
                const bool navaid = wpt.kind == Waypoint::Kind::Vor || wpt.kind == Waypoint::Kind::Ndb;
                const bool rnavFix = isEnrouteIfrFix(wpt.ident);
                if (!navaid && !rnavFix)
                {
                    continue;
                }
                if (isEnrouteIfrFix(wpt.ident) && routeIdents.count(wpt.ident) != 0)
                {
                    continue;
                }
                const int fx = static_cast<int>(std::lround(p.x));
                const int fy = static_cast<int>(std::lround(p.y));
                Uint32 px = fixPx;
                int tri = 4;
                SDL_Color labelCol{180, 196, 210, 230};
                if (wpt.kind == Waypoint::Kind::Vor)
                {
                    px = SDL_MapRGBA(surface->format, 255, 80, 200, 230);
                    tri = 6;
                    labelCol = SDL_Color{255, 140, 220, 255};
                }
                else if (wpt.kind == Waypoint::Kind::Ndb)
                {
                    px = SDL_MapRGBA(surface->format, 255, 183, 0, 230);
                    tri = 5;
                    labelCol = SDL_Color{255, 200, 80, 255};
                }
                paintIfrTriangle(surface, fx, fy, tri, px);
                const std::string &label = !wpt.ident.empty() ? wpt.ident : wpt.name;
                if (!label.empty())
                {
                    paintMapLabel(surface, fx, fy, label, 10, labelCol);
                }
            }
        }
    }

    if (mZoom >= kCloseZoom)
    {
        const Uint32 windPx = SDL_MapRGBA(surface->format, 244, 244, 244, 230);
        const Uint32 chimneyPx = SDL_MapRGBA(surface->format, 226, 90, 40, 240);
        const Uint32 towerPx = SDL_MapRGBA(surface->format, 240, 192, 48, 240);
        const Uint32 buildingPx = SDL_MapRGBA(surface->format, 138, 160, 192, 230);
        const Uint32 otherPx = SDL_MapRGBA(surface->format, 200, 200, 200, 220);
        auto obstColor = [&](ObstaclePoint::Kind kind) {
            switch (kind)
            {
            case ObstaclePoint::Kind::Wind:
                return windPx;
            case ObstaclePoint::Kind::Chimney:
                return chimneyPx;
            case ObstaclePoint::Kind::Tower:
                return towerPx;
            case ObstaclePoint::Kind::Building:
                return buildingPx;
            default:
                return otherPx;
            }
        };
        struct ObstLabel
        {
            int x = 0;
            int y = 0;
            float heightM = 0.0f;
            const ObstaclePoint *obs = nullptr;
        };
        std::vector<ObstLabel> labels;
        labels.reserve(64);
        for (const ObstaclePoint &obs : NavDb::instance().obstacles())
        {
            if (!inBox(obs.lat, obs.lat, obs.lon, obs.lon))
            {
                continue;
            }
            const Point p = toSurf(obs.lat, obs.lon, texW, texH, minLat, maxLat, minLon, maxLon);
            const int ox = static_cast<int>(std::lround(p.x));
            const int oy = static_cast<int>(std::lround(p.y));
            const int radius = obs.heightM >= 100.0f ? 3 : 2;
            paintDisk(surface, ox, oy, radius, obstColor(obs.kind));
            labels.push_back({ox, oy, obs.heightM, &obs});
        }
        std::sort(labels.begin(), labels.end(),
                  [](const ObstLabel &a, const ObstLabel &b) { return a.heightM > b.heightM; });
        const size_t labelLimit = std::min(labels.size(), static_cast<size_t>(kMaxObstacleLabels));
        for (size_t i = 0; i < labelLimit; ++i)
        {
            const ObstLabel &row = labels[i];
            paintMapLabel(surface, row.x, row.y, obstacleLabel(*row.obs), 10, SDL_Color{240, 220, 160, 240});
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
    for (size_t i = 1; i < route.size(); ++i)
    {
        if (!routeOk[i - 1] || !routeOk[i])
        {
            continue;
        }
        const Waypoint *from = NavDb::instance().find(route[i - 1]);
        const Waypoint *to = NavDb::instance().find(route[i]);
        if (from == nullptr || to == nullptr)
        {
            continue;
        }
        const float distNm = FlightPlan::distanceNm(from->lat, from->lon, to->lat, to->lon);
        const float trackDeg = FlightPlan::bearingDeg(from->lat, from->lon, to->lat, to->lon);
        const WindResult wind = FlightPlan::windCorrection(trackDeg, mPlan.tasKt(), mPlan.windDirDeg(), mPlan.windSpdKt());
        float eteMin = 0.0f;
        if (wind.groundSpeedKt > 0.0f && distNm > 0.0f)
        {
            eteMin = std::round((distNm / wind.groundSpeedKt) * 60.0f);
        }
        char distTime[32];
        char hdg[24];
        std::snprintf(distTime, sizeof(distTime), "%.0fNM  %s", static_cast<double>(distNm),
                      formatLegEte(eteMin).c_str());
        std::snprintf(hdg, sizeof(hdg), "HDG %03d", static_cast<int>(std::lround(wind.headingDeg)));
        const bool active = static_cast<int>(i) == mPlan.activeLegIndex();
        SDL_Surface *badge = makeLegBadge(distTime, hdg, active);
        if (badge == nullptr)
        {
            continue;
        }
        const float mx = (routePts[i - 1].x + routePts[i].x) * 0.5f;
        const float my = (routePts[i - 1].y + routePts[i].y) * 0.5f;
        const float dx = routePts[i].x - routePts[i - 1].x;
        const float dy = routePts[i].y - routePts[i - 1].y;
        float angle = std::atan2(dy, dx);
        if (angle > kPi * 0.5f)
        {
            angle -= kPi;
        }
        else if (angle < -kPi * 0.5f)
        {
            angle += kPi;
        }
        blitRotated(surface, badge, mx, my, angle);
        SDL_FreeSurface(badge);
    }
    for (size_t i = 0; i < route.size(); ++i)
    {
        if (!routeOk[i])
        {
            continue;
        }
        const int rx = static_cast<int>(std::lround(routePts[i].x));
        const int ry = static_cast<int>(std::lround(routePts[i].y));
        std::string ident = route[i];
        toUpperInPlace(ident);
        if (isEnrouteIfrFix(ident))
        {
            const Uint32 outline = SDL_MapRGBA(surface->format, 8, 10, 14, 255);
            paintIfrTriangle(surface, rx, ry, 9, outline);
            paintIfrTriangle(surface, rx, ry, 7, routePx);
        }
        else
        {
            const int radius = (i == 0 || i + 1 == route.size()) ? 5 : 3;
            paintDisk(surface, rx, ry, radius, routePx);
        }
    }
    for (size_t i = 0; i < route.size(); ++i)
    {
        if (!routeOk[i])
        {
            continue;
        }
        std::string ident = route[i];
        toUpperInPlace(ident);
        if (!isEnrouteIfrFix(ident))
        {
            continue;
        }
        const int rx = static_cast<int>(std::lround(routePts[i].x));
        const int ry = static_cast<int>(std::lround(routePts[i].y));
        paintHaloLabel(surface, rx, ry, ident, 14, SDL_Color{255, 80, 220, 255});
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
        drawOwnship(p.x, static_cast<float>(scrH) - p.y);
    }

    glDisable(GL_SCISSOR_TEST);
}

void PlanningMap::ensureOwnshipArt()
{
    static bool ready = false;
    if (ready)
    {
        return;
    }
    ready = true;
    SDL_Surface *art = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
    if (art == nullptr)
    {
        return;
    }
    SDL_FillRect(art, nullptr, SDL_MapRGBA(art->format, 0, 0, 0, 0));
    const Uint32 outline = SDL_MapRGBA(art->format, 10, 32, 16, 255);
    const Uint32 fill = SDL_MapRGBA(art->format, 57, 255, 106, 255);
    const Uint32 core = SDL_MapRGBA(art->format, 240, 255, 246, 255);
    fillTriangle(art, 32, 2, 6, 60, 58, 60, outline);
    fillTriangle(art, 32, 10, 14, 54, 50, 54, fill);
    paintDisk(art, 32, 40, 4, core);
    mUpload.setTexture(kOwnshipTex, art);
}

void PlanningMap::drawOwnship(float glX, float glY)
{
    ensureOwnshipArt();

    const int scrW = mScreen.getWidth();
    const int scrH = mScreen.getHeight();
    glm::mat4 mvp(1.0f);
    mvp[0][0] = 2.0f / static_cast<float>(scrW);
    mvp[1][1] = 2.0f / static_cast<float>(scrH);
    mvp[3][0] = -1.0f;
    mvp[3][1] = -1.0f;
    mvp[3][2] = -0.02f;
    mPing.setMvpMatrix(mvp);

    const float t = static_cast<float>(SDL_GetTicks64()) / 1000.0f;
    std::vector<Triangles> rings;
    rings.reserve(2);
    for (int i = 0; i < 2; ++i)
    {
        float phase = std::fmod(t / kPingPeriodS + static_cast<float>(i) * 0.5f, 1.0f);
        if (phase < 0.0f)
        {
            phase += 1.0f;
        }
        const float radius = kPingR0 + (kPingR1 - kPingR0) * phase;
        const float inner = std::max(0.25f, radius - kPingStroke * 0.5f);
        const float outer = inner + kPingStroke;
        const float fade = (1.0f - phase) * (1.0f - phase);
        const uint8_t alpha = static_cast<uint8_t>(std::lround(fade * 220.0f));
        if (alpha < 10)
        {
            continue;
        }
        const uint32_t rgba = 0x39FF6A00u | static_cast<uint32_t>(alpha);
        const std::string name = std::string("plan-ownship-ping-") + std::to_string(i);
        mPing.setColor(name, rgba);
        Triangles mesh;
        mesh.material = name;
        appendAnnulus(mesh, glX, glY, inner, outer, kPingSegs);
        rings.push_back(std::move(mesh));
    }
    mPing.clearGeometry();
    if (!rings.empty())
    {
        mPing.setTriangles(rings);
        mPing.render();
    }

    const int size = kOwnshipPx;
    const int mx = static_cast<int>(std::lround(glX)) - size / 2;
    const int my = static_cast<int>(std::lround(glY)) - size / 2;
    mMarker.drawTexture(kOwnshipTex, mx, my, size, size);
    mMarker.setTransformationMatrix(glm::mat4(1.0f));
    mMarker.render();
}
