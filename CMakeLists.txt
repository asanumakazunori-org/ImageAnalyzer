cmake_minimum_required(VERSION 3.21)

project(ImageAnalyzer LANGUAGES CXX)

# ------------------------------------------------------------------
# 基本設定
# ------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ------------------------------------------------------------------
# Qt は C:\Qt\6.10.1\msvc2022_64 から、
# OpenCV / GTest は vcpkg から取得
# ------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(OpenCV REQUIRED)
find_package(GTest CONFIG REQUIRED)

enable_testing()

# ------------------------------------------------------------------
# MSVC の共通コンパイルオプション
# ------------------------------------------------------------------
if(MSVC)
    add_compile_options(/utf-8 /W4)
    add_definitions(-D_USE_MATH_DEFINES)
endif()

add_definitions(-DUNICODE -D_UNICODE)

# ------------------------------------------------------------------
# OpenCV_noanalyze ラッパー（アナライザ無効化）
# ------------------------------------------------------------------
add_library(OpenCV_noanalyze INTERFACE)
target_include_directories(OpenCV_noanalyze INTERFACE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(OpenCV_noanalyze INTERFACE ${OpenCV_LIBS})

if(MSVC)
    target_compile_options(OpenCV_noanalyze INTERFACE /analyze-)
endif()

# ------------------------------------------------------------------
# ImageProc ライブラリ
# ------------------------------------------------------------------
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

# ------------------------------------------------------------------
# ImageViewer（Qt GUI）
# ------------------------------------------------------------------
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
    ImageProc
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

# ------------------------------------------------------------------
# DLL コピー
#   - vcpkg 由来の DLL（OpenCV など）は $<TARGET_RUNTIME_DLLS> で自動コピー
#   - Qt の DLL / plugins は C:\Qt\6.10.1\msvc2022_64 から取得
# ------------------------------------------------------------------
if(WIN32)
    # vcpkg 側の DLL（OpenCV など）をコピー
    add_custom_command(TARGET ImageViewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Copy runtime DLLs for ImageViewer..."
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:ImageViewer> $<TARGET_FILE_DIR:ImageViewer>
        COMMAND_EXPAND_LISTS
    )

    # ---- Qt のインストールパスを Qt6::Core から逆算 ----
    get_target_property(_qt_core_location Qt6::Core LOCATION)
    if(NOT _qt_core_location)
        get_target_property(_qt_core_location Qt6::Core IMPORTED_LOCATION_RELEASE)
    endif()

    if(_qt_core_location)
        # 例: C:/Qt/6.10.1/msvc2022_64/bin/Qt6Core.dll
        get_filename_component(QT_BIN_DIR  "${_qt_core_location}" DIRECTORY)   # .../bin
        get_filename_component(QT_ROOT_DIR "${QT_BIN_DIR}" DIRECTORY)          # .../msvc2022_64
        set(QT_PLUGINS_DIR "${QT_ROOT_DIR}/plugins")

        # Qt の主要 DLL をコピー（必要に応じて追加可能）
        add_custom_command(TARGET ImageViewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copy Qt DLLs..."
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${QT_BIN_DIR}/Qt6Core.dll"
                    "${QT_BIN_DIR}/Qt6Gui.dll"
                    "${QT_BIN_DIR}/Qt6Widgets.dll"
                    "$<TARGET_FILE_DIR:ImageViewer>"
        )

        # Qt platform plugins (platforms/) をコピー
        add_custom_command(TARGET ImageViewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copy Qt platform plugins..."
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ImageViewer>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${QT_PLUGINS_DIR}/platforms"
                    "$<TARGET_FILE_DIR:ImageViewer>/platforms"
        )
    else()
        message(WARNING "Could not determine Qt6::Core location. Qt DLLs/plugins will not be copied automatically.")
    endif()
endif()

# ------------------------------------------------------------------
# 単体テスト (GTest)
# ------------------------------------------------------------------
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp
)

if(TEST_SOURCES)
    add_executable(ImageProcTests
        ${TEST_SOURCES}
    )

    target_link_libraries(ImageProcTests PRIVATE
        ImageProc
        GTest::gtest_main
    )

    add_test(NAME ImageProcTests COMMAND ImageProcTests)

    # GTest 用 DLL コピー
    if(WIN32)
        add_custom_command(TARGET ImageProcTests POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E echo "Copy runtime DLLs for ImageProcTests..."
            COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:ImageProcTests> $<TARGET_FILE_DIR:ImageProcTests>
            COMMAND_EXPAND_LISTS
        )
    endif()
endif()
