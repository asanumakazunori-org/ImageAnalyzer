cmake_minimum_required(VERSION 3.21)

# ============================================================
#  プロジェクト設定（名前と言語）
#  CMake が作成する Visual Studio ソリューション名になります。
# ============================================================
project(ImageAnalyzer LANGUAGES CXX)

# ============================================================
#  C++バージョンを指定（C++17 必須）
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
#  Qt と vcpkg(OpenCV) の検索場所を指定
#  ※ Qt / vcpkg のパスを変えた場合はここだけ修正すればよい
# ============================================================
set(CMAKE_PREFIX_PATH
    "C:/Qt/6.10.0/msvc2022_64"
    "C:/work/vcpkg/installed/x64-windows"
    CACHE PATH "" FORCE
)

# ============================================================
#  Qt の自動 MOC/UIC/RCC を有効化（Q_OBJECT の処理など）
# ============================================================
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================
#  必要ライブラリの検索
#  - Qt6 Widgets（GUI）
#  - vcpkg の OpenCV
#  - GoogleTest
# ============================================================
find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED)
enable_testing()
find_package(GTest CONFIG REQUIRED)

# ============================================================
#  Windows 共通設定（UTF-8, UNICODE）
# ============================================================
if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_USE_MATH_DEFINES)
endif()
add_definitions(-DUNICODE -D_UNICODE)

# ============================================================
#  OpenCV の /analyze 無効化ラッパー
#  （巨大ヘッダの解析を避けてビルド高速化）
# ============================================================
add_library(OpenCV_noanalyze INTERFACE)
target_include_directories(OpenCV_noanalyze INTERFACE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(OpenCV_noanalyze INTERFACE ${OpenCV_LIBS})
if(MSVC)
    target_compile_options(OpenCV_noanalyze INTERFACE /analyze-)
endif()

# ============================================================
#  1) ImageProc（OpenCVベース処理ライブラリ）
# ============================================================
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

# ============================================================
#  2) ImageViewer（Qt GUI アプリ）
# ============================================================
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

target_include_directories(ImageViewer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/ImageViewer
)

target_link_libraries(ImageViewer PRIVATE
    Qt6::Widgets
    ImageProc
)

# ============================================================
#  3) ImageProcTests（GTest ユニットテスト）
# ============================================================
file(GLOB IMAGEPROC_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp
)

if(IMAGEPROC_TEST_SOURCES)
    add_executable(ImageProcTests
        ${IMAGEPROC_TEST_SOURCES}
    )

    target_include_directories(ImageProcTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Tests
    )

    target_link_libraries(ImageProcTests PRIVATE
        ImageProc
        GTest::gtest_main
    )

    add_test(NAME ImageProcTests COMMAND ImageProcTests)
else()
    message(STATUS "No test sources found in ${CMAKE_CURRENT_SOURCE_DIR}/Tests")
endif()

# ============================================================
#  Qt + OpenCV DLL を自動コピー（配布用）
# ============================================================
add_custom_command(TARGET ImageViewer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E echo "Deploying Qt + OpenCV DLLs..."
    
    COMMAND "C:/Qt/6.10.0/msvc2022_64/bin/windeployqt.exe"
        "$<TARGET_FILE:ImageViewer>"
        --no-translations
        --compiler-runtime
        $<$<CONFIG:Debug>:--debug>
        $<$<CONFIG:Release>:--release>

    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "C:/work/vcpkg/installed/x64-windows/bin"
        "$<TARGET_FILE_DIR:ImageViewer>"

    COMMENT "Run windeployqt + copy OpenCV DLLs"
)

# ============================================================
#  ImageProcTests へ Qt + OpenCV DLL をコピー
# ============================================================
add_custom_command(TARGET ImageProcTests POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E echo "Deploying Qt + OpenCV DLLs for tests..."

    COMMAND "C:/Qt/6.10.0/msvc2022_64/bin/windeployqt.exe"
        "$<TARGET_FILE:ImageProcTests>"
        --no-translations
        --compiler-runtime
        $<$<CONFIG:Debug>:--debug>
        $<$<CONFIG:Release>:--release>

    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "C:/work/vcpkg/installed/x64-windows/bin"
        "$<TARGET_FILE_DIR:ImageProcTests>"

    COMMENT "Run windeployqt + copy OpenCV DLLs to test binary"
)
