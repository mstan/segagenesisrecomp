# Complete indexed-job reuse versus the retained original no-reuse renderer.
# ROM-independent; object models never change production target definitions.
set(_job_vdp "${CMAKE_CURRENT_LIST_DIR}/../../runner/video/genesis_vdp.c")
set(_job_apis
    gvdp_init
    gvdp_reset
    gvdp_write_data
    gvdp_write_control
    gvdp_read_data
    gvdp_read_control
    gvdp_peek_data
    gvdp_peek_status
    gvdp_read_hv_counter
    gvdp_consume_68k_stall
    g_gvdp_events
    g_gvdp_event_seq
    gvdp_screen_width
    gvdp_screen_height
    gvdp_display_enabled
    gvdp_interlace_double
    gvdp_output_height
    gvdp_set_unlimited_sprites
    gvdp_set_ws_extra
    gvdp_set_ws_canvas
    gvdp_set_ws_bar_black
    gvdp_active_width
    gvdp_set_bgdiag
    gvdp_render_scanline
    gvdp_capture_scanline
    gvdp_compose_captured
    gvdp_evaluate_scanline
    gvdp_begin_scanline
    gvdp_unlimited_sprites
    gvdp_set_host_palette
    gvdp_host_palette
    gvdp_set_host_sprites
    gvdp_rb_pending_stall
    gvdp_rb_set_pending_stall
    gvdp_plane_implementation
    gvdp_set_ext_tiles
    gvdp_set_ext_colors
    gvdp_ext_active
    gvdp_ext_line
    gvdp_ext_color
    gvdp_set_sprite_patterns_bank
    gvdp_set_sprite_patterns
    gvdp_sprite_patterns
    gvdp_sprite_pattern_line
    gvdp_render_reuse_stats
    gvdp_render_reuse_clear)
add_library(vdp_job_reference_model OBJECT "${_job_vdp}")
target_compile_features(vdp_job_reference_model PRIVATE c_std_11)
target_compile_definitions(vdp_job_reference_model PRIVATE GENESIS_BATCHED_PLANES=1 GENESIS_RENDER_REUSE=0)
foreach(_api IN LISTS _job_apis)
    target_compile_definitions(vdp_job_reference_model PRIVATE "${_api}=head_${_api}")
endforeach()
add_library(vdp_job_scalar_reference_model OBJECT "${_job_vdp}")
target_compile_features(vdp_job_scalar_reference_model PRIVATE c_std_11)
target_compile_definitions(vdp_job_scalar_reference_model PRIVATE GENESIS_BATCHED_PLANES=0 GENESIS_RENDER_REUSE=0)
foreach(_api IN LISTS _job_apis)
    target_compile_definitions(vdp_job_scalar_reference_model PRIVATE "${_api}=head_${_api}")
endforeach()
foreach(_kind IN ITEMS batched portable disabled scalar)
    add_library(vdp_job_${_kind}_model OBJECT "${_job_vdp}")
    target_compile_features(vdp_job_${_kind}_model PRIVATE c_std_11)
    target_compile_definitions(vdp_job_${_kind}_model PRIVATE GENESIS_BATCHED_PLANES=$<NOT:$<STREQUAL:${_kind},scalar>> GENESIS_RENDER_REUSE_STATS=1)
    foreach(_api IN LISTS _job_apis)
        target_compile_definitions(vdp_job_${_kind}_model PRIVATE "${_api}=cand_${_api}")
    endforeach()
    if(_kind STREQUAL "portable")
        target_compile_definitions(vdp_job_${_kind}_model PRIVATE GENESIS_NATIVE_COMPOSE=0 GENESIS_NATIVE_TILE_ROWS=0)
    elseif(_kind STREQUAL "disabled")
        target_compile_definitions(vdp_job_${_kind}_model PRIVATE GENESIS_RENDER_REUSE=0)
    endif()
    if(_kind STREQUAL "scalar")
        set(_job_head vdp_job_scalar_reference_model)
    else()
        set(_job_head vdp_job_reference_model)
    endif()
    add_executable(vdp_render_reuse_${_kind}_test
        "${CMAKE_CURRENT_LIST_DIR}/vdp_render_diff_test.c"
        $<TARGET_OBJECTS:${_job_head}> $<TARGET_OBJECTS:vdp_job_${_kind}_model>)
    target_compile_features(vdp_render_reuse_${_kind}_test PRIVATE c_std_11)
    add_test(NAME vdp_render_reuse_${_kind} COMMAND vdp_render_reuse_${_kind}_test)
    if(NOT _kind STREQUAL "disabled")
        add_executable(vdp_render_jobs_${_kind}_test
            "${CMAKE_CURRENT_LIST_DIR}/vdp_render_job_test.c"
            $<TARGET_OBJECTS:${_job_head}> $<TARGET_OBJECTS:vdp_job_${_kind}_model>)
        target_compile_features(vdp_render_jobs_${_kind}_test PRIVATE c_std_11)
        add_test(NAME vdp_render_jobs_${_kind} COMMAND vdp_render_jobs_${_kind}_test)
    endif()
endforeach()

foreach(_model IN ITEMS vdp_job_reference_model vdp_job_scalar_reference_model)
    if(MSVC)
        target_compile_options(${_model} PRIVATE /W3 /WX)
    else()
        target_compile_options(${_model} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
foreach(_kind IN ITEMS batched portable disabled scalar)
    set(_checked vdp_job_${_kind}_model vdp_render_reuse_${_kind}_test)
    if(NOT _kind STREQUAL "disabled")
        list(APPEND _checked vdp_render_jobs_${_kind}_test)
    endif()
    foreach(_target IN LISTS _checked)
        if(MSVC)
            target_compile_options(${_target} PRIVATE /W3 /WX)
        else()
            target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror)
        endif()
    endforeach()
endforeach()
