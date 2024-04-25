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

    void setTexture(const std::string &name, SDL_Surface *surface);

    void setColor(const std::string &name, uint32_t rgba);
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
    std::vector<BufferLocations> mBufferLocations;         ///< Vector to store buffer locations.
    glm::mat4 mMvpMat;                                     ///< The model-view-projection matrix.
    GLint mMvpMatrixLoc;                                   ///< The location of the model-view-projection matrix in the shader.
    std::unordered_map<std::string, GLuint> mTextureCache; ///< Cache for loaded textures.
};

#endif // SHADER_H
