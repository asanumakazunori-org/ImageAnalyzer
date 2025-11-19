cmake_minimum_required(VERSION 3.21)

project(ImageAnalyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_PREFIX_PATH
    "C:/Qt/6.10.0/msvc2022_64"
    "C:/work/vcpkg/installed/x64-windows"
    CACHE PATH "" FORCE
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(OpenCV REQUIRED)
enable_testing()
find_package(GTest CONFIG REQUIRED)

if(MSVC)
    add_compile_options(/utf-8)
    add_definitions(-D_USE_MATH_DEFINES)
endif()
add_definitions(-DUNICODE -D_UNICODE)

add_library(OpenCV_noanalyze INTERFACE)
target_include_directories(OpenCV_noanalyze INTERFACE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(OpenCV_noanalyze INTERFACE ${OpenCV_LIBS})
if(MSVC)
    target_compile_options(OpenCV_noanalyze INTERFACE /analyze-)
endif()

# --------------------------- ImageProc ---------------------------

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

# --------------------------- ImageViewer (Qt GUI) ---------------------------

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

# --------------------------- ImageProcTests (GTest) ---------------------------

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

    # ------ 修正点：Qt の deploy は不要。OpenCV の DLL コピーだけ実行 ------
    add_custom_command(TARGET ImageProcTests POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E echo "Copying OpenCV DLLs for tests..."
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "C:/work/vcpkg/installed/x64-windows/bin"
            "$<TARGET_FILE_DIR:ImageProcTests>"
        COMMENT "Copy OpenCV DLLs for test binary"
    )
else()
    message(STATUS "No test sources found in ${CMAKE_CURRENT_SOURCE_DIR}/Tests")
endif()
