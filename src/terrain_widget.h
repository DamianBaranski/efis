#include "iobserver.h"
#include "iwidget.h"
#include "idata_manager.h"
#include "data_type.h"
#include "btg_file.h"
#include "shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "render2d.h"

class TerrainWidget : IWidget
{
public:
    TerrainWidget(Screen &screen) : IWidget(screen)
    {
        mMapFile.load("../resources/terraGit/3039691.btg.gz");
        mShader.setTriangles(mMapFile.generateTriangles());
        mViewMat = glm::translate(glm::mat4(1.0f), glm::vec3(0,0, 0));
        mProjMat = glm::perspective(glm::radians(60.0f), (float)mScreen.getWidth() / mScreen.getHeight(), 1.0f, 10000000.0f);
        
    }

    virtual void render() const
    {
        glm::mat4 mvpMat = mProjMat * glm::rotate(mViewMat, mCamAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        mShader.setMvpMatrix(mvpMat);
        mShader.render();
        mCamAngle += 0.01;
    }

    virtual void setPos(int x, int y)
    {
        (void)x;
        (void)y;
    }

private:
    BtgFile mMapFile;
    mutable Shader mShader;
    mutable float mCamAngle;
    glm::mat4 mViewMat;
    glm::mat4 mProjMat;
};