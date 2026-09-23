/// \file openaip_widget.cpp
/// Draws a flat OpenAIP tile mosaic. The live view uses SatClipmap instead.
#include "openaip_widget.h"

#include <GLES3/gl3.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

OpenAipWidget::OpenAipWidget(Frame &frame, IDataManager &dataManager)
    : IWidget(frame), mDataManager(dataManager), mOwnship(frame.screen())
{
    mEnabled = false;
    mDataManager.attach(this, DataType::LOCATION_DATA);
    const int cx = mScreen.getWidth() / 2;
    const int cy = mScreen.getHeight() / 2;
    mOwnship.drawRectangle(cx - 5, cy - 14, 10, 28, 0xFFCC00FFu);
}

void OpenAipWidget::enable(bool enable)
{
    mEnabled = enable;
}

void OpenAipWidget::setPos(int x, int y)
{
    (void)x;
    (void)y;
}

void OpenAipWidget::update(DataType type)
{
    if (type != DataType::LOCATION_DATA)
    {
        return;
    }
    mLocation = mDataManager.getLocationData();
    OpenAipClient::instance().fetchAround(mLocation.latitude, mLocation.longitude, kZoom, kRadius);
}

uint64_t OpenAipWidget::tileKey(int x, int y) const
{
    return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(y);
}

void OpenAipWidget::ensureTile(int tileX, int tileY)
{
    auto &client = OpenAipClient::instance();
    if (!client.isCached(kZoom, tileX, tileY))
    {
        return;
    }
    const uint64_t key = tileKey(tileX, tileY);
    if (mTiles.count(key))
    {
        return;
    }
    auto renderer = std::make_unique<Render2D>(mScreen);
    renderer->drawTexture(client.cachePath(kZoom, tileX, tileY), 0, 0, 256, 256);
    mTiles.emplace(key, std::move(renderer));
}

void OpenAipWidget::render()
{
    if (!mEnabled)
    {
        return;
    }

    OpenAipClient::instance().fetchAround(mLocation.latitude, mLocation.longitude, kZoom, kRadius);

    const auto center = OpenAipClient::latLonToTile(mLocation.latitude, mLocation.longitude, kZoom);
    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            ensureTile(center.first + dx, center.second + dy);
        }
    }

    double acX = 0.0;
    double acY = 0.0;
    OpenAipClient::latLonToPixels(mLocation.latitude, mLocation.longitude, kZoom, acX, acY);

    const float cx = static_cast<float>(mScreen.getWidth()) * 0.5f;
    const float cy = static_cast<float>(mScreen.getHeight()) * 0.5f;
    const float scale = std::min(static_cast<float>(mScreen.getWidth()), static_cast<float>(mScreen.getHeight())) / (3.0f * 256.0f);
    const float heading = mDataManager.getAttitudeData().heading;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);

    glm::mat4 view(1.0f);
    view = glm::translate(view, glm::vec3(cx, cy, 0.0f));
    view = glm::rotate(view, -heading, glm::vec3(0.0f, 0.0f, 1.0f));
    view = glm::translate(view, glm::vec3(-cx, -cy, 0.0f));

    for (int dy = -kRadius; dy <= kRadius; ++dy)
    {
        for (int dx = -kRadius; dx <= kRadius; ++dx)
        {
            const int tileX = center.first + dx;
            const int tileY = center.second + dy;
            auto it = mTiles.find(tileKey(tileX, tileY));
            if (it == mTiles.end())
            {
                continue;
            }
            const double x0 = static_cast<double>(tileX) * 256.0;
            const double y1 = static_cast<double>(tileY + 1) * 256.0;
            const float left = cx + static_cast<float>((x0 - acX) * scale);
            const float bottom = cy - static_cast<float>((y1 - acY) * scale);

            glm::mat4 transform = view;
            transform = glm::translate(transform, glm::vec3(left, bottom, 0.0f));
            transform = glm::scale(transform, glm::vec3(scale, scale, 1.0f));
            it->second->setTransformationMatrix(transform);
            it->second->render();
        }
    }

    renderOwnship();
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void OpenAipWidget::renderOwnship()
{
    mOwnship.render();
}
