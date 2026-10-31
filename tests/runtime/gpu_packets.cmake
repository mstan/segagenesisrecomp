# Independent original VDP versus expanded and compact owned packets.
set(_gp_root "${CMAKE_CURRENT_LIST_DIR}/../..")
set(_gp_test "${CMAKE_CURRENT_LIST_DIR}")
set(_gp_exports
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
    gvdp_capture_tile_scanline
    gvdp_compose_tile_captured
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
)
foreach(_variant IN ITEMS head cand)
    if(_variant STREQUAL "head")
        set(_source "${_gp_test}/gpu_reference/genesis_vdp.c")
    else()
        set(_source "${_gp_root}/runner/video/genesis_vdp.c")
    endif()
    add_library(gpu_packet_${_variant} OBJECT "${_source}")
    target_compile_features(gpu_packet_${_variant} PRIVATE c_std_11)
    target_include_directories(gpu_packet_${_variant} PRIVATE "${_gp_root}/runner/video")
    target_compile_definitions(gpu_packet_${_variant} PRIVATE GENESIS_BATCHED_PLANES=1 NDEBUG)
    foreach(_symbol IN LISTS _gp_exports)
        target_compile_definitions(gpu_packet_${_variant} PRIVATE ${_symbol}=${_variant}_${_symbol})
    endforeach()
endforeach()
add_executable(gpu_packet_test "${_gp_test}/vdp_render_diff_test.c"
    $<TARGET_OBJECTS:gpu_packet_head> $<TARGET_OBJECTS:gpu_packet_cand>)
target_compile_features(gpu_packet_test PRIVATE c_std_11)
target_compile_definitions(gpu_packet_test PRIVATE GENESIS_GPU_PACKET_TEST=1 NDEBUG)
foreach(_target IN ITEMS gpu_packet_head gpu_packet_cand gpu_packet_test)
    if(MSVC)
        target_compile_options(${_target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${_target} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()
add_test(NAME gpu_packet COMMAND gpu_packet_test)
add_executable(raster_frame_test "${_gp_test}/raster_frame_test.c" "${_gp_test}/gpu_original_color.c"
    $<TARGET_OBJECTS:gpu_packet_head> $<TARGET_OBJECTS:gpu_packet_cand>)
target_compile_features(raster_frame_test PRIVATE c_std_11)
foreach(_symbol IN LISTS _gp_exports)
    target_compile_definitions(raster_frame_test PRIVATE ${_symbol}=cand_${_symbol})
endforeach()
if(MSVC)
    target_compile_options(raster_frame_test PRIVATE /W4 /WX)
else()
    target_compile_options(raster_frame_test PRIVATE -Wall -Wextra -Werror)
endif()
add_test(NAME raster_frame COMMAND raster_frame_test)
