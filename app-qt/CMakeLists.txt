cmake_minimum_required(VERSION 3.20)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

find_package(OpenCV QUIET COMPONENTS core imgcodecs imgproc)

add_executable(AnnotaFlow
    include/Annotation.h
    include/AnnotationCanvas.h
    include/AnnotationIO.h
    include/ImageLoader.h
    include/MainWindow.h
    src/AnnotationCanvas.cpp
    src/AnnotationIO.cpp
    src/ImageLoader.cpp
    src/MainWindow.cpp
    src/main.cpp
)

target_include_directories(AnnotaFlow PRIVATE include)
target_link_libraries(AnnotaFlow PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

if(MSVC)
    target_compile_options(AnnotaFlow PRIVATE /utf-8)
else()
    target_compile_options(AnnotaFlow PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

if(OpenCV_FOUND)
    target_compile_definitions(AnnotaFlow PRIVATE ANNOTAFLOW_HAS_OPENCV)
    target_link_libraries(AnnotaFlow PRIVATE ${OpenCV_LIBS})
    target_include_directories(AnnotaFlow PRIVATE ${OpenCV_INCLUDE_DIRS})
    message(STATUS "OpenCV found: ${OpenCV_VERSION}")
else()
    message(WARNING "OpenCV was not found. The app will build with a Qt image-loading fallback.")
endif()

if(WIN32)
    set_target_properties(AnnotaFlow PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
