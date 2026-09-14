cmake_minimum_required(VERSION 3.10)
project(efis)

if (POLICY CMP0072)
  cmake_policy (SET CMP0072 NEW)
endif(POLICY CMP0072)

# Set C++ standard and compiler flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

find_package(CURL REQUIRED)
find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(SDL2_image REQUIRED)
find_package(SDL2_ttf REQUIRED)
find_package(ZLIB REQUIRED)
find_package(glm REQUIRED)

# Vendored nlohmann_json — used by the Stratux data manager
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(nlohmann_json)

# Optional: install wgrib2 from the vendored tree (see README) then reconfigure
find_package(wgrib2 QUIET)

include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${OPENGL_INCLUDE_DIRS})

# Source files
set(SRC 
    src/data_manager_stratux.cpp
    src/data_manager_sim.cpp
    src/main.cpp
    src/screen.cpp
    src/shader.cpp
    src/render2d.cpp
    src/btg_file.cpp
    src/ahrs_widget.cpp
    src/bucket.cpp
    src/bucket_container.cpp
    src/geo_coord_utils.cpp
    src/openaip_client.cpp
    src/openaip_atlas.cpp
    src/openaip_widget.cpp
    src/runway_overlay.cpp
)

# Header files
set(INC
    src/data_manager_stratux.h
    src/data_manager_sim.h
    src/app_controller.h
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
    src/bucket.h
    src/bucket_container.h
    src/geo_coord_utils.h
    src/openaip_client.h
    src/openaip_atlas.h
    src/openaip_widget.h
    src/runway_overlay.h
)

# Add executable
add_executable(efis ${SRC} ${INC})
if(TARGET SDL2::SDL2)
    get_target_property(_sdl2_incs SDL2::SDL2 INTERFACE_INCLUDE_DIRECTORIES)
    foreach(_inc IN LISTS _sdl2_incs)
        if(_inc MATCHES "/SDL2$")
            get_filename_component(_sdl2_parent "${_inc}" DIRECTORY)
            target_include_directories(efis PRIVATE "${_sdl2_parent}")
        endif()
    endforeach()
endif()
set(_gles_hints)
foreach(_p ${CMAKE_PREFIX_PATH})
    list(APPEND _gles_hints "${_p}/include")
endforeach()
find_path(GLES3_INCLUDE_DIR GLES3/gl3.h HINTS ${_gles_hints} /usr/include)
if(GLES3_INCLUDE_DIR)
    target_include_directories(efis PRIVATE "${GLES3_INCLUDE_DIR}")
endif()
target_link_libraries(efis
    ${OPENGL_LIBRARIES}
    ${CURL_LIBRARIES}
    ${SDL2_LIBRARIES}
    ${SDL2_IMAGE_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    ${ZLIB_LIBRARIES}
    SDL2_image
    SDL2_ttf
    nlohmann_json::nlohmann_json
    glm::glm
)
if(wgrib2_FOUND)
    target_link_libraries(efis wgrib2::wgrib2_lib)
endif()

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

