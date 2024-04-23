/// @file btg_file.h
/// @brief Declaration of the BtgFile class for parsing BTG (Binary Terrain Grid) files.

#ifndef BTG_FILE_H
#define BTG_FILE_H

#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <iostream>
#include <zlib.h>
#include "data_type.h"

/// @brief The BtgFile class represents a parser for BTG (Binary Terrain Grid) files.
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
        INDIVIDUAL_TRIANGLES = 10,
        /// Triangles strips object.
        TRIANGLES_STRIPS = 11,
        /// Triangles fans object.
        TRIANGLES_FANS = 12,
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

    /// Represents a normal with x, y, and z components.
    typedef struct __attribute__((packed))
    {
        uint8_t x; ///< X component.
        uint8_t y; ///< Y component.
        uint8_t z; ///< Z component.
    } Normal;

    /// Represents a color with red, green, blue, and alpha components.
    typedef struct __attribute__((packed))
    {
        float red;   ///< Red component.
        float green; ///< Green component.
        float blue;  ///< Blue component.
        float alpha; ///< Alpha component.
    } Color;

    /// Represents the type used for indexing.
    typedef int IndexType;

    /// Represents an index pair for a vertex and texture coordinate.
    typedef struct
    {
        IndexType vertexIndex;       ///< Index of the vertex.
        IndexType textureCoordIndex; ///< Index of the texture coordinate.
    } VertexTextureIndex;

    /// Represents the header of a property object.
    typedef struct __attribute__((packed))
    {
        uint8_t propertyType; ///< Type of the property.
        unsigned int size;    ///< Size of the property in bytes.
    } PropertyHeader;

    /// @brief The base class for loading objects from compressed files.
    class BtgFileObject
    {
    protected:
        /// @brief Loads a generic object from a compressed file.
        /// @tparam T The type of the object.
        /// @param file The input compressed file stream.
        /// @param data The object to load.
        /// @return The size of the loaded object.
        template <typename T>
        size_t loadObject(gzFile &file, T &data);

        /// @brief Loads a generic object with specified size from a compressed file.
        /// @tparam T The type of the object.
        /// @param file The input compressed file stream.
        /// @param data The object to load.
        /// @param bytes The size of the object to load.
        /// @return The size of the loaded object.
        template <typename T>
        size_t loadObject(gzFile &file, T &data, size_t bytes);

        /// @brief Loads a generic object of different type from a compressed file.
        /// @tparam T1 The original type of the object in the file.
        /// @tparam T2 The target type of the object to load.
        /// @param file The input compressed file stream.
        /// @param data The target object to load.
        /// @return The size of the loaded object.
        template <typename T1, typename T2>
        size_t loadObject(gzFile &file, T2 &data);
    };

    /// @brief Represents a list of objects in a BTG file.
    ///
    /// @tparam T The type of objects stored in the list.
    template <typename T>
    class ObjectsList : private BtgFileObject
    {
    public:
        /// @brief Unserializes the list of objects data from a compressed file.
        /// @param file The input compressed file stream.
        /// @return True if the list of objects is successfully unserialized, false otherwise.
        bool unserialize(gzFile &file);

        /// @brief Gets the size of the list.
        /// @return The size of the list.
        size_t getSize() const;

        /// @brief Accesses an object in the list by index.
        /// @param index The index of the object to access.
        /// @return The object at the specified index.
        const T &operator[](size_t index) const;

    private:
        std::vector<T> mData; ///< Vector to store objects.
    };

    /// @brief Represents the header of the BTG file.
    class Header : private BtgFileObject
    {
    public:
        /// @brief Unserializes the header data from a compressed file.
        /// @param file The input compressed file stream.
        /// @return True if the header is successfully unserialized, false otherwise.
        bool unserialize(gzFile &file);

        /// @brief Gets the number of objects in the BTG file.
        /// @return The number of objects.
        unsigned int getNumberOfObjects() const;

        /// @brief Gets the version of the BTG file format.
        /// @return The version of the file format.
        unsigned short getVersion() const;

        /// @brief Gets the magic number for file identification.
        /// @return The magic number.
        unsigned short getMagicNumber() const;

        /// @brief Gets the creation time of the BTG file.
        /// @return The creation time of the file.
        unsigned int getCreationTime() const;

        /// @brief Gets the width of the data in the BTG file depend on file version.
        /// @return The data width.
        unsigned short getDataWidth() const;

        /// @brief Overloaded stream insertion operator to print Header information.
        /// @param os The output stream.
        /// @param header The Header object to print.
        /// @return The output stream.
        friend std::ostream &operator<<(std::ostream &os, const Header &header);

    private:
        static constexpr short cVersion7 = 7;     ///< Version 7 constant.
        static constexpr short cVersion10 = 10;   ///< Version 10 constant.
        static constexpr short cDataWidthV7 = 2;  ///< Data width for version 7.
        static constexpr short cDataWidthV10 = 4; ///< Data width for version 10.
        unsigned short mVersion;                  ///< Version of the file format.
        unsigned short mMagicNumber;              ///< Magic number for file identification.
        unsigned int mCreationTime;               ///< Creation time of the file.
        unsigned int mNumObjects;                 ///< Number of objects in the file.
    };

    /// @brief Represents the object header of a BTG file object.
    class ObjectHeader : private BtgFileObject
    {
    public:
        /// @brief Constructs an ObjectHeader object from a Header object.
        /// @param header The header object from which to initialize.
        ObjectHeader(const Header &header);

        /// @brief Unserializes the object header data from a compressed file.
        /// @param file The input compressed file stream.
        /// @return True if the object header is successfully unserialized, false otherwise.
        bool unserialize(gzFile &file);

        /// @brief Get the type of the object.
        /// @return The type of the object.
        ObjectType getObjectType() const;

        /// @brief Get the number of properties of the object.
        /// @return The number of properties.
        unsigned int getNumberOfProperties() const;

        /// @brief Get the number of elements in the object.
        /// @return The number of elements.
        unsigned int getNumberOfElements() const;

        /// @brief Overloaded stream insertion operator to print ObjectHeader information.
        /// @param os The output stream.
        /// @param objHeader The ObjectHeader object to print.
        /// @return The output stream.
        friend std::ostream &operator<<(std::ostream &os, const ObjectHeader &objHeader);

    private:
        const BtgFile::Header &mHeader;   ///< Reference to the BTG file header.
        ObjectType mObjectType;           ///< Type of the object.
        unsigned int mNoObjectProperties; ///< Number of properties of the object.
        unsigned int mNoObjectElements;   ///< Number of elements in the object.
    };

    /// @brief Represents a bounding sphere with a center point and radius.
    class BoundingSphere : private BtgFileObject
    {
    public:
        /// @brief Unserializes the bounding sphere data from a compressed file.
        /// @param file The input compressed file stream.
        /// @return True if the bounding sphere is successfully unserialized, false otherwise.
        bool unserialize(gzFile &file);

        /// @brief Gets the X coordinate of the center of the bounding sphere.
        /// @return The X coordinate of the center.
        double getCenterX() const;

        /// @brief Gets the Y coordinate of the center of the bounding sphere.
        /// @return The Y coordinate of the center.
        double getCenterY() const;

        /// @brief Gets the Z coordinate of the center of the bounding sphere.
        /// @return The Z coordinate of the center.
        double getCenterZ() const;

        /// @brief Gets the radius of the bounding sphere.
        /// @return The radius of the bounding sphere.
        float getRadius() const;

        /// @brief Overloaded stream insertion operator to print BoundingSphere information.
        /// @param os The output stream.
        /// @param sphere The BoundingSphere object to print.
        /// @return The output stream.
        friend std::ostream &operator<<(std::ostream &os, const BoundingSphere &sphere);

    private:
        double mCenterX; ///< X coordinate of the center of the bounding sphere.
        double mCenterY; ///< Y coordinate of the center of the bounding sphere.
        double mCenterZ; ///< Z coordinate of the center of the bounding sphere.
        float mRadius;   ///< Radius of the bounding sphere.
    };

    /// @brief Represents properties of a BTG file object.
    class Properties : private BtgFileObject
    {
    public:
        /// @brief Unserializes the properties data from a compressed file.
        /// @param objHeader The object header containing information about the properties.
        /// @param file The input compressed file stream.
        /// @return True if the properties are successfully unserialized, false otherwise.
        bool unserialize(const ObjectHeader &objHeader, gzFile &file);

        /// @brief Gets the index types of the properties.
        /// @return The index types.
        uint8_t getIndexTypes() const;

        /// @brief Gets the material associated with the properties.
        /// @return The material.
        const std::string &getMaterial() const;

        /// @brief Overloaded stream insertion operator to print Properties information.
        /// @param os The output stream.
        /// @param props The Properties object to print.
        /// @return The output stream.
        friend std::ostream &operator<<(std::ostream &os, const Properties &props);

    private:
        uint8_t mIndexTypes;   ///< Index types of the properties.
        std::string mMaterial; ///< Material associated with the properties.
    };

    /// @brief Represents individual triangles in a BTG file.
    class IndividualTriangles : private BtgFileObject
    {
    public:
        /// @brief Unserializes the individual triangles data from a compressed file.
        /// @param header The header containing general information about the file.
        /// @param objHeader The object header containing information about individual triangles.
        /// @param file The input compressed file stream.
        /// @return True if the individual triangles are successfully unserialized, false otherwise.
        bool unserialize(const Header &header, const ObjectHeader &objHeader, gzFile &file);

        /// @brief Gets the material associated with the individual triangles.
        /// @return The material.
        const std::string &getMaterial() const;

        /// @brief Gets the indexes of the individual triangles.
        /// @return The indexes.
        const std::vector<VertexTextureIndex> &getIndexes() const;

        /// @brief Overloaded stream insertion operator to print IndividualTriangles information.
        /// @param os The output stream.
        /// @param triangles The IndividualTriangles object to print.
        /// @return The output stream.
        friend std::ostream &operator<<(std::ostream &os, const IndividualTriangles &triangles);

    private:
        std::string mMaterial;                    ///< Material associated with the individual triangles.
        std::vector<VertexTextureIndex> mIndexes; ///< Indexes of the individual triangles.
    };

    /// Represents a vertex with x, y, and z coordinates.
    typedef struct __attribute__((packed))
    {
        float x; ///< X coordinate.
        float y; ///< Y coordinate.
        float z; ///< Z coordinate.
    } Vertex;

    /// Represents a texture coordinate with x and y components.
    typedef struct __attribute__((packed))
    {
        float x; ///< X component.
        float y; ///< Y component.
    } TextureCoordinate;

    /// @brief Loads data from a file.
    /// @param filename The name of the file to load.
    /// @return True if loading is successful, false otherwise.
    bool load(const std::string &filename);

    const BoundingSphere &getBoundingSphere() const;

    /// @brief Generates triangles from the loaded vertex data.
    /// @return A vector of triangles.
    std::vector<Triangles> generateTriangles();

    /// Friendship declaration for output stream operator for Vertex.
    friend std::ostream &operator<<(std::ostream &os, const Vertex &vertex);

    /// Friendship declaration for output stream operator for Normal.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::Normal &normal);

    /// Friendship declaration for output stream operator for Color.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::Color &color);

    /// Friendship declaration for output stream operator for PropertyHeader.
    friend std::ostream &operator<<(std::ostream &os, const BtgFile::PropertyHeader &propertyHeader);

private:
    ObjectsList<Vertex> mVertices;                                                      ///< Vector to store vertices.
    ObjectsList<TextureCoordinate> mTextureCoordinates;                                 ///< Vector to store texture coordinates.
    ObjectsList<Normal> mNormals;                                                       ///< Vector to store normals.
    ObjectsList<Color> mColors;                                                         ///< Vector to store colors.
    std::vector<std::pair<std::string, std::vector<VertexTextureIndex>>> mVerticesIdxs; ///< Vector to store vertex texture indices.
    BoundingSphere mBoundingSphere;
};

template <typename T>
bool BtgFile::ObjectsList<T>::unserialize(gzFile &file)
{
    unsigned int size;
    if (!loadObject(file, size))
    {
        return false;
    }

    mData.resize(size / sizeof(T));
    if (size == 0)
    {
        return true;
    }
    return loadObject(file, *mData.data(), size);
}

template <typename T>
size_t BtgFile::ObjectsList<T>::getSize() const
{
    return mData.size();
}

template <typename T>
const T &BtgFile::ObjectsList<T>::operator[](size_t index) const
{
    // Check if the index is valid
    if (index >= mData.size())
    {
        throw std::out_of_range("Index out of range");
    }
    return mData[index];
}

template <typename T>
size_t BtgFile::BtgFileObject::loadObject(gzFile &file, T &data)
{
    gzread(file, reinterpret_cast<char *>(&data), sizeof(T));
    return sizeof(T);
}

template <typename T>
size_t BtgFile::BtgFileObject::loadObject(gzFile &file, T &data, size_t bytes)
{
    data = T();
    gzread(file, reinterpret_cast<char *>(&data), bytes);
    return bytes;
}

template <typename T1, typename T2>
size_t BtgFile::BtgFileObject::loadObject(gzFile &file, T2 &data)
{
    T1 obj;
    size_t size = loadObject(file, obj);
    data = static_cast<T2>(obj);
    return size;
}

#endif
