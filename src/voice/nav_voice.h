/// \file nav_voice.h
/// Spoken cues for airspace, reporting points, obstacles, and the nearest field.
#ifndef NAV_VOICE_H
#define NAV_VOICE_H

#include "vrp_overlay.h"
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/// Event voice for airspace, compulsory reporting points, obstacles, and nearest field.
class NavVoice
{
public:
    /// Process-wide phrase rules.
    static NavVoice &instance();

    /// Aircraft position used by the phrase rules.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    void setPosition(double latitude, double longitude);

    /// Master switch. Off still updates state, and speaks nothing.
    void setNarration(bool on) { mNarration = on; }
    /// Speaks airspace entry and exit.
    void setAirspace(bool on) { mAirspace = on; }
    /// Speaks compulsory reporting points.
    void setReporting(bool on) { mReporting = on; }
    /// Speaks the nearest field when ENR nearest is chosen.
    void setNearest(bool on) { mNearest = on; }
    /// Speaks the nearest relevant obstacle.
    void setObstacles(bool on) { mObstacles = on; }
    /// True when phrases are allowed to reach the speaker.
    bool narration() const { return mNarration; }
    /// True when airspace phrases are armed.
    bool airspace() const { return mAirspace; }
    /// True when reporting-point phrases are armed.
    bool reporting() const { return mReporting; }
    /// True when the nearest-field phrase is armed.
    bool nearest() const { return mNearest; }
    /// True when obstacle phrases are armed.
    bool obstacles() const { return mObstacles; }

    /// Speaks a short test phrase on the selected voice.
    void preview();

    /// Moves the selected voice by delta entries in the catalog.
    void stepVoice(int delta);
    /// Labels of the installed voices, in catalog order.
    std::vector<std::string> voiceLabels();
    /// Index of the selected voice in voiceLabels().
    int voiceIndex();
    /// Selects a catalog entry and applies it.
    /// \param index Index from voiceLabels(). Out of range is ignored.
    void selectVoice(int index);
    /// Label of the selected voice, for the sound page.
    std::string voiceLabel();

    /// vertical: -1 below the floor, 0 inside the band, +1 above the ceiling.
    void noteAirspace(const std::string &name, int type, const std::string &lowerLabel, double distM, bool horizontal,
                      int vertical);

    /// Speaks a reporting point when the aircraft reaches 2 NM, and again overhead.
    /// The first sample is a baseline and is not spoken.
    /// \param latitude Degrees, north positive.
    /// \param longitude Degrees, east positive.
    void updateReporting(double latitude, double longitude, const std::vector<VrpOverlay::Point> &points);

    /// One obstacle the voice logic may speak.
    struct ObstacleCue
    {
        std::string name;
        double latitude = 0.0;  ///< Degrees, north positive.
        double longitude = 0.0; ///< Degrees, east positive.
        float heightM = 0.0f;   ///< Metres AGL. 0 when the source has no height.
        int kind = 0;           ///< 0 wind, 1 chimney, 2 tower, 3 building.
    };

    /// Speaks the nearest relevant obstacle inside 3 NM.
    /// Wind farms collapse to one phrase. The first sample is a baseline.
    void updateObstacles(double latitude, double longitude, const std::vector<ObstacleCue> &points);

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
    bool mObstacles = true;

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

    bool mObstPrimed = false;
    std::string mObstKey;
    bool mObstTwo = false;
    bool mObstOver = false;

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
