cmake_minimum_required(VERSION 3.1.3)

project(NormalmapGenerator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt5Core REQUIRED)
find_package(Qt5Gui REQUIRED)
find_package(Qt5Widgets REQUIRED)
find_package(OpenMP REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(SOURCES
        main.cpp
        src_gui/mainwindow.cpp
        src_generators/intensitymap.cpp
        src_generators/normalmapgenerator.cpp
        src_generators/specularmapgenerator.cpp
        src_gui/graphicsscene.cpp
        src_gui/graphicsview.cpp
        src_gui/queueitem.cpp
        src_gui/queuemanager.cpp
        src_generators/gaussianblur.cpp
        src_generators/boxblur.cpp
        src_generators/ssaogenerator.cpp
        src_gui/aboutdialog.cpp
        src_gui/listwidget.cpp)

set(HEADERS
        src_gui/mainwindow.h
        src_generators/intensitymap.h
        src_generators/normalmapgenerator.h
        src_generators/specularmapgenerator.h
        src_gui/graphicsscene.h
        src_gui/graphicsview.h
        src_gui/queueitem.h
        src_gui/queuemanager.h
        src_generators/gaussianblur.h
        src_generators/boxblur.h
        src_generators/ssaogenerator.h
        src_gui/aboutdialog.h
        src_gui/listwidget.h
        src_gui/clickablelabel.h)


set(UI_SOURCES
        src_generators/mainwindow.ui
        src_generators/aboutdialog.ui)

set(RESOURCES stylesheets.qrc)

set(CMAKE_CXX_FLAGS "${OpenMP_CXX_FLAGS} ${CMAKE_CXX_FLAGS}")

add_executable(NormalmapGenerator ${SOURCES} ${HEADERS} ${UI_GENERATED_HEADERS} ${RESOURCES})
qt5_use_modules(NormalmapGenerator Core Gui Widgets)

target_include_directories(NormalmapGenerator PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/src_generators
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>/src_gui)