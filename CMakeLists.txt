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

include(CTest)

if(BUILD_TESTING)
    add_executable(vwad_geometry_tests
        tests/AutoDimensionGeometryTests.cpp
    )

    add_executable(vwad_algorithm_tests
        tests/AutoDimensionAlgorithmsTests.cpp
    )

    add_executable(vwad_algorithm_property_tests
        tests/AutoDimensionAlgorithmsPropertyTests.cpp
    )

    target_link_libraries(vwad_geometry_tests
        PRIVATE
            vwad_geometry
    )

    target_link_libraries(vwad_algorithm_tests
        PRIVATE
            vwad_geometry
    )

    target_link_libraries(vwad_algorithm_property_tests
        PRIVATE
            vwad_geometry
    )

    add_test(NAME vwad_geometry_tests COMMAND vwad_geometry_tests)
    add_test(NAME vwad_algorithm_tests COMMAND vwad_algorithm_tests)
    add_test(NAME vwad_algorithm_property_tests COMMAND vwad_algorithm_property_tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME vwad_geometry_python_regression
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/test_autodim_geometry.py
        )
        add_test(
            NAME vwad_sdk_source_invariants
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sdk_source_invariants.py
                --source-2025 ${CMAKE_CURRENT_SOURCE_DIR}/sdk-projects/2025/AutoDimensionPlugin/Source/AutoDimensionObj.cpp
                --source-2026 ${CMAKE_CURRENT_SOURCE_DIR}/sdk-projects/2026/AutoDimensionPlugin/Source/AutoDimensionObj.cpp
        )
    endif()
endif()

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
