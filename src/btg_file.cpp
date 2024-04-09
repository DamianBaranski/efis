#include "btg_file.h"

bool BtgFile::load(const std::string &filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open())
    {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }

    // Read the header
    Header heder;
    loadObject(file, heder);
    std::cout << heder << std::endl;

    // Read and process each object
    for (int i = 0; i < heder.numObjects; ++i)
    {
        ObjectHeader objHeader;
        loadObject(file, objHeader);
        std::cout << objHeader << std::endl;

        // Process each element based on its type
        switch (objHeader.objectType)
        {
        case ObjectType::BOUDING_SPHERE: // Bounding Sphere
        {
            for (int j = 0; j < objHeader.noObjectElement; ++j)
            {
                BoundingSphere sphere;
                loadObject(file, sphere);
                std::cout << sphere << std::endl;
            }
            break;
        }
        case ObjectType::VERTEX_LIST: // Vertex List
        {
            std::vector<Vertex> vertices;
            loadObjectList(file, vertices);
            for (size_t i = 0; i < vertices.size(); i++)
            {
                std::cout << i << ": " << vertices[i] << std::endl;
            }
            break;
        }

        case ObjectType::COLOR_LIST:
        {
            std::vector<Color> colors;
            loadObjectList(file, colors);
            for (size_t i = 0; i < colors.size(); i++)
            {
                std::cout << i << ": " << colors[i] << std::endl;
            }
            break;
        }

        case ObjectType::NORMAL_LIST:
        {
            std::vector<Normal> normals;
            loadObjectList(file, normals);
            for (size_t i = 0; i < normals.size(); i++)
            {
                std::cout << i << ": " << normals[i] << std::endl;
            }
            break;
        }

        case ObjectType::TEXTURE_COORDINATES_LIST:
        {
            std::vector<TextureCoordinate> textureCoordinate;
            loadObjectList(file, textureCoordinate);
            for (size_t i = 0; i < textureCoordinate.size(); i++)
            {
                std::cout << i << ": " << textureCoordinate[i] << std::endl;
            }
            break;
        }

        case ObjectType::INDIVIDUALS_TRIANGLES:
        {
            uint8_t indexTypes = 0;

            for (int propertyId = 0; propertyId < objHeader.noObjectProperties; propertyId++)
            {
                PropertyHeader propertyHeader;
                loadObject(file, propertyHeader);
                std::cout << propertyHeader << std::endl;
                std::vector<uint8_t> propertyData(propertyHeader.size);
                file.read(reinterpret_cast<char *>(propertyData.data()), propertyHeader.size);
                std::cout << "propertyData:" << propertyData.data() << std::endl;

                if (propertyHeader.propertyType == PropertyType::IndexTypes)
                {
                    indexTypes = propertyData[0];
                }
            }

            int size = 0;
            std::vector<uint16_t> index;
            for (size_t size = 0; size < objHeader.noObjectElement;)
            {

                if (indexTypes & PropertyIndexType::COLOR_INDEX)
                {
                }
                //std::cout << i << ": " << textureCoordinate[i] << std::endl;
            }
            break;
        }

        // Add cases for other object types as needed
        default:
            std::cerr << "Unsupported object type: " << static_cast<int>(objHeader.objectType) << std::endl;
            return false;
        }
    }

    return true;
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

std::ostream &operator<<(std::ostream &os, const BtgFile::TextureCoordinate &textureCoordinate)
{
    os << "Texture Coordinate: (" << textureCoordinate.x << ", " << textureCoordinate.y << ")";
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::Color &color)
{
    os << "Color: (" << color.red << ", " << color.green << ", " << color.blue << ", " << color.alpha << ")";
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::ListHeader &listHeader)
{
    os << "List Header - Size: " << listHeader.size << " bytes";
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::PropertyHeader &propertyHeader)
{
    os << "Property Header - Type: " << static_cast<int>(propertyHeader.propertyType) << ", Size: " << propertyHeader.size << " bytes";
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::Header &header)
{
    os << "Header - Version: " << header.version << ", Magic Number: " << header.magicNumber << ", Creation Time: " << header.creationTime << ", Number of Objects: " << header.numObjects;
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::ObjectHeader &objectHeader)
{
    os << "Object Header - Type: " << static_cast<int>(objectHeader.objectType) << ", Number of Properties: " << objectHeader.noObjectProperties << ", Number of Elements: " << objectHeader.noObjectElement;
    return os;
}

std::ostream &operator<<(std::ostream &os, const BtgFile::BoundingSphere &boundingSphere)
{
    os << "Bounding Sphere - Size: " << boundingSphere.size << " bytes, Center: (" << boundingSphere.centerX << ", " << boundingSphere.centerY << ", " << boundingSphere.centerZ << "), Radius: " << boundingSphere.radius;
    return os;
}

/*int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <filename.btg>" << std::endl;
        return 1;
    }
    BtgFile map;
    if (!map.load(argv[1]))
    {
        return 1;
    }

    return 0;
}*/
