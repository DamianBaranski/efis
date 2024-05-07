cmake_minimum_required(VERSION 3.10)
project(efis)

if (POLICY CMP0072)
  cmake_policy (SET CMP0072 NEW)
endif(POLICY CMP0072)

# Set C++ standard and compiler flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall -Wextra")

find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(ZLIB REQUIRED)

include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${OPENGL_INCLUDE_DIRS})

# Source files
set(SRC 
    src/data_manager_stratux.cpp
    src/main.cpp
    src/screen.cpp
    src/shader.cpp
    src/render2d.cpp
    src/btg_file.cpp
    src/ahrs_widget.cpp
)

# Header files
set(INC
    src/data_manager_stratux.h
    src/data_type.h
    src/idata_manager.h
    src/iobserver.h
    src/iwidget.h
    src/screen.h
    src/subject.h
    src/shader.h
    src/terrain_widget.h
    src/render2d.h
    src/btg_file.h
    src/ahrs_widget.h
)

# Add executable
add_executable(efis ${SRC} ${INC})
target_link_libraries(efis ${OPENGL_LIBRARIES} ${CURL_LIBRARIES} ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES} ${SDL2_TTF_LIBRARIES} ${ZLIB_LIBRARIES} SDL2_image SDL2_ttf)

# Find Doxygen package
find_package(Doxygen)

if(DOXYGEN_FOUND)
    # Set input and output files
    set(DOXYGEN_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/src)
    set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/doc)

    # Configure doxyfile
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)

    # Add custom target to generate documentation
    add_custom_target(doxygen
        ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
endif()

