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
        mShader.setColor("colorRed", 0x55000001);
        Triangles triangle;
        triangle.material = "../resources/textures/deciduous1.png";
          float cubeSize_2 = 100.0f / 2.0f; // Half the cube's size
        std::vector<VertexTexture> vertices = {
            // Front face
            {{-cubeSize_2+100, -cubeSize_2, cubeSize_2}, {0.0f, 0.0f}},
            {{cubeSize_2+100, -cubeSize_2, cubeSize_2}, {1.0f, 0.0f}},
            {{cubeSize_2+100, cubeSize_2, cubeSize_2}, {1.0f, 1.0f}},
            {{-cubeSize_2+100, cubeSize_2, cubeSize_2}, {0.0f, 1.0f}},
            // Back face
            {{cubeSize_2+100, -cubeSize_2, -cubeSize_2}, {0.0f, 0.0f}},
            {{-cubeSize_2+100, -cubeSize_2, -cubeSize_2}, {1.0f, 0.0f}},
            {{-cubeSize_2+100, cubeSize_2, -cubeSize_2}, {1.0f, 1.0f}},
            {{cubeSize_2+100, cubeSize_2, -cubeSize_2}, {0.0f, 1.0f}},
            // Left face
            {{-cubeSize_2+100, -cubeSize_2, -cubeSize_2}, {0.0f, 0.0f}},
            {{-cubeSize_2+100, -cubeSize_2, cubeSize_2}, {1.0f, 0.0f}},
            {{-cubeSize_2+100, cubeSize_2, cubeSize_2}, {1.0f, 1.0f}},
            {{-cubeSize_2+100, cubeSize_2, -cubeSize_2}, {0.0f, 1.0f}},
            // Right face
            {{cubeSize_2+100, -cubeSize_2, cubeSize_2}, {0.0f, 0.0f}},
            {{cubeSize_2+100, -cubeSize_2, -cubeSize_2}, {1.0f, 0.0f}},
            {{cubeSize_2+100, cubeSize_2, -cubeSize_2}, {1.0f, 1.0f}},
            {{cubeSize_2+100, cubeSize_2, cubeSize_2}, {0.0f, 1.0f}},
            // Top face
            {{cubeSize_2+100, cubeSize_2, -cubeSize_2}, {0.0f, 0.0f}},
            {{-cubeSize_2+100, cubeSize_2, -cubeSize_2}, {1.0f, 0.0f}},
            {{-cubeSize_2+100, cubeSize_2, cubeSize_2}, {1.0f, 1.0f}},
            {{cubeSize_2+100, cubeSize_2, cubeSize_2}, {0.0f, 1.0f}},
            // Bottom face
            {{-cubeSize_2+100, -cubeSize_2, -cubeSize_2}, {0.0f, 0.0f}},
            {{cubeSize_2+100, -cubeSize_2, -cubeSize_2}, {1.0f, 0.0f}},
            {{cubeSize_2+100, -cubeSize_2, cubeSize_2}, {1.0f, 1.0f}},
            {{-cubeSize_2+100, -cubeSize_2, cubeSize_2}, {0.0f, 1.0f}}};


        const GLsizei vertsPerSide = 4;
        const GLsizei numSides = 6;
        const GLsizei indicesPerSide = 6;
        const GLsizei numIndices = indicesPerSide * numSides;
        std::vector<GLushort> indices;
        indices.reserve(numIndices); // Reserve memory for indices
        GLuint i = 0;
        for (GLushort j = 0; j < numSides; ++j)
        {
            GLushort sideBaseIdx = j * vertsPerSide;
            indices.push_back(sideBaseIdx + 0);
            indices.push_back(sideBaseIdx + 1);
            indices.push_back(sideBaseIdx + 2);
            indices.push_back(sideBaseIdx + 2);
            indices.push_back(sideBaseIdx + 3);
            indices.push_back(sideBaseIdx + 0);
        }

        triangle.indices = indices;
        triangle.vertex = vertices;
        std::vector<Triangles> tri;
        tri.push_back(triangle);

        for(auto &ver : triangle.vertex) {
            ver.vertex.x-=100.01;
        }
        triangle.material = "colorRed";
        tri.push_back(triangle);
        mShader.setTriangles(tri);
        mShader.render();
    }

    virtual void render() const
    {
        float camPosX = 0.0f;
        float camPosY = 0.0f;
        float camPosZ = 150.0f;
        glm::mat4 viewMat = glm::translate(glm::mat4(1.0f), glm::vec3(-camPosX, -camPosY, -camPosZ));

        glm::mat4 projMat = glm::perspective(glm::radians(60.0f), (float)mScreen.getWidth() / mScreen.getHeight(), 1.0f, 1000.0f);

        glm::mat4 modelMat = glm::rotate(glm::mat4(1.0f), (float)M_PI / 4, glm::vec3(1.0f, 0.0f, 0.0f));
        modelMat = glm::rotate(modelMat, mCamAngle, glm::vec3(0.0f, 1.0f, 0.0f));

        glm::mat4 mvpMat = projMat * viewMat * modelMat;
        mShader.setMvpMatrix(mvpMat);
        mShader.render();
        mCamAngle+=0.01;
    }

    virtual void enable(bool enable)
    {
    }

    virtual void setPos(int x, int y)
    {
    }

private:
    BtgFile mMapFile;
    mutable Shader mShader;
    mutable float mCamAngle;
    glm::mat4 mViewMat;
    glm::mat4 mProjMat;
    glm::mat4 mMvpMat;
};