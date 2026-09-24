cmake_minimum_required(VERSION 3.20)
project(coastline C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()
# исходники в UTF-8 (строки интерфейса на русском)
add_compile_options($<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

# --- raylib 5.5: скачивается автоматически при первой сборке
include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
if(EMSCRIPTEN)
    set(PLATFORM "Web" CACHE STRING "" FORCE)
    set(OPENGL_VERSION "ES 3.0" CACHE STRING "" FORCE)
endif()
FetchContent_Declare(raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(raylib)

# --- шрифты встраиваются в исполняемый файл
set(ASSET_FILES
    assets/RussoOne-Regular.ttf
    assets/PT_Sans-Web-Regular.ttf
    assets/PT_Sans-Web-Bold.ttf)
set(EMBED_CPP ${CMAKE_BINARY_DIR}/embedded_assets.cpp)
# список файлов передаём через «|», чтобы оболочка не разбила его на аргументы
string(REPLACE ";" "|" ASSET_LIST "${ASSET_FILES}")
add_custom_command(
    OUTPUT ${EMBED_CPP}
    COMMAND ${CMAKE_COMMAND} -DOUT=${EMBED_CPP} -DSRC_DIR=${CMAKE_SOURCE_DIR} "-DFILES=${ASSET_LIST}" -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${ASSET_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Встраиваю шрифты"
    VERBATIM)

# --- симуляция: мир, физика, ИИ, события (без графики)
add_library(coastline_core STATIC
    src/core/noise.cpp
    src/world/world.cpp
    src/world/world_props.cpp
    src/sim/car_spec.cpp
    src/sim/vehicle.cpp
    src/sim/route.cpp
    src/sim/ai.cpp
    src/sim/events.cpp
    src/sim/skills.cpp
    src/sim/profile.cpp)
target_include_directories(coastline_core PUBLIC src)

# --- игра
add_executable(coastline
    src/main.cpp
    src/game/game.cpp
    src/game/game_ui.cpp
    src/game/commands.cpp
    src/render/shaders.cpp
    src/render/meshgen.cpp
    src/render/textures.cpp
    src/render/carmodel.cpp
    src/render/propmodels.cpp
    src/render/renderer.cpp
    src/render/camera.cpp
    src/render/effects.cpp
    src/ui/ui.cpp
    src/audio/audio.cpp
    ${EMBED_CPP})
target_link_libraries(coastline PRIVATE coastline_core raylib)

if(EMSCRIPTEN)
    set_target_properties(coastline PROPERTIES SUFFIX ".html")
    target_link_options(coastline PRIVATE
        -sUSE_GLFW=3 -sMAX_WEBGL_VERSION=2 -sMIN_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=2097152 -sINITIAL_MEMORY=134217728
        -sEXPORTED_FUNCTIONS=['_main','_cl_cmd','_malloc','_free'] -sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPF32']
        --shell-file ${CMAKE_SOURCE_DIR}/web/shell.html)
else()
    if(MSVC)
        # без консольного окна в релизе
        set_target_properties(coastline PROPERTIES WIN32_EXECUTABLE $<CONFIG:Release>)
        target_link_options(coastline PRIVATE $<$<CONFIG:Release>:/ENTRY:mainCRTStartup>)
    endif()
    # --- автотесты симуляции
    add_executable(coastline_tests tests/sim_tests.cpp)
    target_link_libraries(coastline_tests PRIVATE coastline_core)
    enable_testing()
    add_test(NAME sim COMMAND coastline_tests)
endif()
