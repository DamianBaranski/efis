#include "btg_file.h"
#include "geo_coord_utils.h"

bool BtgFile::load(const std::string &filename)
{
    gzFile file = gzopen(filename.c_str(), "r");
    if (!file)
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    // Read the header
    Header header;
    header.unserialize(file);
    std::cout << header << std::endl;
    ObjectHeader objHeader(header);

    // Read and process each object
    for (size_t i = 0; i < header.getNumberOfObjects(); ++i)
    {
        objHeader.unserialize(file);

        // Process each element based on its type
        switch (objHeader.getObjectType())
        {
        case ObjectType::BOUDING_SPHERE: // Bounding Sphere
        {
            for (size_t j = 0; j < objHeader.getNumberOfElements(); ++j)
            {
                mBoundingSphere.unserialize(file);
            }
            break;
        }
        case ObjectType::VERTEX_LIST: // Vertex List
        {
            mVertices.unserialize(file);
            break;
        }

        case ObjectType::COLOR_LIST:
        {
            std::vector<Color> colors;
            mColors.unserialize(file);
            break;
        }

        case ObjectType::NORMAL_LIST:
        {
            mNormals.unserialize(file);
            break;
        }

        case ObjectType::TEXTURE_COORDINATES_LIST:
        {
            std::vector<TextureCoordinate> textureCoordinate;
            mTextureCoordinates.unserialize(file);
            break;
        }

        case ObjectType::INDIVIDUAL_TRIANGLES:
        case ObjectType::TRIANGLES_STRIPS:
        case ObjectType::TRIANGLES_FANS:
        {
            IndividualTriangles triangles;
            triangles.unserialize(header, objHeader, file, objHeader.getObjectType());
            auto tmp = triangles.getIndexes();
            if (objHeader.getObjectType() == ObjectType::INDIVIDUAL_TRIANGLES)
            {
                for (int q = static_cast<int>(tmp.size()) - 3; q >= 0; q -= 3)
                {
                    if (tmp[static_cast<size_t>(q)].vertexIndex == tmp[static_cast<size_t>(q) + 1].vertexIndex ||
                        tmp[static_cast<size_t>(q)].vertexIndex == tmp[static_cast<size_t>(q) + 2].vertexIndex ||
                        tmp[static_cast<size_t>(q) + 1].vertexIndex == tmp[static_cast<size_t>(q) + 2].vertexIndex)
                    {
                        tmp.erase(tmp.begin() + q, tmp.begin() + q + 3);
                    }
                }
            }
            if (!tmp.empty())
            {
                mVerticesIdxs.push_back(std::make_pair(triangles.getMaterial(), tmp));
            }
            break;
        }
        case ObjectType::POINTS:
        default:
        {
            IndividualTriangles skip;
            skip.discard(objHeader, file);
            break;
        }
        }
    }

    return true;
}

const BtgFile::BoundingSphere &BtgFile::getBoundingSphere() const {
    return mBoundingSphere;
}

std::vector<Triangles> BtgFile::generateTriangles()
{
    std::cout << "Bounding sphere:" << std::fixed << mBoundingSphere << std::endl;
    std::cout << "generating triangles" << std::endl;
    std::cout << "mVertices size:" << mVertices.getSize() << std::endl;
    std::cout << "mTextureCoordinates size:" << mTextureCoordinates.getSize() << std::endl;

    const double cx = mBoundingSphere.getCenterX();
    const double cy = mBoundingSphere.getCenterY();
    const double cz = mBoundingSphere.getCenterZ();

    std::vector<Triangles> triangles;
    Triangles triangle;
    for (auto obj : mVerticesIdxs)
    {
        triangle.material = std::string("../resources/textures/btg/") + obj.first + std::string(".png");
        for (size_t i = 0; i < obj.second.size(); i++)
        {
            if(static_cast<size_t>(obj.second[i].vertexIndex)>=mVertices.getSize() ||
               static_cast<size_t>(obj.second[i].textureCoordIndex)>=mTextureCoordinates.getSize()) {
                std::cout << "Damage btg file" << std::endl;
                return triangles;
            }
            const auto &vert = mVertices[obj.second[i].vertexIndex];
            const auto &uv = mTextureCoordinates[obj.second[i].textureCoordIndex];
            const auto ll = GeoCoordUtils::convertXYZToLatLon(cx + vert.x, cy + vert.y, cz + vert.z);
            float geoU = 0.0f;
            float geoV = 0.0f;
            GeoCoordUtils::latLonToMercatorUv(ll.latitude, ll.longitude, geoU, geoV);
            VertexTexture vt{};
            vt.vertex.x = vert.x;
            vt.vertex.y = vert.y;
            vt.vertex.z = vert.z;
            vt.textureCoord.x = uv.x;
            vt.textureCoord.y = uv.y;
            vt.geoCoord.x = geoU;
            vt.geoCoord.y = geoV;
            triangle.vertex.push_back(vt);
            triangle.indices.push_back(i);
        }
        triangles.push_back(triangle);

        triangle = Triangles();
    }
    return triangles;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::Vertex &vertex)
{
    os << "Vertex: (" << vertex.x << ", " << vertex.y << ", " << vertex.z << ")";
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::Normal &normal)
{
    os << "Normal: (" << static_cast<int>(normal.x) << ", " << static_cast<int>(normal.y) << ", " << static_cast<int>(normal.z) << ")";
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::Color &color)
{
    os << "Color: (" << color.red << ", " << color.green << ", " << color.blue << ", " << color.alpha << ")";
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::PropertyHeader &propertyHeader)
{
    os << "Property Header - Type: " << static_cast<int>(propertyHeader.propertyType) << ", Size: " << propertyHeader.size << " bytes";
    return os;
}

bool BtgFile::Header::unserialize(gzFile &file)
{
    loadObject(file, mVersion);
    if (mVersion != 7 && mVersion != 10)
    {
        std::cout << "Unsupported btg file version: " << mVersion << std::endl;
        return false;
    }
    loadObject(file, mMagicNumber);
    loadObject(file, mCreationTime);
    if (mVersion == 7)
    {
        loadObject<unsigned short>(file, mNumObjects);
    }
    else
    {
        loadObject(file, mNumObjects);
    }
    return true;
}

unsigned int BtgFile::Header::getNumberOfObjects() const
{
    return mNumObjects;
}

unsigned short BtgFile::Header::getVersion() const
{
    return mVersion;
}

unsigned short BtgFile::Header::getMagicNumber() const
{
    return mMagicNumber;
}

unsigned int BtgFile::Header::getCreationTime() const
{
    return mCreationTime;
}

unsigned short BtgFile::Header::getDataWidth() const
{
    if (mVersion == cVersion7)
    {
        return cDataWidthV7;
    }
    else if (mVersion == cVersion10)
    {
        return cDataWidthV10;
    }
    else
    {
        std::cout << "Not supported BTG file version" << std::endl;
        return 0;
    }
}

std::ostream &operator<<(std::ostream &os, const BtgFile::Header &header)
{
    os << "Version: " << header.getVersion() << std::endl;
    os << "Magic Number: " << header.getMagicNumber() << std::endl;
    os << "Creation Time: " << header.getCreationTime() << std::endl;
    os << "Number of Objects: " << header.getNumberOfObjects();
    return os;
}

BtgFile::ObjectHeader::ObjectHeader(const BtgFile::Header &header) : mHeader(header)
{
}

bool BtgFile::ObjectHeader::unserialize(gzFile &file)
{
    unsigned short dataWidth = mHeader.getDataWidth();
    loadObject(file, mObjectType);
    loadObject(file, mNoObjectProperties, dataWidth);
    loadObject(file, mNoObjectElements, dataWidth);
    return true;
}

BtgFile::ObjectType BtgFile::ObjectHeader::getObjectType() const
{
    return mObjectType;
}

unsigned int BtgFile::ObjectHeader::getNumberOfProperties() const
{
    return mNoObjectProperties;
}

unsigned int BtgFile::ObjectHeader::getNumberOfElements() const
{
    return mNoObjectElements;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::ObjectHeader &objHeader)
{
    os << "Object Type: " << objHeader.mObjectType << std::endl;
    os << "Number of Properties: " << objHeader.mNoObjectProperties << std::endl;
    os << "Number of Elements: " << objHeader.mNoObjectElements;
    return os;
}

bool BtgFile::BoundingSphere::unserialize(gzFile &file)
{
    unsigned int size = 0;
    loadObject(file, size);
    if (size != sizeof(mCenterX) + sizeof(mCenterY) + sizeof(mCenterZ) + sizeof(mRadius))
    {
        std::cout << "Wrong BoundingSphere size" << std::endl;
        return false;
    }
    loadObject(file, mCenterX);
    loadObject(file, mCenterY);
    loadObject(file, mCenterZ);
    loadObject(file, mRadius);
    return true;
}

double BtgFile::BoundingSphere::getCenterX() const { return mCenterX; }
double BtgFile::BoundingSphere::getCenterY() const { return mCenterY; }
double BtgFile::BoundingSphere::getCenterZ() const { return mCenterZ; }
float BtgFile::BoundingSphere::getRadius() const { return mRadius; }

std::ostream &operator<<(std::ostream &os, const BtgFile::BoundingSphere &sphere)
{
    os << "Bounding Sphere:" << std::endl;
    os << "Center: (" << sphere.mCenterX << ", " << sphere.mCenterY << ", " << sphere.mCenterZ << ")" << std::endl;
    os << "Radius: " << sphere.mRadius;
    return os;
}

bool BtgFile::Properties::unserialize(const BtgFile::ObjectHeader &objHeader, gzFile &file)
{
    for (size_t propertyId = 0; propertyId < objHeader.getNumberOfProperties(); propertyId++)
    {
        PropertyHeader propertyHeader;
        loadObject(file, propertyHeader);
        std::vector<uint8_t> propertyData(propertyHeader.size);
        loadObject(file, propertyData[0], propertyHeader.size);

        if (propertyHeader.propertyType == PropertyType::IndexTypes)
        {
            mIndexTypes = propertyData[0];
        }
        else if (propertyHeader.propertyType == PropertyType::Material)
        {
            mMaterial.resize(propertyHeader.size);
            std::copy(propertyData.begin(), propertyData.end(), mMaterial.begin());
        }
    }
    return true;
}

uint8_t BtgFile::Properties::getIndexTypes() const { return mIndexTypes; }
const std::string &BtgFile::Properties::getMaterial() const { return mMaterial; }

std::ostream &operator<<(std::ostream &os, const BtgFile::Properties &props)
{
    os << "Properties:" << std::endl;
    os << "Index Types: " << static_cast<int>(props.mIndexTypes) << std::endl;
    os << "Material: " << props.mMaterial;
    return os;
}

bool BtgFile::IndividualTriangles::unserialize(const BtgFile::Header &header, const ObjectHeader &objHeader,
                                               gzFile &file, ObjectType type)
{
    size_t dataWidth = header.getDataWidth();
    Properties properties;
    if (!properties.unserialize(objHeader, file))
    {
        return false;
    }

    mMaterial = properties.getMaterial();
    VertexTextureIndex vertexTextureIndex;
    for (size_t size = 0; size < objHeader.getNumberOfElements(); size++)
    {
        unsigned int listSize = 0;
        loadObject(file, listSize);
        std::vector<VertexTextureIndex> primitive;
        for (size_t j = 0; j < listSize;)
        {
            unsigned int index;
            if (properties.getIndexTypes() & PropertyIndexType::VERTEX_INDEX)
            {
                j += loadObject(file, index, dataWidth);
                vertexTextureIndex.vertexIndex = index;
            }
            if (properties.getIndexTypes() & PropertyIndexType::NORMAL_INDEX)
            {
                j += loadObject(file, index, dataWidth);
            }
            if (properties.getIndexTypes() & PropertyIndexType::COLOR_INDEX)
            {
                j += loadObject(file, index, dataWidth);
            }
            if (properties.getIndexTypes() & PropertyIndexType::TEXTURE_COORDINATE_INDEX)
            {
                j += loadObject(file, index, dataWidth);
                vertexTextureIndex.textureCoordIndex = index;
                primitive.push_back(vertexTextureIndex);
            }
        }
        if (type == ObjectType::POINTS)
        {
            continue;
        }
        if (type == ObjectType::TRIANGLES_STRIPS)
        {
            for (size_t i = 2; i < primitive.size(); ++i)
            {
                if ((i & 1) != 0)
                {
                    mIndexes.push_back(primitive[i - 1]);
                    mIndexes.push_back(primitive[i - 2]);
                    mIndexes.push_back(primitive[i]);
                }
                else
                {
                    mIndexes.push_back(primitive[i - 2]);
                    mIndexes.push_back(primitive[i - 1]);
                    mIndexes.push_back(primitive[i]);
                }
            }
        }
        else if (type == ObjectType::TRIANGLES_FANS)
        {
            for (size_t i = 2; i < primitive.size(); ++i)
            {
                mIndexes.push_back(primitive[0]);
                mIndexes.push_back(primitive[i - 1]);
                mIndexes.push_back(primitive[i]);
            }
        }
        else
        {
            mIndexes.insert(mIndexes.end(), primitive.begin(), primitive.end());
        }
    }
    return true;
}

bool BtgFile::IndividualTriangles::discard(const ObjectHeader &objHeader, gzFile &file)
{
    Properties properties;
    if (!properties.unserialize(objHeader, file))
    {
        return false;
    }
    for (size_t i = 0; i < objHeader.getNumberOfElements(); ++i)
    {
        unsigned int listSize = 0;
        loadObject(file, listSize);
        if (listSize > 0)
        {
            std::vector<unsigned char> blob(listSize);
            loadObject(file, blob[0], listSize);
        }
    }
    return true;
}

const std::string &BtgFile::IndividualTriangles::getMaterial() const { return mMaterial; }
const std::vector<BtgFile::VertexTextureIndex> &BtgFile::IndividualTriangles::getIndexes() const { return mIndexes; }

std::ostream &operator<<(std::ostream &os, const BtgFile::IndividualTriangles &triangles)
{
    os << "Individual Triangles:" << std::endl;
    os << "Material: " << triangles.mMaterial << std::endl;
    os << "Indexes: ";
    for (const auto &index : triangles.mIndexes)
    {
        os << "(" << index.vertexIndex << ", " << index.textureCoordIndex << ") ";
    }
    os << std::endl;
    return os;
}
