cmake_minimum_required(VERSION 3.25)

project(
    Astraea
    VERSION 0.0.0
    DESCRIPTION "Verification-first PlayStation 5 compatibility research and emulation project"
    LANGUAGES C CXX
)

option(ASTRAEA_WARNINGS_AS_ERRORS "Treat Astraea compiler warnings as errors" OFF)
option(ASTRAEA_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(ASTRAEA_BUILD_FUZZERS "Build libFuzzer targets when supported" OFF)

include(CTest)
include(FetchContent)

set(VULKAN_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(VULKAN_HEADERS_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG v1.4.350
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vulkan_headers)

set(VOLK_PULL_IN_VULKAN ON CACHE BOOL "" FORCE)
set(VOLK_INSTALL OFF CACHE BOOL "" FORCE)
set(VOLK_HEADERS_ONLY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG 1.4.350
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(volk)

set(SPIRV_HEADERS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    spirv_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG 29981f65241605e08b0ede4cfeb999fe3b723c6a
)
FetchContent_MakeAvailable(spirv_headers)

if(BUILD_TESTING)
    set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
    set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
    set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
    set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
    set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(SPIRV-Headers_SOURCE_DIR
        "${spirv_headers_SOURCE_DIR}"
        CACHE PATH "" FORCE
    )
    FetchContent_Declare(
        spirv_tools
        GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
        GIT_TAG v2026.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(spirv_tools)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AstraeaWarnings)
include(AstraeaSanitizers)
include(AstraeaFuzzing)

find_package(Threads REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    enable_language(ASM)
endif()

if(WIN32 AND MSVC AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    enable_language(ASM_MASM)
endif()

add_library(astraea_core STATIC
    src/core/version.cpp
    src/loader/elf64.cpp
    src/loader/dynamic.cpp
    src/loader/dynamic_metadata.cpp
    src/loader/dynamic_string.cpp
    src/loader/sysv_hash.cpp
    src/loader/dynamic_symbols.cpp
    src/loader/sce_symbol_identity.cpp
    src/loader/sce_dynamic_symbol.cpp
    src/loader/dynamic_relocations.cpp
    src/loader/initial_stack.cpp
    src/loader/tls_template.cpp
    src/loader/guest_image.cpp
    src/memory/mapping.cpp
    src/memory/initialized_image_view.cpp
    src/execution/context.cpp
    src/execution/memory_plan.cpp
    src/execution/linux_memory.cpp
    src/execution/linux_execution.cpp
    src/execution/hle.cpp
    src/execution/sce_import_binding.cpp
    src/execution/sce_import_resolution.cpp
    src/execution/sce_jump_slot_patch.cpp
    src/execution/sce_jump_slot_apply.cpp
    src/execution/sce_glob_dat_patch.cpp
    src/execution/sce_glob_dat_apply.cpp
    src/execution/linux_session.cpp
    src/execution/windows_execution.cpp
    src/execution/windows_memory.cpp
    src/execution/hle_runtime.cpp
    src/execution/guest_memory.cpp
    src/execution/sce_agc_create_shader.cpp
    src/execution/sce_agc_driver_submit_dcb.cpp
    src/execution/sce_agc_shader_preparation.cpp
    src/probe/hello.cpp
    src/probe/reference.cpp
    src/graphics/agc_shader_binary.cpp
    src/graphics/agc_shader_container.cpp
    src/graphics/packet.cpp
    src/graphics/graphics_ir.cpp
    src/graphics/rdna2_decoder.cpp
    src/graphics/shader_ir.cpp
    src/graphics/shader_program.cpp
    src/graphics/shader_cfg.cpp
    src/graphics/shader_scalar_execution.cpp
    src/graphics/shader_control_execution.cpp
    src/graphics/shader_program_execution.cpp
    src/graphics/shader_vector_execution.cpp
    src/graphics/shader_wave_block_execution.cpp
    src/graphics/shader_wave_program_execution.cpp
    src/graphics/spirv_vector_probe.cpp
    src/graphics/vulkan_vector_probe_execution.cpp
    src/trace/v0.cpp
    src/trace/diff_v0.cpp
    src/trace/graphics_v0.cpp
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    target_sources(astraea_core PRIVATE
        src/execution/linux_transition.S
    )
endif()

if(WIN32 AND MSVC AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
    target_sources(astraea_core PRIVATE
        src/execution/windows_transition.asm
    )
endif()

add_library(Astraea::core ALIAS astraea_core)

target_include_directories(astraea_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(astraea_core PUBLIC cxx_std_23)
target_link_libraries(astraea_core PRIVATE
    Threads::Threads
    SPIRV-Headers::SPIRV-Headers
    volk::volk
)
astraea_set_project_warnings(astraea_core)
astraea_enable_sanitizers(astraea_core)

add_executable(astraea
    src/app/main.cpp
)
target_link_libraries(astraea PRIVATE Astraea::core)
astraea_set_project_warnings(astraea)
astraea_enable_sanitizers(astraea)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(ASTRAEA_BUILD_FUZZERS)
    add_subdirectory(fuzz)
endif()
