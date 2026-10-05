/// \file app_controller.cpp
/// Applies the MODE, MAP, and AIP flags to the attitude instrument and the 3D world.
#include "app_controller.h"
#include "ahrs_widget.h"
#include "frame.h"
#include "iwidget.h"
#include "nav_voice.h"
#include "terrain_download.h"
#include "terrain_widget.h"
#include <iostream>

AppController::AppController(Frame &frame, AhrsWidget &ahrs, TerrainWidget &terrain)
    : mAhrs(ahrs), mTerrain(terrain)
{
    frame.addInputFront(this);
    applyLayers();
    TerrainDownload::instance().prepare();
    mTerrain.setSatFarZoom(mFarZoom);
    mTerrain.setSatDetailZoom(mSatZoom);
    std::cout << "Keys: 1 AHRS, 2 3D, 4/5 terrain picture in 3D, Esc quit\n";
    std::cout << "Touch: top-left menu for layout, map corner menu for the map\n";
}

bool AppController::keyDown(SDL_Keycode key)
{
    if (mPlanningOpen)
    {
        return false;
    }
    switch (key)
    {
    case SDLK_TAB:
        cycleAhrs();
        return true;
    case SDLK_1:
    case SDLK_F1:
        setPicture(ViewMode::Ahrs);
        return true;
    case SDLK_2:
    case SDLK_F2:
        setPicture(ViewMode::ThreeD);
        return true;
    case SDLK_3:
    case SDLK_F3:
        setPicture(ViewMode::ThreeD);
        return true;
    case SDLK_4:
    case SDLK_F4:
    case SDLK_5:
    case SDLK_F5:
        cycleMap();
        return true;
    default:
        return false;
    }
}

ViewMode AppController::view() const
{
    if (mPlanningOpen)
    {
        return ViewMode::Planning;
    }
    if (mSplit)
    {
        return ViewMode::Split;
    }
    return mPicture;
}

void AppController::cycleAhrs()
{
    setPicture(mPicture == ViewMode::ThreeD ? ViewMode::Ahrs : ViewMode::ThreeD);
}

void AppController::cycleMap()
{
    if (mPicture != ViewMode::ThreeD)
    {
        return;
    }
    setTerrainPicture(mMapMode == MapMode::Satellite ? MapMode::Simple : MapMode::Satellite);
}

void AppController::setTerrainPicture(MapMode mode)
{
    mMapMode = mode;
    applyLayers();
    touch();
}

void AppController::setAipWalls(bool on)
{
    mAipWalls = on;
    applyLayers();
    touch();
}

void AppController::setAipLabels(bool on)
{
    mAipText = on;
    applyLayers();
    touch();
}

void AppController::setVrpsOn(bool on)
{
    mVrpOn = on;
    applyLayers();
    touch();
}

void AppController::setObstaclesOn(bool on)
{
    mObstacles = on;
    applyLayers();
    touch();
}

void AppController::addCockpitLayer(IWidget *widget, CockpitRole role)
{
    if (widget != nullptr)
    {
        mCockpitLayers.push_back(CockpitEntry{widget, role});
        applyLayers();
    }
}

void AppController::setPlanningWidget(IWidget *widget)
{
    mPlanning = widget;
    applyLayers();
}

void AppController::setView(ViewMode mode)
{
    if (mode == ViewMode::Planning)
    {
        if (!mPlanningOpen)
        {
            mLastPicture = mPicture;
            mLastSplit = mSplit;
            mGeneralOpen = false;
        }
        mPlanningOpen = true;
    }
    else if (mode == ViewMode::Split)
    {
        mPlanningOpen = false;
        mSplit = true;
        mPicture = ViewMode::Ahrs;
    }
    else if (mode == ViewMode::Ahrs || mode == ViewMode::ThreeD)
    {
        mPlanningOpen = false;
        mSplit = false;
        mPicture = mode;
    }
    applyLayers();
    touch();
}

void AppController::setPicture(ViewMode mode)
{
    if (mode != ViewMode::Ahrs && mode != ViewMode::ThreeD)
    {
        return;
    }
    mPlanningOpen = false;
    mPicture = mode;
    applyLayers();
    touch();
}

void AppController::setSplit(bool on)
{
    mPlanningOpen = false;
    mSplit = on;
    applyLayers();
    touch();
}

void AppController::leavePlanning()
{
    if (!mPlanningOpen)
    {
        return;
    }
    mPlanningOpen = false;
    mPicture = mLastPicture;
    mSplit = mLastSplit;
    applyLayers();
    touch();
}

void AppController::setEnrPage(EnrPage page)
{
    mEnrPage = page;
    if (page == EnrPage::Nearest)
    {
        NavVoice::instance().announceNearest();
    }
    touch();
}

void AppController::toggleStats()
{
    mShowStats = !mShowStats;
    touch();
}

void AppController::setGeneralOpen(bool open)
{
    mGeneralOpen = open;
    touch();
}

void AppController::setFarCoverage(int zoom, int grid)
{
    mFarZoom = zoom;
    mFarGrid = grid;
    mTerrain.setSatFarZoom(mFarZoom);
    mTerrain.setSatFarGrid(mFarGrid);
}

void AppController::setNearCoverage(int zoom, int grid)
{
    mSatZoom = zoom;
    mNearGrid = grid;
    mTerrain.setSatDetailZoom(mSatZoom);
    mTerrain.setSatNearGrid(mNearGrid);
}

void AppController::applyLayers()
{
    const bool planning = mPlanningOpen;
    const bool split = mSplit && !planning;
    const bool threeD = !planning && mPicture == ViewMode::ThreeD;
    const bool ahrsPicture = !planning && mPicture == ViewMode::Ahrs;
    const bool efis = threeD || ahrsPicture;
    mTerrain.enable(threeD);
    mTerrain.setSplit(split && threeD);
    mTerrain.setSatelliteGround(threeD && mMapMode == MapMode::Satellite);
    mTerrain.setChartOverlay(false);
    mTerrain.setAirspaceWalls(mAipWalls);
    mTerrain.setAirspaceLabels(mAipText);
    mTerrain.setAirspacesEnabled(threeD && (mAipWalls || mAipText));
    mTerrain.setVrpsEnabled(mVrpOn);
    mTerrain.setObstaclesEnabled(mObstacles);
    mAhrs.enable(efis);
    mAhrs.setDrawSkyGround(ahrsPicture);
    mAhrs.setSplit(split);
    for (const CockpitEntry &layer : mCockpitLayers)
    {
        if (layer.widget == nullptr)
        {
            continue;
        }
        const bool dial = layer.role == CockpitRole::Dial;
        layer.widget->enable(dial ? (efis && !split) : efis);
        layer.widget->setSplit(!dial && split);
    }
    if (mPlanning != nullptr)
    {
        mPlanning->enable(planning);
    }
}

void AppController::touch()
{
    ++mRevision;
}
