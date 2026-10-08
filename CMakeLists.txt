cmake_minimum_required(VERSION 3.20)
project(SwiftlyS2ExtensionSDK VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Build examples and tests by default only when configuring the SDK itself.
set(SWIFTLYS2_EXTENSION_SDK_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(SWIFTLYS2_EXTENSION_SDK_TOP_LEVEL ON)
endif()
option(SWIFTLYS2_EXTENSION_SDK_BUILD_EXAMPLES "Build the C and C++ extension examples" ${SWIFTLYS2_EXTENSION_SDK_TOP_LEVEL})
option(SWIFTLYS2_EXTENSION_SDK_BUILD_TESTS "Build and register SDK tests" ${SWIFTLYS2_EXTENSION_SDK_TOP_LEVEL})

add_library(swiftlys2-c INTERFACE)
add_library(swiftlys2::c ALIAS swiftlys2-c)
set_target_properties(swiftlys2-c PROPERTIES EXPORT_NAME c)
target_include_directories(swiftlys2-c INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(swiftlys2-c INTERFACE c_std_11)

add_library(swiftlys2-cpp INTERFACE)
add_library(swiftlys2::cpp ALIAS swiftlys2-cpp)
set_target_properties(swiftlys2-cpp PROPERTIES EXPORT_NAME cpp)
# A C++-only consumer must not inherit C compiler feature requirements.
target_include_directories(swiftlys2-cpp INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/c/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpp/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(swiftlys2-cpp INTERFACE cxx_std_23)

if(SWIFTLYS2_EXTENSION_SDK_BUILD_EXAMPLES)
    add_library(example-c MODULE examples/c/main.c)
    target_link_libraries(example-c PRIVATE swiftlys2::c)
    add_library(example-cpp MODULE examples/cpp/main.cpp)
    target_link_libraries(example-cpp PRIVATE swiftlys2::cpp)
    # SwiftlyS2 loads <name>.dll or <name>.so, without a "lib" prefix.
    set_target_properties(example-c example-cpp PROPERTIES PREFIX "")
endif()

if(SWIFTLYS2_EXTENSION_SDK_BUILD_TESTS)
    enable_testing()
    add_executable(sdk-tests tests/sdk_tests.cpp)
    target_link_libraries(sdk-tests PRIVATE swiftlys2::cpp)
    add_test(NAME sdk-contracts COMMAND sdk-tests)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cmake-integration
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_cmake.py"
                    --cmake "${CMAKE_COMMAND}")
        find_program(SWIFTLYS2_EXTENSION_SDK_XMAKE_EXECUTABLE NAMES xmake)
        if(SWIFTLYS2_EXTENSION_SDK_XMAKE_EXECUTABLE)
            add_test(NAME xmake-integration
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_xmake.py"
                        --xmake "${SWIFTLYS2_EXTENSION_SDK_XMAKE_EXECUTABLE}")
        endif()
    endif()
endif()

foreach(target IN ITEMS example-c example-cpp sdk-tests)
    if(TARGET ${target})
        set_target_properties(${target} PROPERTIES C_EXTENSIONS OFF CXX_EXTENSIONS OFF)
        if(MSVC)
            target_compile_options(${target} PRIVATE /W4 /WX)
        else()
            target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        endif()
    endif()
endforeach()

# Export only the header interfaces; consumers do not link an SDK binary.
set(SWIFTLYS2_EXTENSION_SDK_PACKAGE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/SwiftlyS2ExtensionSDK")
install(TARGETS swiftlys2-c swiftlys2-cpp EXPORT SwiftlyS2ExtensionSDKTargets)
install(DIRECTORY c/include/ cpp/include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT SwiftlyS2ExtensionSDKTargets NAMESPACE swiftlys2::
    DESTINATION "${SWIFTLYS2_EXTENSION_SDK_PACKAGE_DIR}")
configure_package_config_file(cmake/SwiftlyS2ExtensionSDKConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/SwiftlyS2ExtensionSDKConfig.cmake"
    INSTALL_DESTINATION "${SWIFTLYS2_EXTENSION_SDK_PACKAGE_DIR}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/SwiftlyS2ExtensionSDKConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/SwiftlyS2ExtensionSDKConfig.cmake"
              "${CMAKE_CURRENT_BINARY_DIR}/SwiftlyS2ExtensionSDKConfigVersion.cmake"
    DESTINATION "${SWIFTLYS2_EXTENSION_SDK_PACKAGE_DIR}")
