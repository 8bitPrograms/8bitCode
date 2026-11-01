cmake_minimum_required(VERSION 3.21)

project(8bitPad
    VERSION 1.0.0
    DESCRIPTION "Cross-platform text editor"
    LANGUAGES CXX
)

set(APP_DISTRIBUTION "" CACHE STRING "Distribution type")
string(TIMESTAMP CURRENT_YEAR "%Y")
set(APP_COPYRIGHT "8bitCode (formerly 8bitPad) contributors / based on Notepad Next, copyright 2019-${CURRENT_YEAR} Justin Dailey")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_definitions(SCI_OWNREGEX)

if(MSVC)
    # https://stackoverflow.com/questions/78598141/first-stdmutexlock-crashes-in-application-built-with-latest-visual-studio
    add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
endif()

if(UNIX AND NOT APPLE)
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
    add_link_options(-Wl,-z,relro -Wl,-z,now)
endif()

find_package(Qt6
    REQUIRED
        COMPONENTS
            Core
            Gui
            Widgets
            Network
            PrintSupport
            Core5Compat
            Concurrent
            Svg
)

set(CMAKE_AUTORCC ON)
qt_standard_project_setup()

set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cpm-cache" CACHE PATH "CPM source cache directory")

include(CTest)
add_subdirectory(thirdparty)
include(GNUInstallDirs)
add_subdirectory(src/editor)
add_subdirectory(src/update)
add_subdirectory(src)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
include(${CMAKE_SOURCE_DIR}/cmake/BitPadPackaging.cmake)
