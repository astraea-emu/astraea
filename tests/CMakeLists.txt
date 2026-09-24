include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.15.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(astraea_guest_worker_probe
    execution/guest_worker_probe_main.cpp
)
target_link_libraries(astraea_guest_worker_probe PRIVATE
    Astraea::core
)
astraea_set_project_warnings(astraea_guest_worker_probe)
astraea_enable_sanitizers(astraea_guest_worker_probe)

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
    execution/guest_worker_protocol_tests.cpp
    execution/guest_worker_wire_tests.cpp
    execution/guest_worker_syscall_context_tests.cpp
    execution/guest_worker_process_session_tests.cpp
    execution/guest_worker_fault_projection_tests.cpp
    execution/registered_syscall_trap_tests.cpp
    execution/sce_agc_create_shader_tests.cpp
    execution/sce_agc_link_shaders_tests.cpp
    execution/sce_agc_link_shaders_output_tests.cpp
    execution/sce_agc_driver_submit_dcb_tests.cpp
    execution/sce_agc_first_raster_submission_tests.cpp
    execution/submitted_raster_vulkan_integration_tests.cpp
    execution/sce_agc_shader_preparation_tests.cpp
    execution/sce_agc_shader_registry_tests.cpp
    execution/sce_agc_shader_stage_lookup_tests.cpp
    execution/sce_agc_submission_shader_binding_tests.cpp
    execution/sce_agc_create_shader_prototype_tests.cpp
    probe/hello_tests.cpp
    probe/reference_tests.cpp
    probe/agc_link_shaders_tail_tests.cpp
    graphics/agc_shader_binary_tests.cpp
    graphics/agc_shader_container_tests.cpp
    graphics/packet_tests.cpp
    graphics/pm4_type3_framing_tests.cpp
    graphics/pm4_draw_control_ir_tests.cpp
    graphics/pm4_set_sh_reg_ir_tests.cpp
    graphics/pm4_set_context_reg_ir_tests.cpp
    graphics/pm4_set_uconfig_reg_ir_tests.cpp
    graphics/pm4_write_data_ir_tests.cpp
    graphics/gpu_buffer_address_space_tests.cpp
    graphics/gpu_allocation_address_space_tests.cpp
    graphics/gpu_image_tests.cpp
    graphics/gfx10_surface_layout_tests.cpp
    graphics/graphics_ir_tests.cpp
    graphics/shader_register_state_tests.cpp
    graphics/geometry_es_program_address_tests.cpp
    graphics/context_register_state_tests.cpp
    graphics/color_target_state_tests.cpp
    graphics/gfx10_color_target_image_tests.cpp
    graphics/first_raster_draw_plan_tests.cpp
    graphics/user_config_register_state_tests.cpp
    graphics/rdna2_decoder_tests.cpp
    graphics/rdna2_exp_tests.cpp
    graphics/shader_ir_tests.cpp
    graphics/shader_program_tests.cpp
    graphics/shader_cfg_tests.cpp
    graphics/shader_scalar_execution_tests.cpp
    graphics/shader_control_execution_tests.cpp
    graphics/shader_program_execution_tests.cpp
    graphics/shader_vector_execution_tests.cpp
    graphics/shader_export_execution_tests.cpp
    graphics/shader_export_probe_compiler_tests.cpp
    graphics/shader_wave_block_execution_tests.cpp
    graphics/shader_wave_program_execution_tests.cpp
    graphics/spirv_vector_probe_tests.cpp
    graphics/spirv_raster_probe_tests.cpp
    graphics/vulkan_vector_probe_execution_tests.cpp
    graphics/vulkan_gpu_buffer_write_execution_tests.cpp
    graphics/vulkan_offscreen_image_execution_tests.cpp
    graphics/vulkan_raster_probe_execution_tests.cpp
    graphics/rdna2_export_vulkan_raster_tests.cpp
    graphics/decoded_color_target_rdna2_vulkan_tests.cpp
    trace/v0_tests.cpp
    trace/diff_v0_tests.cpp
    trace/graphics_v0_tests.cpp
    trace/shader_vector_execution_v0_tests.cpp
)
target_link_libraries(astraea_core_tests PRIVATE
    Astraea::core
    Catch2::Catch2WithMain
    SPIRV-Tools
)
add_dependencies(
    astraea_core_tests
    astraea_guest_worker_probe
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
   CMAKE_SYSTEM_NAME STREQUAL "Windows")
    target_compile_definitions(astraea_core_tests PRIVATE
        ASTRAEA_GUEST_WORKER_PROBE_PATH="$<TARGET_FILE:astraea_guest_worker_probe>"
    )
endif()
astraea_set_project_warnings(astraea_core_tests)
astraea_enable_sanitizers(astraea_core_tests)

astraea_verify_explicit_cpp_sources(
    ROOT .
    TARGETS
        astraea_core_tests
        astraea_guest_worker_probe
)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(astraea_core_tests)
