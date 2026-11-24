cmake_minimum_required(VERSION 3.28)
project(Kinetic VERSION 1.0.0 DESCRIPTION "2D physics sandbox" LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(KINETIC_WERROR "Treat warnings as errors (CI mode)" OFF)
option(KINETIC_FETCH_BOX2D "Download and build pinned Box2D v3.1.1 if the system package is missing" OFF)
option(KINETIC_BUILD_TESTS "Build unit and physics regression tests" ON)
option(KINETIC_ALLOC_COUNTER "Count heap allocations (glibc malloc interposer)" ON)

# QRhi is semi-public API (private include tree); see docs/DECISIONS.md.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.7 REQUIRED COMPONENTS Core Gui GuiPrivate Widgets ShaderTools Test)
qt_standard_project_setup()

# --- Box2D v3 -------------------------------------------------------------------------
# Arch's official repos ship box2d 3.1.1 (extra/box2d), which is what we use.
# KINETIC_FETCH_BOX2D only exists for machines without the package.
find_package(box2d 3.1 CONFIG QUIET)
if(NOT box2d_FOUND)
    if(KINETIC_FETCH_BOX2D)
        include(FetchContent)
        set(BOX2D_SAMPLES OFF CACHE BOOL "" FORCE)
        set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)
        set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
        set(BOX2D_VALIDATE OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(box2d
            GIT_REPOSITORY https://github.com/erincatto/box2d.git
            GIT_TAG 8c661469c9507d3ad6fbd2fea3f1aa71669c2fe3 # v3.1.1
            GIT_SHALLOW FALSE
            SYSTEM
            EXCLUDE_FROM_ALL) # static, linked in; not installed
        FetchContent_MakeAvailable(box2d)
        message(STATUS "Using fetched Box2D v3.1.1")
    else()
        message(FATAL_ERROR "Box2D >= 3.1 not found. Install it with 'sudo pacman -S box2d' "
                            "or configure with -DKINETIC_FETCH_BOX2D=ON.")
    endif()
else()
    message(STATUS "Using system Box2D ${box2d_VERSION}")
endif()

# --- warnings -----------------------------------------------------------------------------
add_library(kinetic_warnings INTERFACE)
target_compile_options(kinetic_warnings INTERFACE -Wall -Wextra -Wshadow=local -Wno-missing-field-initializers)
if(KINETIC_WERROR)
    target_compile_options(kinetic_warnings INTERFACE -Werror)
endif()

# --- core: model, physics, simulation thread (no widgets) ---------------------------------------
file(GLOB KINETIC_CORE_SOURCES CONFIGURE_DEPENDS src/core/*.cpp src/core/*.h)
file(GLOB KINETIC_ITEM_SOURCES CONFIGURE_DEPENDS src/items/*.cpp src/items/*.h)
add_library(kinetic_core STATIC ${KINETIC_CORE_SOURCES} ${KINETIC_ITEM_SOURCES})
target_include_directories(kinetic_core PUBLIC src)
target_link_libraries(kinetic_core PUBLIC Qt6::Core Qt6::Gui box2d::box2d PRIVATE kinetic_warnings)
if(KINETIC_ALLOC_COUNTER)
    target_compile_definitions(kinetic_core PRIVATE KINETIC_ALLOC_COUNTER)
endif()
set_target_properties(kinetic_core PROPERTIES AUTOMOC OFF)

# --- UI -----------------------------------------------------------------------------------------
file(GLOB KINETIC_APP_SOURCES CONFIGURE_DEPENDS src/app/*.cpp src/app/*.h)
list(FILTER KINETIC_APP_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")
add_library(kinetic_ui STATIC ${KINETIC_APP_SOURCES})
# QRhi headers (rhi/qrhi.h) ship in the GuiPrivate include tree in Qt >= 6.10.
target_link_libraries(kinetic_ui PUBLIC kinetic_core Qt6::Widgets Qt6::Gui Qt6::GuiPrivate PRIVATE kinetic_warnings)

qt_add_executable(kinetic src/app/main.cpp)
target_link_libraries(kinetic PRIVATE kinetic_ui kinetic_warnings)
set_target_properties(kinetic PROPERTIES OUTPUT_NAME kinetic)
target_compile_definitions(kinetic PRIVATE KINETIC_VERSION="${PROJECT_VERSION}")

qt_add_shaders(kinetic "kinetic_shaders"
    PREFIX "/shaders"
    BASE src/shaders
    FILES
        src/shaders/segment.vert src/shaders/segment.frag
        src/shaders/disc.vert src/shaders/disc.frag
        src/shaders/tri.vert src/shaders/tri.frag)

file(GLOB KINETIC_EXAMPLES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS resources/examples/*.json)
file(GLOB KINETIC_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS resources/icons/*.svg)
qt_add_resources(kinetic "kinetic_resources"
    PREFIX "/"
    BASE resources
    FILES ${KINETIC_EXAMPLES} ${KINETIC_ICONS} resources/io.github.qkyubit.Kinetic.svg)

# --- install ---------------------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS kinetic RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/io.github.qkyubit.Kinetic.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/io.github.qkyubit.Kinetic.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES resources/io.github.qkyubit.Kinetic.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

if(KINETIC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Documentation screenshot generator (not installed).
option(KINETIC_BUILD_TOOLS "Build developer tools (screenshot generator)" ON)
if(KINETIC_BUILD_TOOLS)
    qt_add_executable(kinetic-screenshots tools/screenshots.cpp)
    target_link_libraries(kinetic-screenshots PRIVATE kinetic_ui Qt6::Test kinetic_warnings)
    qt_add_shaders(kinetic-screenshots "screenshot_shaders"
        PREFIX "/shaders"
        BASE src/shaders
        FILES
            src/shaders/segment.vert src/shaders/segment.frag
            src/shaders/disc.vert src/shaders/disc.frag
            src/shaders/tri.vert src/shaders/tri.frag)
    qt_add_resources(kinetic-screenshots "screenshot_resources"
        PREFIX "/"
        BASE resources
        FILES ${KINETIC_EXAMPLES} ${KINETIC_ICONS} resources/io.github.qkyubit.Kinetic.svg)
endif()
if(KINETIC_BUILD_TOOLS)
    # Regenerates resources/examples/*.json from tools/make_examples.cpp.
    add_executable(kinetic-make-examples tools/make_examples.cpp)
    target_link_libraries(kinetic-make-examples PRIVATE kinetic_core kinetic_warnings)
endif()
