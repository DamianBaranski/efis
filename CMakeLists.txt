cmake_minimum_required(VERSION 3.10)
project(efis)

# Set C++ standard and compiler flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "-Wall -Wextra")

find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(GLUT REQUIRED)

include_directories(${CURL_INCLUDE_DIRS})
include_directories(${OPENGL_INCLUDE_DIRS})
include_directories(${GLUT_INCLUDE_DIRS})

# Source files
set(SRC 
    src/attitude_widget.cpp
    src/data_manager_stratux.cpp
    src/main.cpp
    src/screen.cpp
)

# Header files
set(INC
    src/attitude_widget.h
    src/data_manager_stratux.h
    src/data_type.h
    src/idata_manager.h
    src/iobserver.h
    src/iwidget.h
    src/screen.h
    src/subject.h
)

# Add executable
add_executable(efis ${SRC} ${INC})
target_link_libraries(efis ${OPENGL_LIBRARIES} ${GLUT_LIBRARY} ${CURL_LIBRARIES} glut)

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

