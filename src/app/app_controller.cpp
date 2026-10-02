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
    std::cout << "Keys: 1 AHRS only, 2 AHRS off, 3 overlay, 4 sat/simple, 5 AIP, Esc quit\n";
    std::cout << "Touch: tap for layer menu (3s), double-tap to keep it until a choice\n";
}

bool AppController::keyDown(SDL_Keycode key)
{
    if (mView == ViewMode::Planning)
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
        setView(ViewMode::Ahrs);
        return true;
    case SDLK_2:
    case SDLK_F2:
        setView(ViewMode::ThreeD);
        return true;
    case SDLK_3:
    case SDLK_F3:
        setView(ViewMode::ThreeD);
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

void AppController::cycleAhrs()
{
    setView(mView == ViewMode::ThreeD ? ViewMode::Ahrs : ViewMode::ThreeD);
}

void AppController::cycleMap()
{
    setMap(mMapMode == MapMode::Satellite ? MapMode::Simple : MapMode::Satellite);
}

void AppController::toggleAip(int row)
{
    if (row == 1 || row == 2)
    {
        const bool on = !(mAipWalls || mAipText);
        mAipWalls = on;
        mAipText = on;
    }
    else if (row == 3)
    {
        mVrpOn = !mVrpOn;
    }
    else
    {
        mObstacles = !mObstacles;
    }
    applyLayers();
    touch();
}

void AppController::addCockpitLayer(IWidget *widget)
{
    if (widget != nullptr)
    {
        mCockpitLayers.push_back(widget);
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
    if (mode == ViewMode::Planning && mView != ViewMode::Planning)
    {
        mLastEfisView = (mView == ViewMode::Ahrs) ? ViewMode::Ahrs : ViewMode::ThreeD;
        mGeneralOpen = false;
    }
    mView = mode;
    if (mode == ViewMode::Ahrs || mode == ViewMode::ThreeD || mode == ViewMode::Planning)
    {
        applyLayers();
    }
    touch();
}

void AppController::leavePlanning()
{
    if (mView != ViewMode::Planning)
    {
        return;
    }
    setView(mLastEfisView);
}

void AppController::setMap(MapMode mode)
{
    mMapMode = mode;
    if (mView != ViewMode::ThreeD)
    {
        mView = ViewMode::ThreeD;
    }
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
    const bool threeD = mView == ViewMode::ThreeD;
    const bool ahrsOnly = mView == ViewMode::Ahrs;
    const bool planning = mView == ViewMode::Planning;
    mTerrain.enable(threeD);
    mTerrain.setSatelliteGround(threeD && mMapMode == MapMode::Satellite);
    mTerrain.setChartOverlay(false);
    mTerrain.setAirspaceWalls(mAipWalls);
    mTerrain.setAirspaceLabels(mAipText);
    mTerrain.setAirspacesEnabled(threeD && (mAipWalls || mAipText));
    mTerrain.setVrpsEnabled(mVrpOn);
    mTerrain.setObstaclesEnabled(mObstacles);
    mAhrs.enable((threeD || ahrsOnly) && !planning);
    mAhrs.setDrawSkyGround(ahrsOnly);
    for (IWidget *layer : mCockpitLayers)
    {
        if (layer != nullptr)
        {
            layer->enable(!planning);
        }
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
