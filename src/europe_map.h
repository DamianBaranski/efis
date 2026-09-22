#ifndef EUROPE_MAP_H
#define EUROPE_MAP_H

#include "europe_borders.h"
#include "render2d.h"
#include "screen.h"
#include "shader.h"

#include <vector>

/// Schematic Europe map drawn from simplified country rings. Seas are the panel.
class EuropeMap
{
public:
    explicit EuropeMap(Screen &screen);

    void layout(int contentX, int contentY, int contentW, int contentH, int screenW, int screenH);
    void render();
    bool hit(int sdlX, int sdlY);

private:
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    static VertexTexture vert(float x, float y);
    void project();
    void rebuildLand();
    void rebuildStroke();
    void rebuildSelect();
    void rebuildLabel();
    void setOrtho();
    int pickCountry(float glX, float glY) const;
    static bool ringContains(float x, float y, const Point *pts, int count);
    static bool clipWall(int vertexA, int vertexB);

    Shader mLand;
    Shader mStroke;
    Shader mSelect;
    Render2D mLabel;
    std::vector<Point> mProj;
    glm::mat4 mMvp{1.0f};
    int mContentX = 0;
    int mContentY = 0;
    int mContentW = 0;
    int mContentH = 0;
    int mScreenW = 0;
    int mScreenH = 0;
    int mSelected = -1;
    bool mLabelOn = false;
    bool mReady = false;
};

#endif
