cmake_minimum_required(VERSION 3.10)
project(efis)

if (POLICY CMP0072)
  cmake_policy (SET CMP0072 NEW)
endif(POLICY CMP0072)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
add_subdirectory(nlohmann_json)

set(SRC
    src/data_manager_sim.cpp
    src/main.cpp
    src/screen.cpp
    src/shader.cpp
    src/render2d.cpp
    src/btg_file.cpp
    src/ahrs_widget.cpp
    src/bucket.cpp
    src/bucket_container.cpp
    src/terrain_download.cpp
    src/europe_borders.cpp
    src/europe_map.cpp
    src/geo_coord_utils.cpp
    src/openaip_client.cpp
    src/openaip_atlas.cpp
    src/sat_clipmap.cpp
    src/openaip_widget.cpp
    src/runway_overlay.cpp
    src/airspace_overlay.cpp
    src/vrp_overlay.cpp
    src/voice_announcer.cpp
    src/nav_voice.cpp
    src/airport_scenery.cpp
    src/asset_path.cpp
)

set(INC
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
    src/popup.h
    src/tab_window.h
    src/btg_file.h
    src/ahrs_widget.h
    src/bucket.h
    src/bucket_container.h
    src/terrain_download.h
    src/europe_borders.h
    src/europe_map.h
    src/geo_coord_utils.h
    src/openaip_client.h
    src/openaip_atlas.h
    src/sat_clipmap.h
    src/openaip_widget.h
    src/runway_overlay.h
    src/airspace_overlay.h
    src/vrp_overlay.h
    src/voice_announcer.h
    src/nav_voice.h
    src/airport_scenery.h
    src/asset_path.h
    src/sdl_compat.h
)

if(ANDROID)
    add_compile_definitions(EFIS_ANDROID=1)
    set(EFIS_THIRD_PARTY "${CMAKE_SOURCE_DIR}/android/third_party")
    if(NOT EXISTS "${EFIS_THIRD_PARTY}/SDL2/CMakeLists.txt")
        message(FATAL_ERROR "Android third-party sources missing. Run android/setup.sh")
    endif()

    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory("${EFIS_THIRD_PARTY}/SDL2" SDL2)

    set(SDL2IMAGE_VENDORED ON CACHE BOOL "" FORCE)
    set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_DEPS_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_BACKEND_STB ON CACHE BOOL "" FORCE)
    set(SDL2IMAGE_AVIF OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_JXL OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_TIF OFF CACHE BOOL "" FORCE)
    set(SDL2IMAGE_WEBP OFF CACHE BOOL "" FORCE)
    add_subdirectory("${EFIS_THIRD_PARTY}/SDL2_image" SDL2_image)

    set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
    set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    add_subdirectory("${EFIS_THIRD_PARTY}/SDL2_ttf" SDL2_ttf)

    set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${EFIS_THIRD_PARTY}/glm" glm)

    add_library(main SHARED ${SRC} ${INC})
    target_link_libraries(main
        SDL2::SDL2
        SDL2_image::SDL2_image
        SDL2_ttf::SDL2_ttf
        nlohmann_json::nlohmann_json
        glm::glm
        GLESv3
        EGL
        android
        log
        z
    )
else()
    find_package(CURL REQUIRED)
    find_package(OpenGL REQUIRED)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_image REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(ZLIB REQUIRED)
    find_package(glm REQUIRED)
    find_package(wgrib2 QUIET)

    add_compile_definitions(EFIS_HAS_CURL=1)
    list(APPEND SRC src/data_manager_stratux.cpp)
    list(APPEND INC src/data_manager_stratux.h)

    include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})
    include_directories(${CURL_INCLUDE_DIRS})
    include_directories(${OPENGL_INCLUDE_DIRS})

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

    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        set(DOXYGEN_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/src)
        set(DOXYGEN_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/doc)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
        add_custom_target(doxygen
            ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM
        )
    endif()
endif()
