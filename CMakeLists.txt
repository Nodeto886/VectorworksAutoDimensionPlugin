cmake_minimum_required(VERSION 3.24)

project(VectorworksAutoDimensionPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(vwad_geometry STATIC
    src/AutoDimensionGeometry.cpp
)

target_include_directories(vwad_geometry
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

option(VWAD_BUILD_VECTORWORKS_ADAPTER "Build the Vectorworks SDK adapter" OFF)
set(VECTORWORKS_SDK_ROOT "" CACHE PATH "Path to the Vectorworks SDK root")

if(VWAD_BUILD_VECTORWORKS_ADAPTER)
    if(NOT VECTORWORKS_SDK_ROOT)
        message(FATAL_ERROR "Set VECTORWORKS_SDK_ROOT to build the SDK adapter")
    endif()

    add_library(vwad_vectorworks_adapter STATIC
        src/Vectorworks/AutoDimensionEventSink.cpp
    )

    target_include_directories(vwad_vectorworks_adapter
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${VECTORWORKS_SDK_ROOT}/SDKLib/Include
    )

    target_link_libraries(vwad_vectorworks_adapter
        PUBLIC
            vwad_geometry
    )
endif()
