/// \file ahrs_widget.h
/// \brief Contains the declaration of the AhrsWidget class, which represents a widget for Attitude and Heading Reference System (AHRS).

#ifndef AHRS_WIDGET
#define AHRS_WIDGET

#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "data_type.h"
#include "render2d.h"

/// \class AhrsWidget
/// \brief Represents a widget for Attitude and Heading Reference System (AHRS).
class AhrsWidget : public IObserver<DataType>, public IWidget
{
public:
    /// \brief Constructs an AhrsWidget object.
    /// \param screen The screen to render the widget on.
    /// \param dataManager The data manager providing attitude data.
    AhrsWidget(Screen &screen, IDataManager &dataManager);

    /// \brief Renders the AHRS widget.
    void render() override;

    /// \brief Sets the position of the AHRS widget.
    /// \param x The x-coordinate of the position.
    /// \param y The y-coordinate of the position.
    void setPos(int x, int y) override;

    /// \brief Updates the AHRS widget with new data.
    /// \param type The type of data being updated.
    void update(DataType type) override;

    /// \brief When false, skip the painted sky/ground tape so 3D terrain can show through.
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
    bool mDrawSkyGround = true;
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
