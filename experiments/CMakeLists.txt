cmake_minimum_required(VERSION 3.21)
project(PhysXDebrisSmoke LANGUAGES CXX)

set(PHYSX_SDK "" CACHE PATH "PhysX SDK directory containing include/PxPhysicsAPI.h")
set(PHYSX_BIN "" CACHE PATH "PhysX x64 release libraries and DLLs")
if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Configure with Visual Studio for x64.")
endif()

add_executable(physx-debris-smoke physx-debris-smoke.cpp physx-particle-solver.cpp)
add_library(physx-debris-bridge SHARED physx-debris-bridge.cpp physx-particle-solver.cpp)

foreach(component IN ITEMS PhysX PhysXCommon PhysXFoundation PhysXCooking PhysXExtensions)
    find_library(${component}_LIBRARY
        NAMES ${component}_64 ${component}_static_64 ${component}
        PATHS "${PHYSX_BIN}" NO_DEFAULT_PATH REQUIRED)
endforeach()

file(GLOB runtime_dlls "${PHYSX_BIN}/PhysX*.dll")
if(NOT runtime_dlls)
    message(FATAL_ERROR "Set PHYSX_BIN to the SDK shared release build directory.")
endif()
foreach(target IN ITEMS physx-debris-smoke physx-debris-bridge)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_definitions(${target} PRIVATE NDEBUG)
    target_include_directories(${target} PRIVATE "${PHYSX_SDK}/include")
    foreach(component IN ITEMS PhysX PhysXCommon PhysXFoundation PhysXCooking PhysXExtensions)
        target_link_libraries(${target} PRIVATE "${${component}_LIBRARY}")
    endforeach()
endforeach()
add_custom_command(TARGET physx-debris-smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${runtime_dlls} "$<TARGET_FILE_DIR:physx-debris-smoke>")
