#include "shader.h"
#include <SDL_image.h>
#include <SDL_opengles2.h>
#include <iostream>
#include <fstream>

Shader::Shader() : mShaderProgram(0)
{
    initializeShaderProgram();
}

Shader::~Shader()
{
    glDeleteProgram(mShaderProgram);
    for (auto &buffer : mBufferLocations)
    {
        glDeleteBuffers(1, &buffer.mIbo);
        glDeleteBuffers(1, &buffer.mVbo);
    }
    for (auto &tex : mTextureCache)
    {
        glDeleteTextures(1, &tex.second);
    }
}

void Shader::render() const
{
    glUseProgram(mShaderProgram);

    for (auto &buffer : mBufferLocations)
    {
        glBindTexture(GL_TEXTURE_2D, buffer.mTexture);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.mIbo);
        glBindBuffer(GL_ARRAY_BUFFER, buffer.mVbo);

        GLuint positionIdx = 0;
        glVertexAttribPointer(positionIdx, 3, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, vertex));
        glEnableVertexAttribArray(positionIdx);

        GLuint texCoordIdx = 1;
        glVertexAttribPointer(texCoordIdx, 2, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, textureCoord));
        glEnableVertexAttribArray(texCoordIdx);

        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(buffer.mIndicesSize), GL_UNSIGNED_SHORT, (GLvoid *)0);
    }
}

void Shader::setMvpMatrix(glm::mat4 mvpMat)
{
    glUseProgram(mShaderProgram);
    mMvpMat = mvpMat;
    glUniformMatrix4fv(mMvpMatrixLoc, 1, GL_FALSE, glm::value_ptr(mMvpMat));
}

void Shader::setTriangles(const std::vector<Triangles> &triangles)
{
    glUseProgram(mShaderProgram);
    std::cout << "set triangles:" << triangles.size() << std::endl;
    for (auto object : triangles)
    {
        GLuint texture = texLoad(object.material);
        if (!texture)
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Couldn't load texture.", NULL);
            return;
        }

        glBindBuffer(GL_ARRAY_BUFFER, 0);

        GLuint ibo = iboCreate(object.indices);
        if (!ibo)
        {
            SDL_Log("ERROR: iboCreate\n");
            return;
        }

        GLuint vbo = vboCreate(object.vertex);
        if (!vbo)
        {
            SDL_Log("ERROR: vboCreate\n");
            return;
        }

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

        GLuint positionIdx = 0;
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(positionIdx, 3, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, vertex));
        glEnableVertexAttribArray(positionIdx);

        GLuint texCoordIdx = 1;
        glVertexAttribPointer(texCoordIdx, 2, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, textureCoord));
        glEnableVertexAttribArray(texCoordIdx);

        mBufferLocations.push_back({ibo, vbo, texture, object.indices.size()});
        std::cout << "setTriangles: ibo:" << ibo << " vbo:" << vbo << " size:" << object.indices.size() << std::endl;
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, mBufferLocations.front().mTexture);
    GLint texSamplerUniformLoc = glGetUniformLocation(mShaderProgram, "texSampler");
    if (texSamplerUniformLoc < 0)
    {
        SDL_Log("ERROR: Couldn't get texSampler's location.");
        return;
    }
    glUniform1i(texSamplerUniformLoc, 0);
}

void Shader::setTexture(const std::string &name, SDL_Surface *surface)
{
    glUseProgram(mShaderProgram);
    auto it = mTextureCache.find(name);
    if (it != mTextureCache.end())
    {
        glDeleteBuffers(1, &it->second);
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, surface->w, surface->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, surface->pixels);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        glDeleteBuffers(1, &texture);
        texture = 0;
        SDL_FreeSurface(surface);
        SDL_Log("Creating texture %s failed, code %u\n", name.c_str(), err);
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    SDL_FreeSurface(surface);

    mTextureCache[name] = texture;
}

void Shader::setColor(const std::string &name, uint32_t rgba)
{
    SDL_Surface *color = SDL_CreateRGBSurface(0, 1, 1, 32, 0, 0, 0, 0);
    ((uint8_t*)color->pixels)[0] = (rgba >> 24) & 0xFF;
    ((uint8_t*)color->pixels)[1] = (rgba >> 16) & 0xFF;
    ((uint8_t*)color->pixels)[2] = (rgba >> 8) & 0xFF;
    ((uint8_t*)color->pixels)[3] = rgba & 0xFF;
    setTexture(name, color);
}

GLuint Shader::texLoad(const std::string &filename)
{
    auto it = mTextureCache.find(filename);
    if (it != mTextureCache.end())
    {
        return it->second;
    }

    int flags = IMG_INIT_JPG | IMG_INIT_PNG;
    if ((IMG_Init(flags) & flags) == 0)
    {
        SDL_Log("ERROR: Texture loading failed. Couldn't get JPEG and PNG loaders. \n");
        return 0;
    }

    SDL_Surface *texSurf = IMG_Load(filename.c_str());
    if (!texSurf)
    {
        SDL_Log("Loading image %s failed with error: %s", filename.c_str(), IMG_GetError());
        return 0;
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texSurf->w, texSurf->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, texSurf->pixels);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        glDeleteBuffers(1, &texture);
        texture = 0;
        SDL_FreeSurface(texSurf);
        SDL_Log("Creating texture %s failed, code %u\n", filename.c_str(), err);
        return 0;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    SDL_FreeSurface(texSurf);

    mTextureCache[filename] = texture;

    return texture;
}

void Shader::initializeShaderProgram()
{
    mShaderProgram = shaderProgLoad("../shader/vertex.gl", "../shader/fragment.gl");

    if (!mShaderProgram)
    {
        return;
    }
    glUseProgram(mShaderProgram);

    mMvpMatrixLoc = glGetUniformLocation(mShaderProgram, "mvpMatrix");
    if (mMvpMatrixLoc < 0)
    {
        SDL_Log("ERROR: Couldn't get mvpMatrix's location.");
        return;
    }
}

GLuint Shader::iboCreate(const std::vector<GLushort> &indices)
{
    GLuint ibo;
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLushort) * indices.size(), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        glDeleteBuffers(1, &ibo);
        SDL_Log("Creating IBO Failed, code %u\n", err);
        ibo = 0;
    }

    return ibo;
}

GLuint Shader::vboCreate(const std::vector<VertexTexture> &vertices)
{
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glBufferData(GL_ARRAY_BUFFER, sizeof(VertexTexture) * vertices.size(), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        glDeleteBuffers(1, &vbo);
        SDL_Log("Creating VBO failed, code %u\n", err);
        vbo = 0;
    }

    return vbo;
}

GLuint Shader::loadShader(const std::string &filename, GLenum shaderType)
{
    std::ifstream file(filename);
    if (!file)
        throw std::runtime_error("Failed to open file: " + filename);

    std::string shaderSource((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const char *shaderSourcePtr = shaderSource.c_str();

    GLuint shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, &shaderSourcePtr, nullptr);
    glCompileShader(shader);

    GLint compileSucceeded = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compileSucceeded);
    if (!compileSucceeded)
    {
        GLint logLength = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<GLchar> infoLog(logLength);
        glGetShaderInfoLog(shader, logLength, &logLength, infoLog.data());
        throw std::runtime_error("Compilation of shader " + filename + " failed: " + std::string(infoLog.begin(), infoLog.end()));
    }

    return shader;
}

GLuint Shader::shaderProgLoad(const std::string &vertFilename, const std::string &fragFilename)
{
    GLuint vertShader = loadShader(vertFilename, GL_VERTEX_SHADER);
    if (!vertShader)
    {
        SDL_Log("Couldn't load vertex shader: %s\n", vertFilename.c_str());
        return 0;
    }

    GLuint fragShader = loadShader(fragFilename, GL_FRAGMENT_SHADER);
    if (!fragShader)
    {
        SDL_Log("Couldn't load fragment shader: %s\n", fragFilename.c_str());
        glDeleteShader(vertShader);
        return 0;
    }

    GLuint shaderProg = glCreateProgram();
    if (shaderProg)
    {
        glAttachShader(shaderProg, vertShader);
        glAttachShader(shaderProg, fragShader);

        glLinkProgram(shaderProg);

        GLint linkingSucceeded = GL_FALSE;
        glGetProgramiv(shaderProg, GL_LINK_STATUS, &linkingSucceeded);
        if (!linkingSucceeded)
        {
            SDL_Log("Linking shader failed (vert. shader: %s, frag. shader: %s\n", vertFilename.c_str(), fragFilename.c_str());
            GLint logLength = 0;
            glGetProgramiv(shaderProg, GL_INFO_LOG_LENGTH, &logLength);
            GLchar *errLog = (GLchar *)malloc(logLength);
            if (errLog)
            {
                glGetProgramInfoLog(shaderProg, logLength, &logLength, errLog);
                SDL_Log("%s\n", errLog);
                free(errLog);
            }
            else
            {
                SDL_Log("Couldn't get shader link log; out of memory\n");
            }
            glDeleteProgram(shaderProg);
            shaderProg = 0;
        }
    }
    else
    {
        SDL_Log("Couldn't create shader program\n");
    }

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);

    return shaderProg;
}
