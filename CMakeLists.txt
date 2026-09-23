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

set(EFIS_INCLUDE_DIRS
    src/app
    src/flight
    src/ahrs
    src/world
    src/terrain
    src/map
    src/overlays
    src/voice
    src/ui
    src/platform
)

set(SRC
    src/app/main.cpp
    src/app/screen.cpp
    src/app/frame.cpp
    src/app/app_controller.cpp
    src/app/menu_widget.cpp
    src/app/hud.cpp
    src/app/settings_popup.cpp
    src/app/stats_overlay.cpp
    src/app/sim_input.cpp
    src/app/session.cpp
    src/flight/data_manager_sim.cpp
    src/ahrs/ahrs_widget.cpp
    src/terrain/bucket.cpp
    src/terrain/bucket_container.cpp
    src/terrain/btg_file.cpp
    src/terrain/terrain_download.cpp
    src/terrain/airport_scenery.cpp
    src/map/openaip_client.cpp
    src/map/openaip_atlas.cpp
    src/map/sat_clipmap.cpp
    src/map/openaip_widget.cpp
    src/overlays/runway_overlay.cpp
    src/overlays/airspace_overlay.cpp
    src/overlays/vrp_overlay.cpp
    src/overlays/obstacle_overlay.cpp
    src/voice/voice_announcer.cpp
    src/voice/nav_voice.cpp
    src/ui/shader.cpp
    src/ui/render2d.cpp
    src/ui/europe_borders.cpp
    src/ui/europe_map.cpp
    src/platform/geo_coord_utils.cpp
    src/platform/asset_path.cpp
)

set(INC
    src/app/app_controller.h
    src/app/frame.h
    src/app/hud.h
    src/app/iwidget.h
    src/app/menu_widget.h
    src/app/screen.h
    src/app/sdl_compat.h
    src/app/settings_popup.h
    src/app/session.h
    src/app/sim_input.h
    src/app/stats_overlay.h
    src/flight/data_manager_sim.h
    src/flight/data_type.h
    src/flight/idata_manager.h
    src/flight/iobserver.h
    src/flight/subject.h
    src/ahrs/ahrs_widget.h
    src/world/iscene_layer.h
    src/world/iworld_read.h
    src/world/terrain_widget.h
    src/terrain/bucket.h
    src/terrain/bucket_container.h
    src/terrain/btg_file.h
    src/terrain/terrain_download.h
    src/terrain/airport_scenery.h
    src/map/iimagery.h
    src/map/openaip_client.h
    src/map/openaip_atlas.h
    src/map/sat_clipmap.h
    src/map/openaip_widget.h
    src/overlays/runway_overlay.h
    src/overlays/airspace_overlay.h
    src/overlays/vrp_overlay.h
    src/overlays/obstacle_overlay.h
    src/voice/voice_announcer.h
    src/voice/nav_voice.h
    src/ui/shader.h
    src/ui/render2d.h
    src/ui/popup.h
    src/ui/tab_window.h
    src/ui/europe_borders.h
    src/ui/europe_map.h
    src/platform/geo_coord_utils.h
    src/platform/asset_path.h
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
    target_include_directories(main PRIVATE ${EFIS_INCLUDE_DIRS})
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
    list(APPEND SRC src/flight/data_manager_stratux.cpp)
    list(APPEND INC src/flight/data_manager_stratux.h)

    include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS})
    include_directories(${CURL_INCLUDE_DIRS})
    include_directories(${OPENGL_INCLUDE_DIRS})

    add_executable(efis ${SRC} ${INC})
    target_include_directories(efis PRIVATE ${EFIS_INCLUDE_DIRS})
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
