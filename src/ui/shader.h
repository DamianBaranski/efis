/// \file shader.h
/// GLES program, mesh upload, and the shared satellite-clip uniforms.
#ifndef SHADER_H
#define SHADER_H

#include <GLES3/gl3.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <SDL.h>
#include "data_type.h"

/// One GLES program plus the meshes and textures it draws.
class Shader
{
public:
    /// Compiles the terrain program.
    Shader();

    /// Releases GPU buffers held by this program.
    ~Shader();

    /// Draws every mesh uploaded with setTriangles.
    void render() const;

    /// Model-view-projection for the next draw, column-major.
    /// \param mvpMat Combined matrix.
    void setMvpMatrix(glm::mat4 mvpMat);

    /// Replaces the GPU mesh. Call clearGeometry first when swapping a previous mesh.
    /// \param triangles Material groups to upload.
    void setTriangles(const std::vector<Triangles> &triangles);

    /// Drops GPU geometry so setTriangles can replace a previous mesh.
    void clearGeometry();

    /// @brief Sets the texture for rendering.
    /// @param name The name of the texture.
    /// @param surface The SDL surface containing the texture data.
    void setTexture(const std::string &name, SDL_Surface *surface);

    /// @brief Gets the width of a texture.
    /// @param name The name of the texture.
    /// @return The width of the texture.
    int getTextureWidth(const std::string &name);

    /// @brief Gets the height of a texture.
    /// @param name The name of the texture.
    /// @return The height of the texture.
    int getTextureHeight(const std::string &name);

    /// @brief Sets the color for rendering.
    /// @param name The name of the color parameter in the shader.
    /// @param rgba The color value in RGBA format.
    void setColor(const std::string &name, uint32_t rgba);

    /// RGB multiply applied in the fragment shader. (1,1,1) is identity.
    void setColorScale(float r, float g, float b);

    /// Terrain meshes sample the satellite clipmap when the global overlay is active.
    void enableOpenAipOverlay(bool enable) { mOpenAipOverlay = enable; }

    /// Binds the three satellite rings for every terrain mesh drawn after this call.
    /// Origins are XYZ tile columns and rows. Masks mark which slots hold a tile.
    /// \param camU Mercator U of the camera, in [0, 1].
    /// \param camV Mercator V of the camera, in [0, 1]. Y grows south.
    static void setSatClip(bool active, GLuint fineTex, GLuint midTex, GLuint wideTex, int fineOriginX,
                           int fineOriginY, int midOriginX, int midOriginY, int wideOriginX, int wideOriginY,
                           int fineZoom, int midZoom, int wideZoom, int fineGrid, const uint32_t *fineMask, int midGrid,
                           const uint32_t *midMask, uint32_t wideMask0, uint32_t wideMask1, float camU = 0.0f,
                           float camV = 0.0f);

    /// GPU bytes held by textures loaded through setTexture.
    static size_t textureCacheBytes();

private:
    /// @brief Loads a texture from file.
    /// @param filename The path to the texture file.
    /// @return The OpenGL texture ID.
    GLuint texLoad(const std::string &filename);

    /// @brief Initializes the shader program.
    void initializeShaderProgram();

    /// @brief Creates an index buffer object (IBO) for the given indices.
    /// @param indices The vector of indices.
    /// @return The OpenGL buffer ID for the IBO.
    GLuint iboCreate(const std::vector<GLuint> &indices);

    /// @brief Creates a vertex buffer object (VBO) for the given vertices.
    /// @param vertices The vector of vertices.
    /// @return The OpenGL buffer ID for the VBO.
    GLuint vboCreate(const std::vector<VertexTexture> &vertices);

    /// @brief Loads and compiles a shader from file.
    /// @param filename The path to the shader file.
    /// @param shaderType The type of shader (GL_VERTEX_SHADER or GL_FRAGMENT_SHADER).
    /// @return The OpenGL shader ID.
    GLuint loadShader(const std::string &filename, GLenum shaderType);

    /// @brief Loads and links the shader program from vertex and fragment shader files.
    /// @param vertFilename The path to the vertex shader file.
    /// @param fragFilename The path to the fragment shader file.
    /// @return The OpenGL shader program ID.
    GLuint shaderProgLoad(const std::string &vertFilename, const std::string &fragFilename);

    GLuint mShaderProgram; ///< The OpenGL shader program ID.
    typedef struct
    {
        GLuint mIbo;         ///< The OpenGL index buffer object (IBO) ID.
        GLuint mVbo;         ///< The OpenGL vertex buffer object (VBO) ID.
        GLuint mTexture;     ///< The OpenGL texture ID.
        size_t mIndicesSize; ///< The size of indices for the object.
    } BufferLocations;

    typedef struct
    {
        GLuint mTbo; ///< The OpenGL texture buffer object ID.
        int mWidth;  ///< The width of the texture.
        int mHeight; ///< The height of the texture.
    } TextureData;

    std::vector<BufferLocations> mBufferLocations;                     ///< Vector to store buffer locations.
    glm::mat4 mMvpMat;                                                 ///< The model-view-projection matrix.
    GLint mMvpMatrixLoc;                                               ///< The location of the model-view-projection matrix in the shader.
    GLint mUseOpenAipLoc = -1;
    GLint mSatFineLoc = -1;
    GLint mSatMidLoc = -1;
    GLint mSatWideLoc = -1;
    GLint mSatFineOriginLoc = -1;
    GLint mSatMidOriginLoc = -1;
    GLint mSatWideOriginLoc = -1;
    GLint mSatFineMaskLoc = -1;
    GLint mSatMidMaskLoc = -1;
    GLint mSatWideMaskLoc = -1;
    GLint mSatFineZoomLoc = -1;
    GLint mSatMidZoomLoc = -1;
    GLint mSatWideZoomLoc = -1;
    GLint mSatFineGridLoc = -1;
    GLint mSatFineBitsLoc = -1;
    GLint mSatMidGridLoc = -1;
    GLint mSatMidBitsLoc = -1;
    GLint mSatCamUvLoc = -1;
    GLint mColorScaleLoc = -1;
    glm::vec3 mColorScale{1.0f, 1.0f, 1.0f};
    bool mOpenAipOverlay = false;
    static std::unordered_map<std::string, TextureData> mTextureCache; ///< Cache for loaded textures.

    struct SatClipState
    {
        bool active = false;
        GLuint fineTex = 0;
        GLuint midTex = 0;
        GLuint wideTex = 0;
        int fineOriginX = 0;
        int fineOriginY = 0;
        int midOriginX = 0;
        int midOriginY = 0;
        int wideOriginX = 0;
        int wideOriginY = 0;
        int fineZoom = 16;
        int midZoom = 13;
        int wideZoom = 11;
        int fineGrid = 8;
        int midGrid = 8;
        uint32_t fineMask[8]{};
        uint32_t midMask[8]{};
        uint32_t wideMask0 = 0;
        uint32_t wideMask1 = 0;
        float camU = 0.0f;
        float camV = 0.0f;
    };
    static SatClipState sSat;
};

#endif // SHADER_H
