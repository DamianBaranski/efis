/// \file ahrs_widget.h
/// One attitude instrument for AHRS mode and 3D mode.

#ifndef AHRS_WIDGET
#define AHRS_WIDGET

#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "data_type.h"
#include "render2d.h"

/// Pitch, roll, slip, and the aircraft symbol stay up in both AHRS and 3D.
/// The sky and ground tape is drawn only in AHRS mode.
class AhrsWidget : public IObserver<DataType>, public IWidget
{
public:
    /// Registers with the frame and subscribes to attitude updates.
    /// \param frame Loop that draws this widget. Must outlive it.
    /// \param dataManager Situation source. Must outlive this object.
    AhrsWidget(Frame &frame, IDataManager &dataManager);

    /// Draws pitch, roll, slip, and the aircraft symbol.
    void render() override;

    /// Places the instrument. Coordinates are pixels, origin at the top left.
    /// \param x Left edge.
    /// \param y Top edge.
    void setPos(int x, int y) override;

    /// Copies a new attitude sample. Other channels are ignored.
    /// \param type Situation channel that changed.
    void update(DataType type) override;

    /// Shows or hides the sky and ground tape. The aircraft symbol is unaffected.
    void setDrawSkyGround(bool draw) { mDrawSkyGround = draw; }

private:
    void rebuildSprites();
    float hudScale() const;

    /// \brief Updates the internal renderers based on the new attitude data.
    void updateRenderers();

    IDataManager &mDataManager;            ///< Reference to the data manager providing attitude data.
    Render2D mLandRepresentation;  ///< Representation of land in the AHRS widget.
    Render2D mHorizonLine;         ///< Representation of the horizon line in the AHRS widget.
    Render2D mPithScale;           ///< Representation of the pitch scale in the AHRS widget.
    Render2D mRollPointer;                 ///< Representation of the roll pointer in the AHRS widget.
    Render2D mSkipSkidIndicator;           ///< Representation of the skip/skid indicator in the AHRS widget.
    Render2D mAttitudeIndicator;   ///< Representation of the attitude indicator in the AHRS widget.
    Render2D mAircraftSymbol;              ///< Representation of the aircraft symbol in the AHRS widget.
    AttitudeData mAttitudeData;    ///< Attitude data.
    bool mAttitudeDataUpdated;
    bool mDrawSkyGround = true; ///< Land and horizon tape. False in 3D so terrain shows through.
    int mAttitudeY;
    int mLayoutX = 0;
    int mLayoutW = 0;
    int mLayoutH = 0;
    float mHudScale = 1.0f;

    static constexpr const char *cResourcesPath = "../resources/textures/ui/AHRS/"; ///< Path to the resources directory.
    static constexpr const char *cPithScaleTexture = "layer10.png";                 ///< Texture file for the pitch scale.
    static constexpr const char *cRollPointerTexture = "layer5.png";                ///< Texture file for the roll pointer.
    static constexpr const char *cSkipSkidIndicatorTexture = "layer7.png";          ///< Texture file for the skip/skid indicator.
    static constexpr const char *cAttitudeIndicatorTexture = "layer4.png";          ///< Texture file for the attitude indicator.
    static constexpr const char *cAircraftSymbolTexture = "layer11.png";            ///< Texture file for the aircraft symbol.
    static constexpr const char *cHorizonLineTexture = "layer6.png";                ///< Texture file for the horizon line.
    static constexpr const char *cLandRepresentationTexture = "layer3.png";         ///< Texture file for the land representation.
    static constexpr float cDesignHeight = 600.0f;
    static constexpr float cPixelPerPitchRadians = cDesignHeight / (2 * M_PI / 360.0f * 60.0f);
};

#endif
