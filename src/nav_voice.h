#ifndef NAV_VOICE_H
#define NAV_VOICE_H

#include "vrp_overlay.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// Event voice for airspace, compulsory reporting points, and nearest field.
class NavVoice
{
public:
    static NavVoice &instance();

    void setPosition(double latitude, double longitude);

    void setNarration(bool on) { mNarration = on; }
    void setAirspace(bool on) { mAirspace = on; }
    void setReporting(bool on) { mReporting = on; }
    void setNearest(bool on) { mNearest = on; }
    bool narration() const { return mNarration; }
    bool airspace() const { return mAirspace; }
    bool reporting() const { return mReporting; }
    bool nearest() const { return mNearest; }

    void preview();

    void stepVoice(int delta);
    std::string voiceLabel();

    /// vertical: -1 below the floor, 0 inside the band, +1 above the ceiling.
    void noteAirspace(const std::string &name, int type, const std::string &lowerLabel, double distM, bool horizontal,
                      int vertical);

    void updateReporting(double latitude, double longitude, const std::vector<VrpOverlay::Point> &points);

    /// ENR nearest. Speaks the closest field in the airport catalog.
    void announceNearest();

private:
    NavVoice() = default;

    void say(const std::string &key, const std::string &text, int priority, bool channel);

    double mLatitude = 0.0;
    double mLongitude = 0.0;
    bool mHavePosition = false;
    bool mNarration = true;
    bool mAirspace = true;
    bool mReporting = true;
    bool mNearest = true;

    struct AirMem
    {
        bool known = false;
        bool inside = false;
        bool under = false;
        bool over = false;
        bool arm2 = false;
        bool arm5 = false;
        double lastDistM = -1.0;
    };
    std::vector<std::pair<std::string, AirMem>> mAir;
    std::vector<std::pair<std::string, uint32_t>> mRecent;

    bool mVrpPrimed = false;
    std::string mVrpKey;
    bool mVrpTwo = false;
    bool mVrpOver = false;

    bool mAirportsLoaded = false;
    struct Field
    {
        std::string icao;
        std::string name;
        double latitude = 0.0;
        double longitude = 0.0;
        std::string runway;
        float lengthM = 0.0f;
    };
    std::vector<Field> mFields;
};
#endif
