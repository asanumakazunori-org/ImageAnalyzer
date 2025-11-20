cmake_minimum_required(VERSION 3.21)

project(ImageAnalyzer LANGUAGES CXX)

#----------------------------------------
# Common settings
#----------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt and vcpkg
set(CMAKE_PREFIX_PATH
    "C:/Qt/6.10.0/msvc2022_64"
    "C:/work/vcpkg/installed/x64-windows"
)

# Qt automatic handling
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

#----------------------------------------
# Dependencies
#----------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED)
enable_testing()
find_package(GTest CONFIG REQUIRED)

#----------------------------------------
# Compiler options
#----------------------------------------
if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

add_definitions(-DUNICODE -D_UNICODE)

#----------------------------------------
# OpenCV no-analyze wrapper
#----------------------------------------
add_library(OpenCV_noanalyze INTERFACE)
target_include_directories(OpenCV_noanalyze INTERFACE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(OpenCV_noanalyze INTERFACE ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(OpenCV_noanalyze INTERFACE /analyze-)
endif()

#----------------------------------------
# ImageProc
#----------------------------------------
file(GLOB IMAGEPROC_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageProc/*.cpp
)
file(GLOB IMAGEPROC_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageProc/*.h
)

add_library(ImageProc STATIC
    ${IMAGEPROC_SOURCES}
    ${IMAGEPROC_HEADERS}
)

target_include_directories(ImageProc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageProc
)

target_link_libraries(ImageProc PUBLIC
    OpenCV_noanalyze
)

#----------------------------------------
# ImageViewer (Qt GUI)
#----------------------------------------
file(GLOB IMAGEVIEWER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageViewer/*.cpp
)
file(GLOB IMAGEVIEWER_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageViewer/*.h
)

add_executable(ImageViewer
    ${IMAGEVIEWER_SOURCES}
    ${IMAGEVIEWER_HEADERS}
)

target_link_libraries(ImageViewer PRIVATE
    Qt6::Widgets
    ImageProc
)

#----------------------------------------
# DLL directories
#----------------------------------------
set(QT_ROOT "C:/Qt/6.10.0/msvc2022_64")
set(QT_BIN_DIR "${QT_ROOT}/bin")
set(QT_PLATFORMS_DIR "${QT_ROOT}/plugins/platforms")

set(OPENCV_BIN_DIR_DEBUG   "C:/work/vcpkg/installed/x64-windows/debug/bin")
set(OPENCV_BIN_DIR_RELEASE "C:/work/vcpkg/installed/x64-windows/bin")

#----------------------------------------
# Safe DLL select (no generator expressions!)
#----------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(QT_CORE_DLL      "${QT_BIN_DIR}/Qt6Cored.dll")
    set(QT_GUI_DLL       "${QT_BIN_DIR}/Qt6Guid.dll")
    set(QT_WIDGETS_DLL   "${QT_BIN_DIR}/Qt6Widgetsd.dll")
    set(QT_PLATFORM_DLL  "${QT_PLATFORMS_DIR}/qwindowsd.dll")

    set(OPENCV_CORE_DLL       "${OPENCV_BIN_DIR_DEBUG}/opencv_core4d.dll")
    set(OPENCV_IMGPROC_DLL    "${OPENCV_BIN_DIR_DEBUG}/opencv_imgproc4d.dll")
    set(OPENCV_IMGCODECS_DLL  "${OPENCV_BIN_DIR_DEBUG}/opencv_imgcodecs4d.dll")
else()
    set(QT_CORE_DLL      "${QT_BIN_DIR}/Qt6Core.dll")
    set(QT_GUI_DLL       "${QT_BIN_DIR}/Qt6Gui.dll")
    set(QT_WIDGETS_DLL   "${QT_BIN_DIR}/Qt6Widgets.dll")
    set(QT_PLATFORM_DLL  "${QT_PLATFORMS_DIR}/qwindows.dll")

    set(OPENCV_CORE_DLL       "${OPENCV_BIN_DIR_RELEASE}/opencv_core4.dll")
    set(OPENCV_IMGPROC_DLL    "${OPENCV_BIN_DIR_RELEASE}/opencv_imgproc4.dll")
    set(OPENCV_IMGCODECS_DLL  "${OPENCV_BIN_DIR_RELEASE}/opencv_imgcodecs4.dll")
endif()

#----------------------------------------
# Copy DLLs (safe for Ninja)
#----------------------------------------
add_custom_command(TARGET ImageViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QT_CORE_DLL}"      "$<TARGET_FILE_DIR:ImageViewer>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QT_GUI_DLL}"       "$<TARGET_FILE_DIR:ImageViewer>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QT_WIDGETS_DLL}"   "$<TARGET_FILE_DIR:ImageViewer>"
)

add_custom_command(TARGET ImageViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ImageViewer>/platforms"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${QT_PLATFORM_DLL}"
                              "$<TARGET_FILE_DIR:ImageViewer>/platforms"
)

# OpenCV
add_custom_command(TARGET ImageViewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENCV_CORE_DLL}"      "$<TARGET_FILE_DIR:ImageViewer>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENCV_IMGPROC_DLL}"   "$<TARGET_FILE_DIR:ImageViewer>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENCV_IMGCODECS_DLL}" "$<TARGET_FILE_DIR:ImageViewer>"
)

#----------------------------------------
# ImageProcTests (GTest)
#----------------------------------------
file(GLOB IMAGEPROC_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp
)

if(IMAGEPROC_TEST_SOURCES)
    add_executable(ImageProcTests
        ${IMAGEPROC_TEST_SOURCES}
    )

    target_link_libraries(ImageProcTests PRIVATE
        ImageProc
        GTest::gtest_main
    )

    add_test(NAME ImageProcTests COMMAND ImageProcTests)

    add_custom_command(TARGET ImageProcTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENCV_CORE_DLL}"      "$<TARGET_FILE_DIR:ImageProcTests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENCV_IMGPROC_DLL}"   "$<TARGET_FILE_DIR:ImageProcTests>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OPENCV_IMGCODECS_DLL}" "$<TARGET_FILE_DIR:ImageProcTests>"
    )
endif()
