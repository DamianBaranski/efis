#include "shader.h"
#include "asset_path.h"
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_opengles2.h>
#include <iostream>
#include <fstream>

std::unordered_map<std::string, Shader::TextureData> Shader::mTextureCache = {};
Shader::SatClipState Shader::sSat = {};

void Shader::setSatClip(bool active, GLuint fineTex, GLuint midTex, GLuint wideTex, int fineOriginX, int fineOriginY,
                        int midOriginX, int midOriginY, int wideOriginX, int wideOriginY, int fineZoom, int midZoom,
                        int wideZoom, int fineGrid, const uint32_t *fineMask, int midGrid, const uint32_t *midMask,
                        uint32_t wideMask0, uint32_t wideMask1, float camU, float camV)
{
    sSat.active = active;
    sSat.fineTex = fineTex;
    sSat.midTex = midTex;
    sSat.wideTex = wideTex;
    sSat.fineOriginX = fineOriginX;
    sSat.fineOriginY = fineOriginY;
    sSat.midOriginX = midOriginX;
    sSat.midOriginY = midOriginY;
    sSat.wideOriginX = wideOriginX;
    sSat.wideOriginY = wideOriginY;
    sSat.fineZoom = fineZoom;
    sSat.midZoom = midZoom;
    sSat.wideZoom = wideZoom;
    sSat.fineGrid = fineGrid > 0 ? fineGrid : 8;
    sSat.midGrid = midGrid > 0 ? midGrid : 8;
    for (int i = 0; i < 8; ++i)
    {
        sSat.fineMask[i] = fineMask != nullptr ? fineMask[i] : 0u;
        sSat.midMask[i] = midMask != nullptr ? midMask[i] : 0u;
    }
    sSat.wideMask0 = wideMask0;
    sSat.wideMask1 = wideMask1;
    sSat.camU = camU;
    sSat.camV = camV;
}

size_t Shader::textureCacheBytes()
{
    size_t bytes = 0;
    for (const auto &entry : mTextureCache)
    {
        if (entry.second.mWidth > 0 && entry.second.mHeight > 0)
        {
            bytes += static_cast<size_t>(entry.second.mWidth) * static_cast<size_t>(entry.second.mHeight) * 4u;
        }
    }
    return bytes;
}

namespace
{
GLuint gSharedProgram = 0;
GLint gMvpMatrixLoc = -1;
GLint gUseOpenAipLoc = -1;
GLint gSatFineLoc = -1;
GLint gSatMidLoc = -1;
GLint gSatWideLoc = -1;
GLint gSatFineOriginLoc = -1;
GLint gSatMidOriginLoc = -1;
GLint gSatWideOriginLoc = -1;
GLint gSatFineMaskLoc = -1;
GLint gSatMidMaskLoc = -1;
GLint gSatWideMaskLoc = -1;
GLint gSatFineZoomLoc = -1;
GLint gSatMidZoomLoc = -1;
GLint gSatWideZoomLoc = -1;
GLint gSatFineGridLoc = -1;
GLint gSatFineBitsLoc = -1;
GLint gSatMidGridLoc = -1;
GLint gSatMidBitsLoc = -1;
GLint gSatCamUvLoc = -1;
GLint gColorScaleLoc = -1;
}

Shader::Shader() : mShaderProgram(0)
{
    initializeShaderProgram();
}

Shader::~Shader()
{
    for (auto &buffer : mBufferLocations)
    {
        glDeleteBuffers(1, &buffer.mIbo);
        glDeleteBuffers(1, &buffer.mVbo);
    }
}

void Shader::render() const
{
    glUseProgram(mShaderProgram);

    if (mColorScaleLoc >= 0)
    {
        glUniform3fv(mColorScaleLoc, 1, glm::value_ptr(mColorScale));
    }

    const bool overlay = mOpenAipOverlay && sSat.active && (sSat.midTex != 0 || sSat.fineTex != 0);
    if (mUseOpenAipLoc >= 0)
    {
        glUniform1i(mUseOpenAipLoc, overlay ? 1 : 0);
    }
    if (overlay)
    {
        if (mSatFineLoc >= 0)
        {
            glUniform1i(mSatFineLoc, 1);
        }
        if (mSatMidLoc >= 0)
        {
            glUniform1i(mSatMidLoc, 2);
        }
        if (mSatWideLoc >= 0)
        {
            glUniform1i(mSatWideLoc, 3);
        }
        if (mSatFineOriginLoc >= 0)
        {
            glUniform2i(mSatFineOriginLoc, sSat.fineOriginX, sSat.fineOriginY);
        }
        if (mSatMidOriginLoc >= 0)
        {
            glUniform2i(mSatMidOriginLoc, sSat.midOriginX, sSat.midOriginY);
        }
        if (mSatWideOriginLoc >= 0)
        {
            glUniform2i(mSatWideOriginLoc, sSat.wideOriginX, sSat.wideOriginY);
        }
        if (mSatFineGridLoc >= 0)
        {
            glUniform1i(mSatFineGridLoc, sSat.fineGrid);
        }
        if (mSatFineBitsLoc >= 0)
        {
            glUniform1uiv(mSatFineBitsLoc, 8, sSat.fineMask);
        }
        if (mSatMidGridLoc >= 0)
        {
            glUniform1i(mSatMidGridLoc, sSat.midGrid);
        }
        if (mSatMidBitsLoc >= 0)
        {
            glUniform1uiv(mSatMidBitsLoc, 8, sSat.midMask);
        }
        if (mSatWideMaskLoc >= 0)
        {
            glUniform2ui(mSatWideMaskLoc, sSat.wideMask0, sSat.wideMask1);
        }
        if (mSatFineZoomLoc >= 0)
        {
            glUniform1i(mSatFineZoomLoc, sSat.fineZoom);
        }
        if (mSatMidZoomLoc >= 0)
        {
            glUniform1i(mSatMidZoomLoc, sSat.midZoom);
        }
        if (mSatWideZoomLoc >= 0)
        {
            glUniform1i(mSatWideZoomLoc, sSat.wideZoom);
        }
        if (mSatCamUvLoc >= 0)
        {
            glUniform2f(mSatCamUvLoc, sSat.camU, sSat.camV);
        }
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, sSat.fineTex);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, sSat.midTex);
        if (sSat.wideTex != 0)
        {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D_ARRAY, sSat.wideTex);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    for (auto &buffer : mBufferLocations)
    {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, buffer.mTexture);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer.mIbo);
        glBindBuffer(GL_ARRAY_BUFFER, buffer.mVbo);

        GLuint positionIdx = 0;
        glVertexAttribPointer(positionIdx, 3, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, vertex));
        glEnableVertexAttribArray(positionIdx);

        GLuint texCoordIdx = 1;
        glVertexAttribPointer(texCoordIdx, 2, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, textureCoord));
        glEnableVertexAttribArray(texCoordIdx);

        GLuint geoCoordIdx = 2;
        glVertexAttribPointer(geoCoordIdx, 2, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, geoCoord));
        glEnableVertexAttribArray(geoCoordIdx);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(buffer.mIndicesSize), GL_UNSIGNED_INT, (GLvoid *)0);
    }
}

void Shader::setMvpMatrix(glm::mat4 mvpMat)
{
    glUseProgram(mShaderProgram);
    mMvpMat = mvpMat;
    glUniformMatrix4fv(mMvpMatrixLoc, 1, GL_FALSE, glm::value_ptr(mMvpMat));
}

void Shader::clearGeometry()
{
    for (auto &buffer : mBufferLocations)
    {
        glDeleteBuffers(1, &buffer.mIbo);
        glDeleteBuffers(1, &buffer.mVbo);
    }
    mBufferLocations.clear();
}

void Shader::setTriangles(const std::vector<Triangles> &triangles)
{
    if (triangles.size() == 0)
    {
        return;
    }
    glUseProgram(mShaderProgram);
    for (auto object : triangles)
    {
        GLuint texture = texLoad(object.material);
        if (!texture)
        {
            texture = texLoad(AssetPath::resolve("resources/textures/unknown.png"));
            if (!texture)
            {
                return;
            }
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

        GLuint geoCoordIdx = 2;
        glVertexAttribPointer(geoCoordIdx, 2, GL_FLOAT, GL_FALSE, sizeof(VertexTexture), (const GLvoid *)offsetof(VertexTexture, geoCoord));
        glEnableVertexAttribArray(geoCoordIdx);

        mBufferLocations.push_back({ibo, vbo, texture, object.indices.size()});
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
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (!rgba)
    {
        SDL_Log("Converting texture %s to RGBA failed: %s", name.c_str(), SDL_GetError());
        return;
    }

    TextureData textureData;
    textureData.mHeight = rgba->h;
    textureData.mWidth = rgba->w;
    auto it = mTextureCache.find(name);
    if (it != mTextureCache.end() && it->second.mTbo != 0)
    {
        textureData.mTbo = it->second.mTbo;
    }
    else
    {
        glGenTextures(1, &textureData.mTbo);
    }
    glBindTexture(GL_TEXTURE_2D, textureData.mTbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int bpp = rgba->format ? rgba->format->BytesPerPixel : 4;
    glPixelStorei(GL_UNPACK_ROW_LENGTH, bpp > 0 ? rgba->pitch / bpp : 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureData.mWidth, textureData.mHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba->pixels);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        SDL_FreeSurface(rgba);
        SDL_Log("Creating texture %s failed, code %u\n", name.c_str(), err);
        return;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    SDL_FreeSurface(rgba);
    mTextureCache[name] = textureData;
}

int Shader::getTextureWidth(const std::string &name)
{
    texLoad(name);
    auto it = mTextureCache.find(name);
    if (it != mTextureCache.end())
    {
        return it->second.mWidth;
    }
    else
    {
        return -1;
    }
}

int Shader::getTextureHeight(const std::string &name)
{
    texLoad(name);
    auto it = mTextureCache.find(name);
    if (it != mTextureCache.end())
    {
        return it->second.mHeight;
    }
    else
    {
        return -1;
    }
}

void Shader::setColor(const std::string &name, uint32_t rgba)
{
    SDL_Surface *color = SDL_CreateRGBSurfaceWithFormat(0, 1, 1, 32, SDL_PIXELFORMAT_RGBA32);
    if (!color)
    {
        SDL_Log("setColor failed: %s", SDL_GetError());
        return;
    }
    const Uint8 r = static_cast<Uint8>((rgba >> 24) & 0xFF);
    const Uint8 g = static_cast<Uint8>((rgba >> 16) & 0xFF);
    const Uint8 b = static_cast<Uint8>((rgba >> 8) & 0xFF);
    const Uint8 a = static_cast<Uint8>(rgba & 0xFF);
    *static_cast<Uint32 *>(color->pixels) = SDL_MapRGBA(color->format, r, g, b, a);
    setTexture(name, color);
}

void Shader::setColorScale(float r, float g, float b)
{
    mColorScale = glm::vec3(r, g, b);
}

GLuint Shader::texLoad(const std::string &filename)
{
    auto it = mTextureCache.find(filename);
    if (it != mTextureCache.end())
    {
        return it->second.mTbo;
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

    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(texSurf, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(texSurf);
    if (!rgba)
    {
        SDL_Log("Converting image %s to RGBA failed: %s", filename.c_str(), SDL_GetError());
        return 0;
    }

    TextureData textureData;
    textureData.mHeight = rgba->h;
    textureData.mWidth = rgba->w;
    glGenTextures(1, &textureData.mTbo);
    glBindTexture(GL_TEXTURE_2D, textureData.mTbo);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
    {
        glDeleteTextures(1, &textureData.mTbo);
        textureData.mTbo = 0;
        SDL_FreeSurface(rgba);
        SDL_Log("Creating texture %s failed, code %u\n", filename.c_str(), err);
        return 0;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    SDL_FreeSurface(rgba);

    mTextureCache[filename] = textureData;

    return textureData.mTbo;
}

void Shader::initializeShaderProgram()
{
    if (!gSharedProgram)
    {
        gSharedProgram = shaderProgLoad(AssetPath::resolve("shader/vertex.gl"), AssetPath::resolve("shader/fragment.gl"));
        if (!gSharedProgram)
        {
            return;
        }
        glUseProgram(gSharedProgram);

        gMvpMatrixLoc = glGetUniformLocation(gSharedProgram, "mvpMatrix");
        if (gMvpMatrixLoc < 0)
        {
            SDL_Log("ERROR: Couldn't get mvpMatrix's location.");
        }

        gUseOpenAipLoc = glGetUniformLocation(gSharedProgram, "useOpenAip");
        gSatFineLoc = glGetUniformLocation(gSharedProgram, "satFine");
        gSatMidLoc = glGetUniformLocation(gSharedProgram, "satMid");
        gSatWideLoc = glGetUniformLocation(gSharedProgram, "satWide");
        gSatFineOriginLoc = glGetUniformLocation(gSharedProgram, "satFineOrigin");
        gSatMidOriginLoc = glGetUniformLocation(gSharedProgram, "satMidOrigin");
        gSatWideOriginLoc = glGetUniformLocation(gSharedProgram, "satWideOrigin");
        gSatFineMaskLoc = glGetUniformLocation(gSharedProgram, "satFineMask");
        gSatMidMaskLoc = glGetUniformLocation(gSharedProgram, "satMidMask");
        gSatWideMaskLoc = glGetUniformLocation(gSharedProgram, "satWideMask");
        gSatFineZoomLoc = glGetUniformLocation(gSharedProgram, "satFineZoom");
        gSatMidZoomLoc = glGetUniformLocation(gSharedProgram, "satMidZoom");
        gSatWideZoomLoc = glGetUniformLocation(gSharedProgram, "satWideZoom");
        gSatFineGridLoc = glGetUniformLocation(gSharedProgram, "satFineGrid");
        gSatFineBitsLoc = glGetUniformLocation(gSharedProgram, "satFineBits");
        gSatMidGridLoc = glGetUniformLocation(gSharedProgram, "satMidGrid");
        gSatMidBitsLoc = glGetUniformLocation(gSharedProgram, "satMidBits");
        gSatCamUvLoc = glGetUniformLocation(gSharedProgram, "satCamUv");
        gColorScaleLoc = glGetUniformLocation(gSharedProgram, "colorScale");
        if (gSatFineLoc >= 0)
        {
            glUniform1i(gSatFineLoc, 1);
        }
        if (gSatMidLoc >= 0)
        {
            glUniform1i(gSatMidLoc, 2);
        }
        if (gSatWideLoc >= 0)
        {
            glUniform1i(gSatWideLoc, 3);
        }
        GLint texSamplerUniformLoc = glGetUniformLocation(gSharedProgram, "texSampler");
        if (texSamplerUniformLoc >= 0)
        {
            glUniform1i(texSamplerUniformLoc, 0);
        }
        if (gUseOpenAipLoc >= 0)
        {
            glUniform1i(gUseOpenAipLoc, 0);
        }
        if (gColorScaleLoc >= 0)
        {
            const float one[3] = {1.0f, 1.0f, 1.0f};
            glUniform3fv(gColorScaleLoc, 1, one);
        }
    }

    mShaderProgram = gSharedProgram;
    mMvpMatrixLoc = gMvpMatrixLoc;
    mUseOpenAipLoc = gUseOpenAipLoc;
    mSatFineLoc = gSatFineLoc;
    mSatMidLoc = gSatMidLoc;
    mSatWideLoc = gSatWideLoc;
    mSatFineOriginLoc = gSatFineOriginLoc;
    mSatMidOriginLoc = gSatMidOriginLoc;
    mSatWideOriginLoc = gSatWideOriginLoc;
    mSatFineMaskLoc = gSatFineMaskLoc;
    mSatMidMaskLoc = gSatMidMaskLoc;
    mSatWideMaskLoc = gSatWideMaskLoc;
    mSatFineZoomLoc = gSatFineZoomLoc;
    mSatMidZoomLoc = gSatMidZoomLoc;
    mSatWideZoomLoc = gSatWideZoomLoc;
    mSatFineGridLoc = gSatFineGridLoc;
    mSatFineBitsLoc = gSatFineBitsLoc;
    mSatMidGridLoc = gSatMidGridLoc;
    mSatMidBitsLoc = gSatMidBitsLoc;
    mSatCamUvLoc = gSatCamUvLoc;
    mColorScaleLoc = gColorScaleLoc;
}

GLuint Shader::iboCreate(const std::vector<GLuint> &indices)
{
    GLuint ibo;
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);

    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * indices.size(), indices.data(), GL_STATIC_DRAW);
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
