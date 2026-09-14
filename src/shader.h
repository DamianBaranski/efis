/// \file shader.h
/// \brief Contains the declaration of the Shader class, which encapsulates functionality related to OpenGL shaders.
#ifndef SHADER_H
#define SHADER_H

#include <GLES3/gl3.h>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <SDL.h>
#include "data_type.h"

/// @brief The Shader class encapsulates functionality related to OpenGL shaders.
class Shader
{
public:
    /// @brief Constructs a new Shader object.
    Shader();

    /// @brief Destroys the Shader object.
    ~Shader();

    /// @brief Renders the objects using the shader program.
    void render() const;

    /// @brief Sets the model-view-projection matrix for the shader.
    /// @param mvpMat The model-view-projection matrix.
    void setMvpMatrix(glm::mat4 mvpMat);

    /// @brief Sets the triangles to be rendered.
    /// @param triangles The vector of triangles to render.
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

    /// Terrain buckets sample the OpenAIP atlas with geographic UVs when the global overlay is active.
    void enableOpenAipOverlay(bool enable) { mOpenAipOverlay = enable; }

    static void setOpenAipGround(bool active, GLuint texture, float originX, float originY,
                                 float tilesX, float tilesY, float n);

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
    GLint mOpenAipSamplerLoc = -1;
    GLint mOpenAipAtlasLoc = -1;
    GLint mOpenAipNLoc = -1;
    bool mOpenAipOverlay = false;
    static std::unordered_map<std::string, TextureData> mTextureCache; ///< Cache for loaded textures.

    struct OpenAipGroundState
    {
        bool active = false;
        GLuint texture = 0;
        float originX = 0.0f;
        float originY = 0.0f;
        float tilesX = 1.0f;
        float tilesY = 1.0f;
        float n = 1.0f;
    };
    static OpenAipGroundState sOpenAip;
};

#endif // SHADER_H
