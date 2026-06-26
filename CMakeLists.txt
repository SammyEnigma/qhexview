cmake_minimum_required (VERSION 3.15)
project(QHexView)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)

# has our consumer already detected Qt? If so, only look for that version, otherwise
# prioritize 6
if(QT_VERSION_MAJOR EQUAL 6)
    find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets)
elseif(QT_VERSION_MAJOR EQUAL 5)
    find_package(QT NAMES Qt5 REQUIRED COMPONENTS Widgets)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)
endif()


add_library(QHexView
    qhexview.cpp
    qhexview.h
    QHexView
)

target_include_directories (QHexView
PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(QHexView
PUBLIC
    Qt${QT_VERSION_MAJOR}::Widgets
)

set_target_properties(QHexView
    PROPERTIES
    CXX_EXTENSIONS OFF
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
