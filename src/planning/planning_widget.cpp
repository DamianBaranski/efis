/// \file planning_widget.cpp
/// Draws the Route, Setup, Briefing, and Map tabs of the flight planner and
/// dispatches touch and keyboard events across them.
#include "planning_widget.h"

#include "app_controller.h"
#include "asset_path.h"
#include "frame.h"
#include "menu_widget.h"
#include "nav_db.h"

#include <GLES3/gl3.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>

namespace
{
constexpr uint32_t kBg = 0x0A0D10FFu;
constexpr uint32_t kCard = 0x131920F0u;
constexpr uint32_t kCardSubtle = 0x19222CE0u;
constexpr uint32_t kBorder = 0x273444FFu;
constexpr uint32_t kBorderActive = 0x4D9FFFFFu;
constexpr uint32_t kInk = 0xF0F4F8FFu;
constexpr uint32_t kMuted = 0x8899A8FFu;
constexpr uint32_t kMagenta = 0xFF2FD0FFu;
constexpr uint32_t kGreen = 0x39FF6AFFu;
constexpr uint32_t kAmber = 0xFFB700FFu;
constexpr uint32_t kRed = 0xFF3B47FFu;
constexpr uint32_t kCyan = 0x00E5FFFFu;

constexpr uint32_t kButtonBg = 0x2A3848FFu;
constexpr uint32_t kButtonBgHi = 0x365274FFu;
constexpr uint32_t kButtonBgMag = 0x8C1A78FFu;
constexpr uint32_t kButtonBgGrn = 0x1A6E31FFu;

std::string toUpper(std::string s)
{
    for (char &c : s)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

/// Splits `text` on whitespace. Empty tokens are dropped.
std::vector<std::string> tokenize(const std::string &text)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : text)
    {
        if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (!cur.empty())
            {
                out.push_back(cur);
                cur.clear();
            }
        }
        else
        {
            cur.push_back(c);
        }
    }
    if (!cur.empty())
    {
        out.push_back(cur);
    }
    return out;
}

std::string formatMinutes(float minutes)
{
    if (minutes < 0.5f)
    {
        return "--";
    }
    const int total = static_cast<int>(std::lround(minutes));
    const int h = total / 60;
    const int m = total % 60;
    char buf[16];
    if (h > 0)
    {
        std::snprintf(buf, sizeof(buf), "%d:%02d", h, m);
    }
    else
    {
        std::snprintf(buf, sizeof(buf), "%d MIN", m);
    }
    return buf;
}

std::string formatEteShort(float minutes)
{
    if (minutes < 0.5f)
    {
        return "--";
    }
    const int total = static_cast<int>(std::lround(minutes));
    const int h = total / 60;
    const int m = total % 60;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", h, m);
    return buf;
}

std::string routeToString(const std::vector<std::string> &route)
{
    std::string out;
    for (size_t i = 0; i < route.size(); ++i)
    {
        if (i > 0)
        {
            out.push_back(' ');
        }
        out += route[i];
    }
    return out;
}

std::string clockUtc()
{
    std::time_t now = std::time(nullptr);
    std::tm utc{};
    gmtime_r(&now, &utc);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02dZ", utc.tm_hour, utc.tm_min);
    return buf;
}
} // namespace

PlanningWidget::PlanningWidget(Frame &frame, AppController &controller, IDataManager &data, FlightPlan &plan)
    : IWidget(frame), mFrame(frame), mController(controller), mData(data), mPlan(plan),
      mMap(frame.screen(), data, plan)
{
    (void)mController;
    mRouteBuffer = routeToString(mPlan.route());
    mCaretStart = std::chrono::steady_clock::now();
    mEnabled = false;
}

void PlanningWidget::bindMenu(MenuWidget &menu)
{
    mMenu = &menu;
}

void PlanningWidget::updateTextInput()
{
    mFrame.setTextInputActive(mEnabled && mFocus != Focus::None);
}

void PlanningWidget::loadPreset(const std::vector<std::string> &route)
{
    if (route.size() < 2)
    {
        return;
    }
    mPlan.setRoute(route);
    mPlan.setActiveLegIndex(1);
    mRouteBuffer = routeToString(mPlan.route());
    showToast(std::string("LOADED PRESET: ") + route.front() + " -> " + route.back());
}

void PlanningWidget::setInitialTab(int tabIndex)
{
    switch (tabIndex)
    {
    case 0: mTab = Tab::Route; break;
    case 1: mTab = Tab::Setup; break;
    case 2: mTab = Tab::Briefing; break;
    case 3: mTab = Tab::Map; break;
    default: break;
    }
}

void PlanningWidget::setInitialMapSatellite(bool satellite)
{
    mMap.setBackgroundVector(!satellite);
}

void PlanningWidget::enable(bool enable)
{
    if (enable == mEnabled)
    {
        return;
    }
    mEnabled = enable;
    if (enable)
    {
        mRouteBuffer = routeToString(mPlan.route());
    }
    else
    {
        mFocus = Focus::None;
        mModalOpen = false;
        cancelRouteDrag();
        mEditMode = false;
    }
    updateTextInput();
}

Render2D &PlanningWidget::nextDraw()
{
    if (mDrawCursor >= mDraws.size())
    {
        mDraws.emplace_back(std::make_unique<Render2D>(mScreen));
    }
    return *mDraws[mDrawCursor++];
}

void PlanningWidget::addRect(int x, int y, int w, int h, std::function<void()> action)
{
    HitRect r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    r.action = std::move(action);
    mHits.push_back(std::move(r));
}

void PlanningWidget::drawButton(const std::string &cacheKey, const std::string &label, int x, int y, int w, int h,
                                uint32_t fill, uint32_t textColor, float fontPx, std::function<void()> action)
{
    const int glY = mScreenH - y - h;
    Render2D &box = nextDraw();
    box.drawRectangle(x, glY, w, h, fill);
    Render2D &txt = nextDraw();
    txt.drawTextCentered(label, fontPx, static_cast<float>(x + w / 2), static_cast<float>(glY + h / 2), textColor,
                         cacheKey.c_str());
    addRect(x, y, w, h, std::move(action));
}

void PlanningWidget::showToast(const std::string &text)
{
    mToast = text;
    mToastUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(2200);
}

void PlanningWidget::applyRouteString()
{
    parseRouteString(mRouteBuffer);
    showToast("ROUTE APPLIED");
    mFocus = Focus::None;
    updateTextInput();
}

void PlanningWidget::parseRouteString(const std::string &str)
{
    std::vector<std::string> toks = tokenize(str);
    if (toks.size() < 2)
    {
        return;
    }
    for (std::string &t : toks)
    {
        t = toUpper(t);
    }
    mPlan.setRoute(toks);
    mPlan.setActiveLegIndex(std::min(1, static_cast<int>(toks.size()) - 1));
    mRouteBuffer = routeToString(mPlan.route());
}

void PlanningWidget::openSearchModal()
{
    mModalOpen = true;
    mSearchQuery.clear();
    mSearchResults.clear();
    mFocus = Focus::Search;
    updateTextInput();
}

void PlanningWidget::closeSearchModal()
{
    mModalOpen = false;
    mSearchQuery.clear();
    mSearchResults.clear();
    if (mFocus == Focus::Search)
    {
        mFocus = Focus::None;
    }
    updateTextInput();
}

void PlanningWidget::doWaypointSearch(const std::string &query)
{
    mSearchResults.clear();
    const std::vector<const Waypoint *> hits = NavDb::instance().search(query, 12);
    for (const Waypoint *w : hits)
    {
        std::string entry = w->ident;
        if (!w->name.empty())
        {
            entry += "  ";
            entry += w->name;
        }
        mSearchResults.push_back(entry);
    }
}

bool PlanningWidget::tapIsOnModeChip(int x, int y) const
{
    const int chipW = std::max(168, mScreenW / 10);
    const int chipH = std::max(48, mScreenH / 14);
    return x >= 0 && y >= 0 && x < chipW && y < chipH;
}

void PlanningWidget::layout()
{
    mScreenW = std::max(1, mScreen.getWidth());
    mScreenH = std::max(1, mScreen.getHeight());
}

void PlanningWidget::drawBackground()
{
    Render2D &bg = nextDraw();
    bg.drawRectangle(0, 0, mScreenW, mScreenH, kBg);
}

std::vector<LegRow> PlanningWidget::computeLegRows() const
{
    std::vector<LegRow> rows;
    const std::vector<std::string> &route = mPlan.route();
    rows.reserve(route.size());
    for (size_t i = 0; i < route.size(); ++i)
    {
        LegRow r;
        r.ident = route[i];
        const Waypoint *wpt = NavDb::instance().find(route[i]);
        if (wpt != nullptr)
        {
            r.name = wpt->name;
            r.lat = wpt->lat;
            r.lon = wpt->lon;
            r.elevationFt = wpt->elevationFt;
            r.known = true;
        }
        rows.push_back(r);
    }
    for (size_t i = 1; i < rows.size(); ++i)
    {
        LegRow &prev = rows[i - 1];
        LegRow &cur = rows[i];
        if (!prev.known || !cur.known)
        {
            continue;
        }
        cur.legDistNm = FlightPlan::distanceNm(prev.lat, prev.lon, cur.lat, cur.lon);
        cur.trackDeg = FlightPlan::bearingDeg(prev.lat, prev.lon, cur.lat, cur.lon);
        const WindResult w = FlightPlan::windCorrection(cur.trackDeg, mPlan.tasKt(), mPlan.windDirDeg(),
                                                        mPlan.windSpdKt());
        cur.headingDeg = w.headingDeg;
        cur.groundSpeedKt = w.groundSpeedKt;
        if (w.groundSpeedKt > 0.0f && cur.legDistNm > 0.0f)
        {
            cur.eteMin = std::round((cur.legDistNm / w.groundSpeedKt) * 60.0f);
        }
        cur.computed = true;
    }
    return rows;
}

void PlanningWidget::computeTotals(float &totalNm, float &totalMin) const
{
    totalNm = 0.0f;
    totalMin = 0.0f;
    const std::vector<LegRow> rows = computeLegRows();
    for (const LegRow &r : rows)
    {
        if (r.computed)
        {
            totalNm += r.legDistNm;
            totalMin += r.eteMin;
        }
    }
}

void PlanningWidget::drawHeader()
{
    const int headerH = std::max(48, mScreenH / 14);
    Render2D &bar = nextDraw();
    bar.drawRectangle(0, mScreenH - headerH, mScreenW, headerH, 0x090D11FFu);

    // PLANNING chip. The only way back to the EFIS; the top menu is hidden here.
    const int chipW = std::max(168, mScreenW / 10);
    Render2D &chip = nextDraw();
    chip.drawRectangle(6, mScreenH - headerH + 4, chipW - 12, headerH - 8, kButtonBgHi);
    Render2D &chipTxt = nextDraw();
    chipTxt.drawTextCentered("PLANNING", 16.0f, static_cast<float>(chipW / 2),
                             static_cast<float>(mScreenH - headerH / 2), kCyan, "plan-hdr-mode");

    // Live UTC and plan summary.
    float totalNm = 0.0f;
    float totalMin = 0.0f;
    computeTotals(totalNm, totalMin);
    const float trip = std::round((totalMin / 60.0f) * mPlan.fuelBurnLph());
    const float reserve = std::round(0.75f * mPlan.fuelBurnLph());

    char distBuf[32];
    std::snprintf(distBuf, sizeof(distBuf), "%.0fNM", totalNm);
    char fuelBuf[32];
    std::snprintf(fuelBuf, sizeof(fuelBuf), "%.0fL (+RES %.0fL)", trip, reserve);

    const std::string dest = mPlan.route().empty() ? std::string("---") : mPlan.route().back();

    struct HudItem
    {
        std::string label;
        std::string value;
        uint32_t color;
    };
    HudItem hud[] = {
        {"UTC", clockUtc(), kCyan},
        {"DEST", dest, kMagenta},
        {"TOTAL", distBuf, kGreen},
        {"ETE", formatEteShort(totalMin), kAmber},
        {"FUEL", fuelBuf, kGreen},
    };

    int cursor = mScreenW - 12;
    for (int i = static_cast<int>(sizeof(hud) / sizeof(hud[0])) - 1; i >= 0; --i)
    {
        const HudItem &it = hud[i];
        const float labelPx = 12.0f;
        const float valuePx = 18.0f;
        const int valueW = std::max(60, static_cast<int>(it.value.size()) * 12);
        const int itemW = valueW + 8;
        cursor -= itemW;
        Render2D &lbl = nextDraw();
        lbl.drawTextCentered(it.label, labelPx, static_cast<float>(cursor + itemW / 2),
                             static_cast<float>(mScreenH - headerH + 12), kMuted,
                             (std::string("plan-hud-l-") + it.label).c_str());
        Render2D &val = nextDraw();
        val.drawTextCentered(it.value, valuePx, static_cast<float>(cursor + itemW / 2),
                             static_cast<float>(mScreenH - headerH + 30), it.color,
                             (std::string("plan-hud-v-") + it.label + "-" + it.value).c_str());
        cursor -= 12;
    }
}

void PlanningWidget::drawTabStrip()
{
    const int headerH = std::max(48, mScreenH / 14);
    const int tabsH = std::max(52, mScreenH / 14);
    const int tabsTop = headerH;
    const int tabsBottom = tabsTop + tabsH;
    Render2D &strip = nextDraw();
    strip.drawRectangle(0, mScreenH - tabsBottom, mScreenW, tabsH, 0x090C10FFu);

    struct TabEntry
    {
        const char *label;
        Tab tab;
    };
    TabEntry entries[] = {
        {"ROUTE", Tab::Route},
        {"SETUP", Tab::Setup},
        {"BRIEFING", Tab::Briefing},
        {"MAP", Tab::Map},
    };
    const int count = static_cast<int>(sizeof(entries) / sizeof(entries[0]));
    const int tabW = mScreenW / count;
    for (int i = 0; i < count; ++i)
    {
        const bool active = mTab == entries[i].tab;
        const int x = i * tabW;
        const int y = tabsTop;
        Render2D &bg = nextDraw();
        bg.drawRectangle(x, mScreenH - tabsBottom, tabW, tabsH, active ? 0x152030FFu : 0x000000FFu);
        // Underline for the active tab.
        if (active)
        {
            Render2D &ul = nextDraw();
            ul.drawRectangle(x + 8, mScreenH - tabsBottom, tabW - 16, 4, kCyan);
        }
        Render2D &lbl = nextDraw();
        lbl.drawTextCentered(entries[i].label, 20.0f, static_cast<float>(x + tabW / 2),
                             static_cast<float>(mScreenH - tabsTop - tabsH / 2), active ? kInk : kMuted,
                             (std::string("plan-tab-") + entries[i].label).c_str());
        Tab targetTab = entries[i].tab;
        addRect(x, y, tabW, tabsH, [this, targetTab] {
            mTab = targetTab;
            if (targetTab == Tab::Map)
            {
                // Nothing extra; the widget centres on entry when needed.
            }
        });
    }
}

void PlanningWidget::drawToast()
{
    if (mToast.empty())
    {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= mToastUntil)
    {
        mToast.clear();
        return;
    }
    const int w = std::max(280, mScreenW / 3);
    const int h = 48;
    const int x = (mScreenW - w) / 2;
    const int y = mScreenH / 8;
    Render2D &box = nextDraw();
    box.drawRectangle(x, mScreenH - y - h, w, h, 0x1F2D42EEu);
    Render2D &txt = nextDraw();
    txt.drawTextCentered(mToast, 18.0f, static_cast<float>(x + w / 2), static_cast<float>(mScreenH - y - h / 2), kInk,
                         "plan-toast");
}

void PlanningWidget::drawModal()
{
    if (!mModalOpen)
    {
        return;
    }
    // Full-screen scrim.
    Render2D &scrim = nextDraw();
    scrim.drawRectangle(0, 0, mScreenW, mScreenH, 0x000000CCu);
    addRect(0, 0, mScreenW, mScreenH, [this] { closeSearchModal(); });

    const int w = std::min(mScreenW - 40, std::max(360, mScreenW * 2 / 3));
    const int h = std::min(mScreenH - 40, std::max(320, mScreenH * 3 / 4));
    const int x = (mScreenW - w) / 2;
    const int y = (mScreenH - h) / 2;

    Render2D &card = nextDraw();
    card.drawRectangle(x, mScreenH - y - h, w, h, 0x131920FFu);
    // Border.
    Render2D &brd = nextDraw();
    brd.drawRectangle(x, mScreenH - y - h, w, 2, kBorder);
    Render2D &brd2 = nextDraw();
    brd2.drawRectangle(x, mScreenH - y - 2, w, 2, kBorder);

    Render2D &title = nextDraw();
    title.drawTextCentered("ADD WAYPOINT", 22.0f, static_cast<float>(x + w / 2), static_cast<float>(mScreenH - y - 28),
                           kInk, "plan-modal-title");

    // Query input box.
    const int inputX = x + 20;
    const int inputY = y + 60;
    const int inputW = w - 40 - 80;
    const int inputH = 44;
    Render2D &inputBox = nextDraw();
    const uint32_t inputFill = mFocus == Focus::Search ? 0x1F2D42FFu : 0x0A0F14FFu;
    inputBox.drawRectangle(inputX, mScreenH - inputY - inputH, inputW, inputH, inputFill);
    std::string shown = mSearchQuery.empty() ? std::string("SEARCH IDENT OR NAME") : mSearchQuery;
    // Caret.
    if (mFocus == Focus::Search)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - mCaretStart).count();
        if ((ms / 500) % 2 == 0)
        {
            shown.push_back('_');
        }
    }
    Render2D &inputTxt = nextDraw();
    inputTxt.drawText(shown, 20.0f, static_cast<float>(inputX + 8),
                      static_cast<float>(mScreenH - inputY - inputH + 10),
                      mSearchQuery.empty() ? kMuted : kInk, "plan-modal-input");
    addRect(inputX, inputY, inputW, inputH, [this] {
        mFocus = Focus::Search;
        updateTextInput();
    });

    // Close button.
    drawButton("plan-modal-close", "CLOSE", x + w - 20 - 76, inputY, 76, inputH, kButtonBg, kInk, 16.0f,
               [this] { closeSearchModal(); });

    // Results list.
    const int listX = x + 20;
    const int listY = inputY + inputH + 10;
    const int listW = w - 40;
    const int rowH = 40;
    for (size_t i = 0; i < mSearchResults.size(); ++i)
    {
        const int ry = listY + static_cast<int>(i) * (rowH + 4);
        if (ry + rowH > y + h - 10)
        {
            break;
        }
        Render2D &row = nextDraw();
        row.drawRectangle(listX, mScreenH - ry - rowH, listW, rowH, 0x1F2D42AAu);
        Render2D &lbl = nextDraw();
        lbl.drawText(mSearchResults[i], 18.0f, static_cast<float>(listX + 10),
                     static_cast<float>(mScreenH - ry - rowH + 8), kInk,
                     (std::string("plan-modal-row-") + std::to_string(i)).c_str());
        // Extract ident (first token).
        std::string ident;
        for (char c : mSearchResults[i])
        {
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                break;
            }
            ident.push_back(c);
        }
        addRect(listX, ry, listW, rowH, [this, ident] {
            mPlan.insertBeforeDest(ident);
            mRouteBuffer = routeToString(mPlan.route());
            closeSearchModal();
            showToast(std::string("ADDED ") + ident);
        });
    }
}

void PlanningWidget::drawRouteTab()
{
    const int headerH = std::max(48, mScreenH / 14);
    const int tabsH = std::max(52, mScreenH / 14);
    const int top = headerH + tabsH + 12;
    const int pad = 16;
    const int contentW = mScreenW - pad * 2;

    // Route bar
    const int barY = top;
    const int barH = 84;
    Render2D &bar = nextDraw();
    bar.drawRectangle(pad, mScreenH - barY - barH, contentW, barH, kCard);

    // DEP -> ARR flow with big idents.
    const std::string dep = mPlan.route().empty() ? std::string("---") : mPlan.route().front();
    const std::string arr = mPlan.route().empty() ? std::string("---") : mPlan.route().back();
    Render2D &depTxt = nextDraw();
    depTxt.drawTextCentered(dep, 30.0f, static_cast<float>(pad + 90),
                            static_cast<float>(mScreenH - barY - barH / 2 + 4), kInk, "plan-route-dep");
    Render2D &depLbl = nextDraw();
    depLbl.drawTextCentered("DEP", 12.0f, static_cast<float>(pad + 90),
                            static_cast<float>(mScreenH - barY - barH / 2 - 20), kMuted, "plan-route-dep-l");
    Render2D &arrow = nextDraw();
    arrow.drawTextCentered("->", 24.0f, static_cast<float>(pad + 200),
                           static_cast<float>(mScreenH - barY - barH / 2), kMagenta, "plan-route-arrow");
    Render2D &arrTxt = nextDraw();
    arrTxt.drawTextCentered(arr, 30.0f, static_cast<float>(pad + 300),
                            static_cast<float>(mScreenH - barY - barH / 2 + 4), kMagenta, "plan-route-arr");
    Render2D &arrLbl = nextDraw();
    arrLbl.drawTextCentered("ARR", 12.0f, static_cast<float>(pad + 300),
                            static_cast<float>(mScreenH - barY - barH / 2 - 20), kMuted, "plan-route-arr-l");

    // Route string input.
    const int inputX = pad + 400;
    const int inputY = barY + 16;
    const int inputW = contentW - 400 - 200;
    const int inputH = 44;
    Render2D &inputBg = nextDraw();
    const uint32_t fill = (mFocus == Focus::RouteString) ? 0x1F2D42FFu : 0x0A0F14FFu;
    inputBg.drawRectangle(inputX, mScreenH - inputY - inputH, inputW, inputH, fill);
    std::string shown = mRouteBuffer;
    if (mFocus == Focus::RouteString)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - mCaretStart).count();
        if ((ms / 500) % 2 == 0)
        {
            shown.push_back('_');
        }
    }
    Render2D &inputTxt = nextDraw();
    inputTxt.drawText(shown.empty() ? std::string("TAP TO EDIT ROUTE") : shown, 18.0f,
                      static_cast<float>(inputX + 8), static_cast<float>(mScreenH - inputY - inputH + 10),
                      shown.empty() ? kMuted : kInk, "plan-route-input");
    addRect(inputX, inputY, inputW, inputH, [this] {
        mFocus = Focus::RouteString;
        updateTextInput();
    });

    drawButton("plan-route-apply", "APPLY", inputX + inputW + 10, inputY, 88, inputH, kButtonBgHi, kInk, 16.0f,
               [this] { applyRouteString(); });
    drawButton("plan-route-rev", "REV", inputX + inputW + 10 + 96, inputY, 76, inputH, kButtonBg, kInk, 16.0f,
               [this] {
                   std::vector<std::string> r = mPlan.route();
                   std::reverse(r.begin(), r.end());
                   mPlan.setRoute(r);
                   mPlan.setActiveLegIndex(1);
                   mRouteBuffer = routeToString(mPlan.route());
                   showToast("ROUTE REVERSED");
               });

    // Quick presets from the POC.
    const int presetY = barY + barH + 8;
    const int presetH = 40;
    struct Preset
    {
        const char *label;
        std::vector<std::string> route;
    };
    const Preset presets[] = {
        {"EPWR -> EPKK", {"EPWR", "KUKUS", "DODUS", "EPKK"}},
        {"LKPR -> EPWR", {"LKPR", "VELIK", "OKL", "EPWR"}},
        {"KEYW -> KMIA", {"KEYW", "MTH", "DHP", "KMIA"}},
        {"KBLI -> KSEA", {"KBLI", "PAE", "KSEA"}},
    };
    int px = pad;
    const int presetCount = static_cast<int>(sizeof(presets) / sizeof(presets[0]));
    const int presetW = (contentW - (presetCount - 1) * 8) / presetCount;
    for (int i = 0; i < presetCount; ++i)
    {
        const Preset &p = presets[i];
        drawButton(std::string("plan-preset-") + p.label, p.label, px, presetY, presetW, presetH, kButtonBg, kInk, 13.0f,
                   [this, route = p.route] { loadPreset(route); });
        px += presetW + 8;
    }

    // Legs table.
    const int tableY = presetY + presetH + 8;
    const int tableH = mScreenH - tableY - 96 - 24;
    Render2D &tblBg = nextDraw();
    tblBg.drawRectangle(pad, mScreenH - tableY - tableH, contentW, tableH, kCardSubtle);

    struct Col
    {
        const char *label;
        float widthFrac;
    };
    Col cols[] = {
        {"WAYPOINT", 0.26f},
        {"TRK", 0.10f},
        {"HDG", 0.10f},
        {"ALT", 0.12f},
        {"DIST", 0.12f},
        {"GS/ETE", 0.14f},
        {"ACT", 0.16f},
    };
    const int nCols = static_cast<int>(sizeof(cols) / sizeof(cols[0]));
    int colX[8];
    int colW[8];
    int cx = pad + 10;
    for (int i = 0; i < nCols; ++i)
    {
        colX[i] = cx;
        colW[i] = static_cast<int>((contentW - 20) * cols[i].widthFrac);
        cx += colW[i];
    }
    const int headerRow = tableY + 4;
    for (int i = 0; i < nCols; ++i)
    {
        Render2D &lbl = nextDraw();
        lbl.drawTextCentered(cols[i].label, 12.0f, static_cast<float>(colX[i] + colW[i] / 2),
                             static_cast<float>(mScreenH - headerRow - 12), kMuted,
                             (std::string("plan-tab-col-") + cols[i].label).c_str());
    }

    const std::vector<LegRow> rows = computeLegRows();
    const int rowH = 42;
    int rowY = tableY + 28;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rowY + rowH > tableY + tableH - 4)
        {
            break;
        }
        const bool active = static_cast<int>(i) == mPlan.activeLegIndex();
        const uint32_t rowFill = active ? 0x2C1B2CFFu : (i % 2 == 0 ? 0x0B1219FFu : 0x101821FFu);
        Render2D &rowBg = nextDraw();
        rowBg.drawRectangle(colX[0] - 6, mScreenH - rowY - rowH, contentW - 8, rowH, rowFill);
        if (active)
        {
            Render2D &bar2 = nextDraw();
            bar2.drawRectangle(colX[0] - 6, mScreenH - rowY - rowH, 4, rowH, kMagenta);
        }
        const LegRow &r = rows[i];
        std::string ident = r.ident;
        if (active)
        {
            ident += " [ACT]";
        }
        std::string name = r.known ? r.name : std::string("(unknown ident)");
        if (name.size() > 22)
        {
            name.resize(22);
        }
        Render2D &identText = nextDraw();
        identText.drawText(ident, 18.0f, static_cast<float>(colX[0]),
                           static_cast<float>(mScreenH - rowY - rowH + 20), r.known ? kInk : kAmber,
                           (std::string("plan-leg-id-") + std::to_string(i) + "-" + r.ident).c_str());
        Render2D &nameText = nextDraw();
        nameText.drawText(name, 11.0f, static_cast<float>(colX[0]),
                          static_cast<float>(mScreenH - rowY - rowH + 4), kMuted,
                          (std::string("plan-leg-nm-") + std::to_string(i) + "-" + r.ident).c_str());

        char buf[32];
        auto cell = [&](int col, const std::string &text, uint32_t color) {
            Render2D &t = nextDraw();
            t.drawTextCentered(text, 16.0f, static_cast<float>(colX[col] + colW[col] / 2),
                               static_cast<float>(mScreenH - rowY - rowH + 12), color,
                               (std::string("plan-leg-c") + std::to_string(col) + "-" + std::to_string(i) + "-" +
                                text)
                                   .c_str());
        };
        if (r.computed)
        {
            std::snprintf(buf, sizeof(buf), "%03d", static_cast<int>(std::lround(r.trackDeg)));
            cell(1, buf, kInk);
            std::snprintf(buf, sizeof(buf), "%03d", static_cast<int>(std::lround(r.headingDeg)));
            cell(2, buf, kCyan);
        }
        else
        {
            cell(1, "---", kMuted);
            cell(2, "---", kMuted);
        }
        int alt = 0;
        if (r.elevationFt > 0.0f)
        {
            alt = static_cast<int>(std::lround(r.elevationFt));
        }
        else
        {
            alt = static_cast<int>(std::lround(mPlan.altitudeFt()));
        }
        std::snprintf(buf, sizeof(buf), "%dFT", alt);
        cell(3, buf, kInk);
        if (r.computed)
        {
            std::snprintf(buf, sizeof(buf), "%.0fNM", r.legDistNm);
            cell(4, buf, kGreen);
            char gs[32];
            std::snprintf(gs, sizeof(gs), "%.0f/%s", r.groundSpeedKt, formatMinutes(r.eteMin).c_str());
            cell(5, gs, kGreen);
        }
        else
        {
            cell(4, i == 0 ? "0NM" : "---", i == 0 ? kMuted : kMuted);
            cell(5, i == 0 ? "DEP" : "---", kMuted);
        }

        // Action buttons in the last column.
        const int actX = colX[6];
        const int btnW = (colW[6] - 8) / 4;
        const int btnH = rowH - 8;
        const int btnY = rowY + 4;
        drawButton(std::string("plan-leg-dto-") + std::to_string(i), "-D->", actX, btnY, btnW, btnH, kButtonBgMag,
                   kInk, 12.0f, [this, i] {
                       mPlan.setActiveLegIndex(static_cast<int>(i));
                       showToast(std::string("DIRECT ") + mPlan.route()[i]);
                   });
        drawButton(std::string("plan-leg-up-") + std::to_string(i), "UP", actX + btnW + 2, btnY, btnW, btnH,
                   kButtonBg, kInk, 12.0f, [this, i] {
                       mPlan.moveWaypoint(static_cast<int>(i), -1);
                       mRouteBuffer = routeToString(mPlan.route());
                   });
        drawButton(std::string("plan-leg-dn-") + std::to_string(i), "DN", actX + (btnW + 2) * 2, btnY, btnW, btnH,
                   kButtonBg, kInk, 12.0f, [this, i] {
                       mPlan.moveWaypoint(static_cast<int>(i), 1);
                       mRouteBuffer = routeToString(mPlan.route());
                   });
        drawButton(std::string("plan-leg-x-") + std::to_string(i), "X", actX + (btnW + 2) * 3, btnY, btnW, btnH,
                   0x5A1A1AFFu, kInk, 14.0f, [this, i] {
                       if (mPlan.route().size() <= 2)
                       {
                           showToast("MIN 2 WAYPOINTS");
                           return;
                       }
                       mPlan.deleteWaypoint(static_cast<int>(i));
                       mRouteBuffer = routeToString(mPlan.route());
                   });

        rowY += rowH + 4;
    }

    // Bottom dock.
    const int dockH = 76;
    const int dockY = mScreenH - dockH - 12;
    Render2D &dockBg = nextDraw();
    dockBg.drawRectangle(pad, mScreenH - dockY - dockH, contentW, dockH, kCard);
    const int dockBtnH = 52;
    const int dockBtnY = dockY + 12;
    int bx = pad + 16;
    drawButton("plan-dock-add", "+ ADD WPT", bx, dockBtnY, 160, dockBtnH, kButtonBgHi, kInk, 18.0f,
               [this] { openSearchModal(); });
    bx += 172;
    drawButton("plan-dock-dto", "DIRECT-TO", bx, dockBtnY, 160, dockBtnH, kButtonBgMag, kInk, 18.0f,
               [this] { showToast("SELECT WAYPOINT ROW"); });
    bx += 172;
    drawButton("plan-dock-act", "ACTIVATE", bx, dockBtnY, 180, dockBtnH, kButtonBgGrn, kInk, 18.0f, [this] {
        if (mPlan.armActiveLeg())
        {
            showToast(std::string("ARMED ") + mPlan.armedFromIdent() + " -> " + mPlan.armedToIdent());
        }
        else
        {
            showToast("CANNOT ARM (unknown ident)");
        }
    });
    bx += 192;
    drawButton("plan-dock-clr", "CLEAR", bx, dockBtnY, 130, dockBtnH, kButtonBg, kInk, 18.0f, [this] {
        mPlan.clearRoute();
        mRouteBuffer = routeToString(mPlan.route());
        showToast("PLAN CLEARED");
    });
}

void PlanningWidget::drawSetupTab()
{
    const int headerH = std::max(48, mScreenH / 14);
    const int tabsH = std::max(52, mScreenH / 14);
    const int top = headerH + tabsH + 12;
    const int pad = 16;
    const int contentW = mScreenW - pad * 2;
    const int contentH = mScreenH - top - 12;

    const int cardW = (contentW - 12) / 2;
    const int cardH = (contentH - 12) / 2;

    struct Card
    {
        int x;
        int y;
        int w;
        int h;
    };
    Card cards[4];
    cards[0] = {pad, top, cardW, cardH};
    cards[1] = {pad + cardW + 12, top, cardW, cardH};
    cards[2] = {pad, top + cardH + 12, cardW, cardH};
    cards[3] = {pad + cardW + 12, top + cardH + 12, cardW, cardH};
    for (int i = 0; i < 4; ++i)
    {
        Render2D &bg = nextDraw();
        bg.drawRectangle(cards[i].x, mScreenH - cards[i].y - cards[i].h, cards[i].w, cards[i].h, kCard);
    }

    auto label = [&](const Card &c, const std::string &text) {
        Render2D &lbl = nextDraw();
        lbl.drawText(text, 14.0f, static_cast<float>(c.x + 14), static_cast<float>(mScreenH - c.y - 26), kMuted,
                     ("plan-setup-lbl-" + text).c_str());
    };

    // Card 1: Aircraft / cruise
    label(cards[0], "AIRCRAFT & CRUISE");
    // Aircraft profile buttons
    const int acBtnY = cards[0].y + 40;
    const char *acs[] = {"C172", "PA28", "VL3", "RV7"};
    for (int i = 0; i < 4; ++i)
    {
        const int bw = 88;
        const int bh = 44;
        const int bx = cards[0].x + 14 + i * (bw + 8);
        const bool active = mPlan.aircraft() == acs[i];
        drawButton(std::string("plan-setup-ac-") + acs[i], acs[i], bx, acBtnY, bw, bh,
                   active ? kButtonBgHi : kButtonBg, active ? kCyan : kInk, 16.0f,
                   [this, ac = std::string(acs[i])] { mPlan.setAircraft(ac); });
    }

    // Steppers helper.
    auto stepper = [&](const std::string &lbl, int gridRow, const Card &c, float value, const char *unit,
                       std::function<void(int)> step) {
        const int rowY = c.y + 100 + gridRow * 60;
        Render2D &l = nextDraw();
        l.drawText(lbl, 14.0f, static_cast<float>(c.x + 14), static_cast<float>(mScreenH - rowY - 12), kMuted,
                   ("plan-step-l-" + lbl).c_str());
        char valBuf[32];
        std::snprintf(valBuf, sizeof(valBuf), "%.0f %s", value, unit);
        Render2D &v = nextDraw();
        v.drawText(valBuf, 20.0f, static_cast<float>(c.x + 14 + 110), static_cast<float>(mScreenH - rowY - 14),
                   kInk, ("plan-step-v-" + lbl).c_str());
        const int minusX = c.x + c.w - 100 - 8;
        const int plusX = c.x + c.w - 44 - 8;
        drawButton("plan-step-m-" + lbl, "-", minusX, rowY - 6, 44, 40, kButtonBg, kInk, 22.0f,
                   [step] { step(-1); });
        drawButton("plan-step-p-" + lbl, "+", plusX, rowY - 6, 44, 40, kButtonBg, kInk, 22.0f,
                   [step] { step(+1); });
    };

    stepper("TAS", 0, cards[0], mPlan.tasKt(), "KT", [this](int d) { mPlan.setTasKt(mPlan.tasKt() + d * 5.0f); });
    stepper("ALT", 1, cards[0], mPlan.altitudeFt(), "FT",
            [this](int d) { mPlan.setAltitudeFt(mPlan.altitudeFt() + d * 500.0f); });
    stepper("BURN", 2, cards[0], mPlan.fuelBurnLph(), "L/H",
            [this](int d) { mPlan.setFuelBurnLph(mPlan.fuelBurnLph() + d * 1.0f); });

    // Card 2: Winds
    label(cards[1], "WINDS");
    stepper("WIND DIR", 0, cards[1], mPlan.windDirDeg(), "DEG",
            [this](int d) { mPlan.setWindDirDeg(mPlan.windDirDeg() + d * 10.0f); });
    stepper("WIND SPD", 1, cards[1], mPlan.windSpdKt(), "KT",
            [this](int d) { mPlan.setWindSpdKt(mPlan.windSpdKt() + d * 2.0f); });

    // Compact wind-from dial: N/E/S/W around a centre mark.
    const int dialCx = cards[1].x + cards[1].w - 70;
    const int dialCy = cards[1].y + cards[1].h - 70;
    const int dialGlX = dialCx;
    const int dialGlY = mScreenH - dialCy;
    Render2D &dialBox = nextDraw();
    dialBox.drawRectangle(dialGlX - 36, dialGlY - 36, 72, 72, 0x0A0F14FFu);
    auto dialLbl = [&](const char *t, int dx, int dy) {
        Render2D &l = nextDraw();
        l.drawTextCentered(t, 12.0f, static_cast<float>(dialGlX + dx), static_cast<float>(dialGlY + dy), kMuted,
                           (std::string("plan-dial-") + t).c_str());
    };
    dialLbl("N", 0, 24);
    dialLbl("S", 0, -24);
    dialLbl("E", 24, 0);
    dialLbl("W", -24, 0);
    const float windRad = mPlan.windDirDeg() * static_cast<float>(3.14159265358979323846 / 180.0);
    const int nx = static_cast<int>(std::lround(std::sin(windRad) * 22.0f));
    const int ny = static_cast<int>(std::lround(std::cos(windRad) * 22.0f));
    Render2D &fromDot = nextDraw();
    fromDot.drawRectangle(dialGlX + nx - 4, dialGlY + ny - 4, 8, 8, kMagenta);
    Render2D &hub = nextDraw();
    hub.drawRectangle(dialGlX - 3, dialGlY - 3, 6, 6, kCyan);

    // Live crosswind / headwind for the active leg.
    const std::vector<LegRow> rows = computeLegRows();
    const int active = std::clamp(mPlan.activeLegIndex(), 0, static_cast<int>(rows.size()) - 1);
    std::string xw = "--";
    std::string hw = "--";
    if (active >= 0 && active < static_cast<int>(rows.size()) && rows[active].computed)
    {
        const WindResult w = FlightPlan::windCorrection(rows[active].trackDeg, mPlan.tasKt(), mPlan.windDirDeg(),
                                                        mPlan.windSpdKt());
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%s%.0f KT", w.crosswindKt >= 0 ? "R " : "L ", std::fabs(w.crosswindKt));
        xw = buf;
        std::snprintf(buf, sizeof(buf), "%s%.0f KT", w.headwindKt >= 0 ? "HW " : "TW ", std::fabs(w.headwindKt));
        hw = buf;
    }
    Render2D &xwLbl = nextDraw();
    xwLbl.drawText("CROSSWIND", 12.0f, static_cast<float>(cards[1].x + 14),
                   static_cast<float>(mScreenH - cards[1].y - cards[1].h + 60), kMuted, "plan-wind-xw-l");
    Render2D &xwVal = nextDraw();
    xwVal.drawText(xw, 20.0f, static_cast<float>(cards[1].x + 14 + 130),
                   static_cast<float>(mScreenH - cards[1].y - cards[1].h + 58), kCyan, ("plan-wind-xw-v-" + xw).c_str());
    Render2D &hwLbl = nextDraw();
    hwLbl.drawText("H/T WIND", 12.0f, static_cast<float>(cards[1].x + 14),
                   static_cast<float>(mScreenH - cards[1].y - cards[1].h + 34), kMuted, "plan-wind-hw-l");
    Render2D &hwVal = nextDraw();
    hwVal.drawText(hw, 20.0f, static_cast<float>(cards[1].x + 14 + 130),
                   static_cast<float>(mScreenH - cards[1].y - cards[1].h + 32), kGreen,
                   ("plan-wind-hw-v-" + hw).c_str());

    // Card 3: Fuel / endurance
    label(cards[2], "FUEL & ENDURANCE");
    stepper("ONBOARD", 0, cards[2], mPlan.fuelOnBoardL(), "L",
            [this](int d) { mPlan.setFuelOnBoardL(mPlan.fuelOnBoardL() + d * 10.0f); });
    stepper("POB", 1, cards[2], static_cast<float>(mPlan.pob()), "",
            [this](int d) { mPlan.setPob(mPlan.pob() + d); });

    float totalNm = 0.0f;
    float totalMin = 0.0f;
    computeTotals(totalNm, totalMin);
    const float trip = std::round((totalMin / 60.0f) * mPlan.fuelBurnLph());
    const float reserve = std::round(0.75f * mPlan.fuelBurnLph());
    const float remaining = mPlan.fuelOnBoardL() - trip;
    // Fuel bar
    const int barX = cards[2].x + 14;
    const int barY = cards[2].y + cards[2].h - 60;
    const int barW = cards[2].w - 28;
    const int barH = 16;
    Render2D &barBg = nextDraw();
    barBg.drawRectangle(barX, mScreenH - barY - barH, barW, barH, 0x0A0F14FFu);
    const float ratio = std::min(1.0f, std::max(0.0f, mPlan.fuelOnBoardL() / 140.0f));
    const uint32_t barCol = remaining < reserve ? kRed : kGreen;
    Render2D &barFill = nextDraw();
    barFill.drawRectangle(barX, mScreenH - barY - barH, static_cast<int>(barW * ratio), barH, barCol);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "TRIP %.0fL   RESERVE 45MIN %.0fL   REMAIN %.0fL", trip, reserve, remaining);
    Render2D &sum = nextDraw();
    sum.drawText(buf, 14.0f, static_cast<float>(barX), static_cast<float>(mScreenH - barY - barH - 20),
                 remaining < reserve ? kRed : kInk, ("plan-fuel-sum-" + std::string(buf)).c_str());

    // Card 4: Sync stubs
    label(cards[3], "SYNC");
    const int syncY = cards[3].y + 60;
    drawButton("plan-sync-simbrief", "FETCH SIMBRIEF", cards[3].x + 14, syncY, 220, 48, kButtonBgHi, kInk, 16.0f,
               [this] {
                   mSimBriefPending = true;
                   mSimBriefAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(700);
                   showToast("FETCHING SIMBRIEF...");
               });
    drawButton("plan-sync-metar", "LIVE METAR", cards[3].x + 14, syncY + 60, 220, 48, kButtonBg, kInk, 16.0f,
               [this] { showToast("METAR: NOT AVAILABLE (STUB)"); });
    drawButton("plan-sync-open", "OPEN .FPL", cards[3].x + 14, syncY + 120, 220, 48, kButtonBg, kInk, 16.0f,
               [this] { showToast("IMPORT: NOT AVAILABLE (STUB)"); });
    drawButton("plan-sync-export", "EXPORT", cards[3].x + 14, syncY + 180, 220, 48, kButtonBg, kInk, 16.0f,
               [this] { showToast("EXPORT: NOT AVAILABLE (STUB)"); });
}

void PlanningWidget::drawBriefingTab()
{
    const int headerH = std::max(48, mScreenH / 14);
    const int tabsH = std::max(52, mScreenH / 14);
    const int top = headerH + tabsH + 12;
    const int pad = 16;
    const int contentW = mScreenW - pad * 2;

    const int cardH = 240;
    const int cardW = (contentW - 12) / 2;

    const std::string dep = mPlan.route().empty() ? std::string("---") : mPlan.route().front();
    const std::string arr = mPlan.route().empty() ? std::string("---") : mPlan.route().back();

    struct Card
    {
        std::string title;
        std::string metar;
        std::string decoded;
        std::string badge;
        uint32_t badgeColor;
    };

    Card cards[2];
    cards[0].title = std::string("DEP ") + dep;
    cards[0].metar = dep + " 232030Z 24008KT 9999 FEW035 22/12 Q1015";
    cards[0].decoded = "WIND 240/08KT   VIS 10KM   FEW 3500FT   22C/12C   1015 hPa";
    cards[0].badge = "VFR";
    cards[0].badgeColor = kGreen;
    cards[1].title = std::string("ARR ") + arr;
    cards[1].metar = arr + " 232030Z 23010KT 9999 SCT045 21/11 Q1014";
    cards[1].decoded = "WIND 230/10KT   VIS 10KM   SCATTERED 4500FT   21C/11C   1014 hPa";
    cards[1].badge = "VFR";
    cards[1].badgeColor = kGreen;

    for (int i = 0; i < 2; ++i)
    {
        const int cx = pad + i * (cardW + 12);
        Render2D &bg = nextDraw();
        bg.drawRectangle(cx, mScreenH - top - cardH, cardW, cardH, kCard);
        Render2D &title = nextDraw();
        title.drawText(cards[i].title, 20.0f, static_cast<float>(cx + 16),
                       static_cast<float>(mScreenH - top - 32), kInk, ("plan-brief-t-" + cards[i].title).c_str());
        // Badge
        Render2D &badgeBg = nextDraw();
        badgeBg.drawRectangle(cx + cardW - 96, mScreenH - top - 36, 80, 26, 0x1A2A1FFFu);
        Render2D &badgeTxt = nextDraw();
        badgeTxt.drawTextCentered(cards[i].badge, 16.0f, static_cast<float>(cx + cardW - 96 + 40),
                                  static_cast<float>(mScreenH - top - 24), cards[i].badgeColor,
                                  ("plan-brief-b-" + cards[i].title).c_str());
        Render2D &metar = nextDraw();
        metar.drawText(cards[i].metar, 16.0f, static_cast<float>(cx + 16),
                       static_cast<float>(mScreenH - top - 80), kCyan,
                       ("plan-brief-m-" + cards[i].title).c_str());
        Render2D &dec = nextDraw();
        dec.drawText(cards[i].decoded, 14.0f, static_cast<float>(cx + 16),
                     static_cast<float>(mScreenH - top - 120), kMuted,
                     ("plan-brief-d-" + cards[i].title).c_str());
    }

    // Refresh button
    drawButton("plan-brief-refresh", "REFRESH METAR", pad, top + cardH + 20, 220, 52, kButtonBgHi, kInk, 18.0f,
               [this] { showToast("METAR REFRESH (STUB)"); });
}

void PlanningWidget::drawMapTab()
{
    const int headerH = std::max(48, mScreenH / 14);
    const int tabsH = std::max(52, mScreenH / 14);
    const int top = headerH + tabsH + 12;
    const int pad = 16;
    const int contentW = mScreenW - pad * 2;
    const int controlsH = 68;
    const int dockH = 72;
    const int mapH = mScreenH - top - controlsH - dockH - 24;

    // Controls row
    const int controlsY = top;
    Render2D &cbg = nextDraw();
    cbg.drawRectangle(pad, mScreenH - controlsY - controlsH, contentW, controlsH, kCard);
    int cx = pad + 12;
    drawButton("plan-map-edit", "EDIT MODE", cx, controlsY + 10, 148, 48,
               mEditMode ? kButtonBgHi : kButtonBg, mEditMode ? kCyan : kInk, 15.0f, [this] {
                   mEditMode = !mEditMode;
                   if (!mEditMode)
                   {
                       cancelRouteDrag();
                       showToast("EDIT MODE OFF");
                   }
                   else
                   {
                       showToast("EDIT MODE: DRAG POINTS TO SNAP");
                   }
               });
    cx += 156;
    drawButton("plan-map-fit", "FIT", cx, controlsY + 10, 80, 48, kButtonBg, kInk, 16.0f, [this] { mMap.fitRoute(); });
    cx += 88;
    drawButton("plan-map-as", mMap.airspaceVisible() ? "AS ON" : "AS OFF", cx, controlsY + 10, 88, 48,
               mMap.airspaceVisible() ? kButtonBgHi : kButtonBg, kInk, 16.0f,
               [this] { mMap.setAirspaceVisible(!mMap.airspaceVisible()); });
    cx += 96;
    drawButton("plan-map-fis", mMap.fisVisible() ? "FIS ON" : "FIS OFF", cx, controlsY + 10, 88, 48,
               mMap.fisVisible() ? kButtonBgHi : kButtonBg, mMap.fisVisible() ? kAmber : kInk, 16.0f, [this] {
                   mMap.setFisVisible(!mMap.fisVisible());
                   showToast(mMap.fisVisible() ? "FIS SECTORS ON" : "FIS SECTORS OFF");
               });
    cx += 96;
    drawButton("plan-map-ifr", mMap.ifrVisible() ? "IFR ON" : "IFR OFF", cx, controlsY + 10, 88, 48,
               mMap.ifrVisible() ? kButtonBgHi : kButtonBg, kInk, 16.0f, [this] {
                   mMap.setIfrVisible(!mMap.ifrVisible());
                   showToast(mMap.ifrVisible() ? "IFR POINTS ON" : "IFR POINTS OFF");
               });
    cx += 96;
    drawButton("plan-map-bg", mMap.backgroundLabel(), cx, controlsY + 10, 110, 48, kButtonBg, kInk, 16.0f, [this] {
        mMap.toggleBackground();
        showToast(std::string("BASEMAP: ") + mMap.backgroundLabel());
    });
    cx += 118;

    // Filters
    struct FilterOpt
    {
        const char *label;
        AirspaceFilter val;
    };
    FilterOpt opts[] = {
        {"ALL", AirspaceFilter::All},   {"CTR/TMA", AirspaceFilter::CtrTma}, {"R/P/D", AirspaceFilter::Rpd},
        {"TRA", AirspaceFilter::Tra},   {"ATZ", AirspaceFilter::Atz},
    };
    for (const FilterOpt &opt : opts)
    {
        const bool active = mMap.filter() == opt.val;
        drawButton(std::string("plan-map-f-") + opt.label, opt.label, cx, controlsY + 10, 88, 48,
                   active ? kButtonBgHi : kButtonBg, active ? kCyan : kInk, 14.0f,
                   [this, val = opt.val, name = std::string(opt.label)] {
                       mMap.setFilter(val);
                       showToast(std::string("FILTER: ") + name);
                   });
        cx += 92;
    }

    // Map viewport
    const int mapY = controlsY + controlsH + 8;
    const int mapHeight = std::max(1, mapH);
    mMap.place(pad, mapY, contentW, mapHeight);
    Render2D &well = nextDraw();
    well.drawRectangle(pad, mScreenH - mapY - mapHeight, contentW, mapHeight, 0x101820FFu);
    addRect(pad, mapY, contentW, mapHeight, [] {});

    // Dock
    const int dockY = mapY + std::max(1, mapH) + 8;
    Render2D &dbg = nextDraw();
    dbg.drawRectangle(pad, mScreenH - dockY - dockH, contentW, dockH, kCard);
    int bx = pad + 12;
    drawButton("plan-mapdock-dto", "DIRECT-TO", bx, dockY + 12, 160, dockH - 24, kButtonBgMag, kInk, 18.0f,
               [this] { showToast("SELECT ROW ON ROUTE TAB"); });
    bx += 172;
    drawButton("plan-mapdock-send", "SEND TO AVIONICS", bx, dockY + 12, 220, dockH - 24, kButtonBgGrn, kInk, 18.0f,
               [this] {
                   if (mPlan.armActiveLeg())
                   {
                       showToast(std::string("ARMED ") + mPlan.armedFromIdent() + " -> " + mPlan.armedToIdent());
                   }
                   else
                   {
                       showToast("CANNOT ARM (unknown ident)");
                   }
               });
    bx += 232;
    drawButton("plan-mapdock-aip", "RELOAD AIP", bx, dockY + 12, 160, dockH - 24, kButtonBg, kInk, 16.0f, [this] {
        NavDb::instance().reloadAirspaces();
        mMap.invalidate();
        showToast("AIP RELOADED");
    });
    bx += 172;
    drawButton("plan-mapdock-back", "BACK TO ROUTE", bx, dockY + 12, 180, dockH - 24, kButtonBg, kInk, 16.0f,
               [this] { mTab = Tab::Route; });
}

void PlanningWidget::drawMapOverlays()
{
    const int headerH = std::max(48, mScreenH / 14);
    const int tabsH = std::max(52, mScreenH / 14);
    const int pad = 16;
    const int mapY = headerH + tabsH + 12 + 68 + 8;
    float totalNm = 0.0f;
    float totalMin = 0.0f;
    computeTotals(totalNm, totalMin);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s  %.0fNM  %s  %.0fFT", routeToString(mPlan.route()).c_str(), totalNm,
                  formatEteShort(totalMin).c_str(), mPlan.altitudeFt());
    const size_t start = mDrawCursor;
    Render2D &ovl = nextDraw();
    ovl.drawText(buf, 14.0f, static_cast<float>(pad + 12), static_cast<float>(mScreenH - mapY - 24), kInk,
                 ("plan-map-ovl-" + std::string(buf)).c_str());
    char coord[64];
    std::snprintf(coord, sizeof(coord), "LAT %.3f  LON %.3f  Z%.1f", mMap.centerLat(), mMap.centerLon(),
                  static_cast<double>(mMap.zoomLevel()));
    Render2D &co = nextDraw();
    co.drawText(coord, 12.0f, static_cast<float>(pad + 12),
                static_cast<float>(mScreenH - mapY - mMap.h() + 12), kMuted,
                ("plan-map-coord-" + std::string(coord)).c_str());

    if (mEditMode)
    {
        const char *hint = mDragRouteIndex >= 0
                               ? (mSnapValid ? mSnapIdent.c_str() : "DRAG TO A MARKED POINT")
                               : "EDIT MODE: DRAG ROUTE POINTS";
        Render2D &hintTxt = nextDraw();
        hintTxt.drawText(hint, 14.0f, static_cast<float>(pad + 12),
                         static_cast<float>(mScreenH - mapY - mMap.h() + 32),
                         mDragRouteIndex >= 0 && mSnapValid ? kCyan : kAmber, "plan-map-edit-hint");
    }

    auto markerAt = [this](int sx, int sy, int size, uint32_t color) {
        const int glY = mScreenH - sy;
        Render2D &mk = nextDraw();
        mk.drawRectangle(sx - size / 2, glY - size / 2, size, size, color);
    };
    auto lineTo = [this](int x0, int y0, int x1, int y1, uint32_t color) {
        const float gx0 = static_cast<float>(x0);
        const float gy0 = static_cast<float>(mScreenH - y0);
        const float gx1 = static_cast<float>(x1);
        const float gy1 = static_cast<float>(mScreenH - y1);
        const float dx = gx1 - gx0;
        const float dy = gy1 - gy0;
        const float len = std::hypot(dx, dy);
        if (len < 1.0f)
        {
            return;
        }
        const int steps = std::max(1, static_cast<int>(len / 6.0f));
        for (int i = 0; i <= steps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            const int px = static_cast<int>(std::lround(gx0 + dx * t));
            const int py = static_cast<int>(std::lround(gy0 + dy * t));
            Render2D &seg = nextDraw();
            seg.drawRectangle(px - 2, py - 2, 5, 5, color);
        }
    };

    if (mDragRouteIndex >= 0 && mSnapValid)
    {
        int snapX = 0;
        int snapY = 0;
        mMap.latLonToScreen(mSnapLat, mSnapLon, snapX, snapY);
        markerAt(snapX, snapY, 28, kCyan);
        markerAt(snapX, snapY, 16, 0x0A0D10FFu);
        markerAt(snapX, snapY, 8, kCyan);
        Render2D &snapLbl = nextDraw();
        snapLbl.drawTextCentered(mSnapIdent, 16.0f, static_cast<float>(snapX),
                                 static_cast<float>(mScreenH - snapY + 22), kCyan,
                                 ("plan-map-snap-" + mSnapIdent).c_str());

        const std::vector<std::string> &route = mPlan.route();
        auto projectIdent = [this](const std::string &ident, int &ox, int &oy) -> bool {
            const Waypoint *wpt = NavDb::instance().find(ident);
            if (wpt == nullptr)
            {
                return false;
            }
            mMap.latLonToScreen(wpt->lat, wpt->lon, ox, oy);
            return true;
        };
        if (mDragRouteIndex > 0)
        {
            int px = 0;
            int py = 0;
            if (projectIdent(route[static_cast<size_t>(mDragRouteIndex - 1)], px, py))
            {
                lineTo(px, py, snapX, snapY, kMagenta);
            }
        }
        if (mDragRouteIndex + 1 < static_cast<int>(route.size()))
        {
            int nx = 0;
            int ny = 0;
            if (projectIdent(route[static_cast<size_t>(mDragRouteIndex + 1)], nx, ny))
            {
                lineTo(snapX, snapY, nx, ny, kMagenta);
            }
        }
    }
    else if (mDragRouteIndex >= 0)
    {
        const std::vector<std::string> &route = mPlan.route();
        if (mDragRouteIndex < static_cast<int>(route.size()))
        {
            const Waypoint *wpt = NavDb::instance().find(route[static_cast<size_t>(mDragRouteIndex)]);
            if (wpt != nullptr)
            {
                int ox = 0;
                int oy = 0;
                mMap.latLonToScreen(wpt->lat, wpt->lon, ox, oy);
                markerAt(ox, oy, 22, kAmber);
            }
        }
    }

    const glm::mat4 identity(1.0f);
    for (size_t i = start; i < mDrawCursor; ++i)
    {
        mDraws[i]->setTransformationMatrix(identity);
        mDraws[i]->render();
    }
}

void PlanningWidget::render()
{
    if (!mEnabled)
    {
        return;
    }
    if (mSimBriefPending && std::chrono::steady_clock::now() >= mSimBriefAt)
    {
        mSimBriefPending = false;
        mPlan.setRoute({"EPWR", "KUKUS", "DODUS", "EPKK"});
        mPlan.setActiveLegIndex(1);
        mPlan.setAltitudeFt(5500.0f);
        mPlan.setTasKt(118.0f);
        mPlan.setFuelOnBoardL(110.0f);
        mRouteBuffer = routeToString(mPlan.route());
        showToast("SIMBRIEF DEMO PLAN LOADED");
    }
    layout();
    mDrawCursor = 0;
    mHits.clear();

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    drawBackground();
    drawHeader();
    drawTabStrip();

    switch (mTab)
    {
    case Tab::Route:
        drawRouteTab();
        break;
    case Tab::Setup:
        drawSetupTab();
        break;
    case Tab::Briefing:
        drawBriefingTab();
        break;
    case Tab::Map:
        drawMapTab();
        break;
    }

    drawModal();
    drawToast();

    // Draw all Render2D that were used this frame.
    const glm::mat4 identity(1.0f);
    for (size_t i = 0; i < mDrawCursor; ++i)
    {
        mDraws[i]->setTransformationMatrix(identity);
        mDraws[i]->render();
    }
    if (mTab == Tab::Map)
    {
        mMap.render();
        drawMapOverlays();
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void PlanningWidget::cancelRouteDrag()
{
    mDragRouteIndex = -1;
    mSnapValid = false;
    mSnapIdent.clear();
}

void PlanningWidget::updateRouteSnap(int x, int y)
{
    mSnapValid = mMap.nearestMarked(x, y, 56.0f, mSnapIdent, mSnapLat, mSnapLon);
}

bool PlanningWidget::mouseClick(int x, int y)
{
    if (!mEnabled)
    {
        return false;
    }
    if (tapIsOnModeChip(x, y))
    {
        mController.leavePlanning();
        return true;
    }
    // Modal on top: only its hit rects count.
    if (mModalOpen)
    {
        // Search in reverse so top-most (modal) hits win.
        for (auto it = mHits.rbegin(); it != mHits.rend(); ++it)
        {
            const HitRect &r = *it;
            if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
            {
                if (r.action)
                {
                    r.action();
                }
                updateTextInput();
                return true;
            }
        }
        return true;
    }
    if (mTab == Tab::Map && mEditMode && mMap.contains(x, y))
    {
        const int idx = mMap.hitRoutePoint(x, y, 36.0f);
        if (idx >= 0)
        {
            mDragRouteIndex = idx;
            updateRouteSnap(x, y);
            return true;
        }
    }
    for (auto it = mHits.rbegin(); it != mHits.rend(); ++it)
    {
        const HitRect &r = *it;
        if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
        {
            if (r.action)
            {
                r.action();
            }
            updateTextInput();
            return true;
        }
    }
    // Tap outside any button while on the map still counts as clearing focus.
    mFocus = Focus::None;
    updateTextInput();
    return true;
}

bool PlanningWidget::mouseMove(int x, int y, int dx, int dy)
{
    if (!mEnabled || mTab != Tab::Map)
    {
        return false;
    }
    if (mDragRouteIndex >= 0)
    {
        updateRouteSnap(x, y);
        return true;
    }
    if (mMapPinch || mMap.contains(x, y))
    {
        mMap.pan(dx, dy);
        return true;
    }
    return false;
}

bool PlanningWidget::mouseUp(int, int)
{
    if (mDragRouteIndex >= 0)
    {
        if (mSnapValid)
        {
            const std::string prev = (mDragRouteIndex < static_cast<int>(mPlan.route().size()))
                                         ? mPlan.route()[static_cast<size_t>(mDragRouteIndex)]
                                         : std::string();
            mPlan.setWaypoint(mDragRouteIndex, mSnapIdent);
            mRouteBuffer = routeToString(mPlan.route());
            if (mSnapIdent != prev)
            {
                showToast(std::string("MOVED TO ") + mSnapIdent);
            }
        }
        else
        {
            showToast("NO SNAP TARGET");
        }
        cancelRouteDrag();
        mMapPinch = false;
        return true;
    }
    mMapPinch = false;
    return false;
}

bool PlanningWidget::mouseWheel(int x, int y, int dy)
{
    if (!mEnabled || mTab != Tab::Map)
    {
        return false;
    }
    if (mMap.contains(x, y))
    {
        mMap.zoomAt(static_cast<float>(dy) * 0.5f, x, y);
        return true;
    }
    return false;
}

bool PlanningWidget::pinch(int x, int y, float dz)
{
    if (!mEnabled || mTab != Tab::Map)
    {
        return false;
    }
    if (mMenu != nullptr && mMenu->isOpen())
    {
        return false;
    }
    if (!mMapPinch)
    {
        if (!mMap.contains(x, y))
        {
            return false;
        }
        cancelRouteDrag();
        mMapPinch = true;
    }
    mMap.zoomAt(dz, x, y);
    return true;
}

bool PlanningWidget::textInput(const char *text)
{
    if (!mEnabled || mFocus == Focus::None || text == nullptr)
    {
        return false;
    }
    std::string &buf = (mFocus == Focus::Search) ? mSearchQuery : mRouteBuffer;
    for (const char *p = text; *p != '\0'; ++p)
    {
        if (static_cast<unsigned char>(*p) >= 0x20 && static_cast<unsigned char>(*p) < 0x7F)
        {
            buf.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(*p))));
        }
    }
    if (mFocus == Focus::Search)
    {
        doWaypointSearch(mSearchQuery);
    }
    return true;
}

bool PlanningWidget::keyDown(SDL_Keycode key)
{
    if (!mEnabled)
    {
        return false;
    }
    if (mFocus == Focus::None)
    {
        return false;
    }
    std::string &buf = (mFocus == Focus::Search) ? mSearchQuery : mRouteBuffer;
    if (key == SDLK_BACKSPACE)
    {
        if (!buf.empty())
        {
            buf.pop_back();
        }
        if (mFocus == Focus::Search)
        {
            doWaypointSearch(mSearchQuery);
        }
        return true;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER)
    {
        if (mFocus == Focus::RouteString)
        {
            applyRouteString();
        }
        else
        {
            mFocus = Focus::None;
        }
        updateTextInput();
        return true;
    }
    if (key == SDLK_ESCAPE)
    {
        if (mModalOpen)
        {
            closeSearchModal();
        }
        else
        {
            mFocus = Focus::None;
        }
        updateTextInput();
        return true;
    }
    return false;
}
