include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(astraea_core_tests
    core/version_tests.cpp
    core/result_tests.cpp
    loader/elf64_tests.cpp
    loader/sce_program_header_tests.cpp
    loader/dynamic_tests.cpp
    loader/dynamic_metadata_tests.cpp
    loader/sce_dynamic_metadata_tests.cpp
    loader/dynamic_string_tests.cpp
    loader/sysv_hash_tests.cpp
    loader/dynamic_symbols_tests.cpp
    loader/sce_symbol_identity_tests.cpp
    loader/sce_dynamic_symbol_tests.cpp
    loader/dynamic_relocations_tests.cpp
    loader/dynamic_plt_relocations_tests.cpp
    loader/initial_stack_tests.cpp
    loader/tls_template_tests.cpp
    loader/guest_image_tests.cpp
    memory/guest_address_tests.cpp
    memory/mapping_tests.cpp
    memory/initialized_image_view_tests.cpp
    execution/context_tests.cpp
    execution/memory_plan_tests.cpp
    execution/linux_memory_tests.cpp
    execution/linux_execution_tests.cpp
    execution/windows_execution_tests.cpp
    execution/windows_memory_tests.cpp
    execution/hle_tests.cpp
    execution/sce_import_binding_tests.cpp
    execution/sce_import_resolution_tests.cpp
    execution/sce_plt_import_batch_tests.cpp
    execution/sce_jump_slot_patch_tests.cpp
    execution/sce_glob_dat_patch_tests.cpp
    execution/sce_glob_dat_apply_tests.cpp
    execution/sce_glob_dat_prototype_tests.cpp
    execution/sce_prototype_tests.cpp
    execution/sce_multi_import_prototype_tests.cpp
    execution/hle_runtime_tests.cpp
    execution/sce_agc_create_shader_tests.cpp
    probe/hello_tests.cpp
    probe/reference_tests.cpp
    graphics/agc_shader_binary_tests.cpp
    graphics/agc_shader_container_tests.cpp
    graphics/packet_tests.cpp
    graphics/graphics_ir_tests.cpp
    graphics/rdna2_decoder_tests.cpp
    graphics/shader_ir_tests.cpp
    graphics/shader_program_tests.cpp
    graphics/shader_cfg_tests.cpp
    graphics/shader_scalar_execution_tests.cpp
    graphics/shader_control_execution_tests.cpp
    graphics/shader_program_execution_tests.cpp
    graphics/shader_vector_execution_tests.cpp
    graphics/shader_wave_block_execution_tests.cpp
    graphics/shader_wave_program_execution_tests.cpp
    trace/v0_tests.cpp
    trace/diff_v0_tests.cpp
    trace/graphics_v0_tests.cpp
    trace/shader_vector_execution_v0_tests.cpp
)
target_link_libraries(astraea_core_tests PRIVATE
    Astraea::core
    Catch2::Catch2WithMain
)
astraea_set_project_warnings(astraea_core_tests)
astraea_enable_sanitizers(astraea_core_tests)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(astraea_core_tests)
