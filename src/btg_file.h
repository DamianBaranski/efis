#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>

#if __DOXYGEN__
    #define PACKED
#else
    #define PACKED __attribute__((packed))
#endif

class BtgFile
{
public:
    /// Enumeration for different object types.
    enum ObjectType : uint8_t
    {
        /// Bounding sphere object.
        BOUDING_SPHERE = 0,
        /// Vertex list object.
        VERTEX_LIST = 1,
        /// Normal list object.
        NORMAL_LIST = 2,
        /// Texture coordinates list object.
        TEXTURE_COORDINATES_LIST = 3,
        /// Color list object.
        COLOR_LIST = 4,
        /// Points object.
        POINTS = 9,
        /// Individual triangles object.
        INDIVIDUALS_TRIANGLES = 10,
        /// Triangles strips object.
        TRIANGLES_STRIPS = 11,
        /// Triangles fans object.
        TRINAGLES_FANS = 12,
    };

    /// Enumeration for different property types.
    enum PropertyType : uint8_t
    {
        /// Material property type.
        Material = 0,
        /// Index types property type.
        IndexTypes = 1,
    };

    /// Enumeration for different property index types.
    enum PropertyIndexType : uint8_t
    {
        /// Vertex index property.
        VERTEX_INDEX = 1,
        /// Normal index property.
        NORMAL_INDEX = 2,
        /// Color index property.
        COLOR_INDEX = 4,
        /// Texture coordinate index property.
        TEXTURE_COORDINATE_INDEX = 8,
    };

    /// Represents a vertex with x, y, and z coordinates.
    typedef struct PACKED
    {
        float x; ///< X coordinate.
        float y; ///< Y coordinate.
        float z; ///< Z coordinate.
    } Vertex;

    /// Represents a normal with x, y, and z components.
    typedef struct PACKED
    {
        uint8_t x; ///< X component.
        uint8_t y; ///< Y component.
        uint8_t z; ///< Z component.
    } Normal;

    /// Represents a texture coordinate with x and y components.
    typedef struct PACKED
    {
        float x; ///< X component.
        float y; ///< Y component.
    } TextureCoordinate;

    /// Represents a color with red, green, blue, and alpha components.
    typedef struct PACKED
    {
        float red;   ///< Red component.
        float green; ///< Green component.
        float blue;  ///< Blue component.
        float alpha; ///< Alpha component.
    } Color;

    /// Represents the header of a list object.
    typedef struct PACKED
    {
        unsigned int size; ///< Size of the list in bytes.
    } ListHeader;

    /// Represents the header of a property object.
    typedef struct PACKED
    {
        uint8_t propertyType;   ///< Type of the property.
        unsigned int size;      ///< Size of the property in bytes.
    } PropertyHeader;

    /// Represents the header of the BTG file.
    typedef struct PACKED
    {
        unsigned short version;    ///< Version of the file format.
        unsigned short magicNumber;///< Magic number for file identification.
        unsigned int creationTime; ///< Creation time of the file.
        unsigned short numObjects; ///< Number of objects in the file.
    } Header;

    /// Represents the header of an object in the BTG file.
    typedef struct PACKED
    {
        ObjectType objectType;    ///< Type of the object.
        unsigned short noObjectProperties; ///< Number of properties of the object.
        unsigned short noObjectElement;    ///< Number of elements in the object.
    } ObjectHeader;

    /// Represents a bounding sphere with a center point and radius.
    typedef struct PACKED
    {
        unsigned int size;  ///< Size of the bounding sphere structure in bytes.
        double centerX;     ///< X coordinate of the center of the bounding sphere.
        double centerY;     ///< Y coordinate of the center of the bounding sphere.
        double centerZ;     ///< Z coordinate of the center of the bounding sphere.
        float radius;       ///< Radius of the bounding sphere.
    } BoundingSphere;

    /// Represents the type used for indexing.
    typedef uint16_t IndexType;

    /**
     * @brief Loads data from a file.
     * @param filename The name of the file to load.
     * @return True if loading is successful, false otherwise.
     */
    bool load(const std::string &filename);

    /// Friendship declaration for output stream operator for Vertex.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::Vertex &vertex);

    /// Friendship declaration for output stream operator for Normal.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::Normal &normal);

    /// Friendship declaration for output stream operator for TextureCoordinate.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::TextureCoordinate &textureCoordinate);

    /// Friendship declaration for output stream operator for Color.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::Color &color);

    /// Friendship declaration for output stream operator for ListHeader.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::ListHeader &listHeader);

    /// Friendship declaration for output stream operator for PropertyHeader.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::PropertyHeader &propertyHeader);

    /// Friendship declaration for output stream operator for Header.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::Header &header);

    /// Friendship declaration for output stream operator for ObjectHeader.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::ObjectHeader &objectHeader);

    /// Friendship declaration for output stream operator for BoundingSphere.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::BoundingSphere &boundingSphere);

private:

    /// @brief Loads an object from a file stream.
    /// @tparam T The type of the object.
    /// @param stream The input file stream.
    /// @param data The object to load.
    template <typename T>
    void loadObject(std::ifstream &stream, T &data);

    /// @brief Loads a list of objects from a file stream.
    /// @tparam T The type of objects in the list.
    /// @param stream The input file stream.
    /// @param data The vector to store loaded objects.
    template <typename T>
    void loadObjectList(std::ifstream &stream, std::vector<T> &data);
};

template <typename T>
void BtgFile::loadObject(std::ifstream &stream, T &data)
{
    stream.read(reinterpret_cast<char *>(&data), sizeof(T));
}

template <typename T>
void BtgFile::loadObjectList(std::ifstream &stream, std::vector<T> &data)
{
    ListHeader listHeader;
    loadObject(stream, listHeader);
    std::cout << "list size: " << listHeader.size << std::endl;
    data.resize(listHeader.size / sizeof(T));
    stream.read(reinterpret_cast<char *>(data.data()), listHeader.size);
}