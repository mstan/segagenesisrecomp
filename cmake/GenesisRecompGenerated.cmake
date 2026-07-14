# Generated 68000 translations are build products, never repository inputs.
#
# genesisrecomp_add_generated_sources(
#     <out-var> <prefix> <game-dir> <rom-name>
#     [REVERSE_DEBUG <bool>])
#
# The ROM is intentionally a build dependency.  This prevents a runner from
# silently compiling C generated from some older or different ROM image.

include_guard(GLOBAL)

if(NOT DEFINED RECOMP_ROOT)
    message(FATAL_ERROR "RECOMP_ROOT must name the segagenesisrecomp checkout")
endif()

if(NOT TARGET GenesisRecomp)
    add_subdirectory("${RECOMP_ROOT}/recompiler"
                     "${CMAKE_BINARY_DIR}/genesis-recompiler")
endif()

function(genesisrecomp_add_generated_sources OUT_VAR PREFIX GAME_DIR ROM_NAME)
    set(one_value_args REVERSE_DEBUG)
    cmake_parse_arguments(GRG "" "${one_value_args}" "" ${ARGN})

    set(_game "${RECOMP_ROOT}/${GAME_DIR}")
    set(_rom  "${_game}/${ROM_NAME}")
    # Each configured build owns its generated C. This prevents normal,
    # reverse-debug, and concurrent build trees from overwriting one another.
    set(_gen  "${CMAKE_CURRENT_BINARY_DIR}/generated/${PREFIX}")

    set(_full     "${_gen}/${PREFIX}_full.c")
    set(_dispatch "${_gen}/${PREFIX}_dispatch.c")
    set(_layout   "${_gen}/${PREFIX}_layout.c")
    set(_cycles   "${_gen}/${PREFIX}_cycles.c")
    set(_sources  "${_full}" "${_dispatch}" "${_layout}")
    set(_outputs  ${_sources} "${_cycles}")
    set(_audit    "${_gen}/${PREFIX}_dispatch_audit.log")
    set(_unresolved "${_gen}/${PREFIX}.unresolved_jumptables.log")

    set(_stage "${CMAKE_CURRENT_BINARY_DIR}/generation-stage/${PREFIX}")
    set(_stage_full       "${_stage}/${PREFIX}_full.c")
    set(_stage_dispatch   "${_stage}/${PREFIX}_dispatch.c")
    set(_stage_layout     "${_stage}/${PREFIX}_layout.c")
    set(_stage_cycles     "${_stage}/${PREFIX}_cycles.c")
    set(_stage_audit      "${_stage}/${PREFIX}_dispatch_audit.log")
    set(_stage_unresolved "${_stage}/${PREFIX}.unresolved_jumptables.log")
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_generation.stamp")

    # All discovery/configuration inputs participate in freshness checking.
    # CONFIGURE_DEPENDS also makes newly added discovery fragments visible
    # without requiring a hand-maintained dependency list here.
    file(GLOB _config_inputs CONFIGURE_DEPENDS
        "${_game}/*.toml"
        "${_game}/*.txt"
        "${_game}/*.csv")
    # An exact CONFIGURE_DEPENDS glob lets configuration succeed without the
    # private ROM, then automatically adds it as a freshness dependency when
    # the user supplies it.
    file(GLOB _rom_input CONFIGURE_DEPENDS "${_rom}")
    if(_rom_input)
        set(_rom_present 1)
    else()
        set(_rom_present 0)
    endif()

    set(_recompiler_args "${ROM_NAME}" --game game.toml
        --output-dir "${_stage}")
    if(GRG_REVERSE_DEBUG)
        list(APPEND _recompiler_args --reverse-debug)
        set(_reverse_value 1)
    else()
        set(_reverse_value 0)
    endif()

    # CMake updates this file's timestamp only when its content changes. It
    # invalidates generated C for option switches and for input additions,
    # removals, or renames (paths removed from DEPENDS alone do not do that).
    list(SORT _config_inputs)
    string(JOIN "\ninput=" _input_paths ${_config_inputs})
    set(_options_file
        "${CMAKE_CURRENT_BINARY_DIR}/${PREFIX}_generation_options.txt")
    file(GENERATE OUTPUT "${_options_file}"
         CONTENT "pipeline=2\nprefix=${PREFIX}\ngame=${_game}\nrom=${_rom}\nreverse_debug=${_reverse_value}\nrom_present=${_rom_present}\ninput=${_input_paths}\n")

    add_custom_command(
        OUTPUT "${_stamp}"
        BYPRODUCTS
            "${_stage_full}" "${_stage_dispatch}" "${_stage_layout}"
            "${_stage_cycles}" "${_stage_audit}" "${_stage_unresolved}"
            "${_audit}" "${_unresolved}"
        # Invalidate the old stamp and compiled outputs before doing any work.
        # A failed or interrupted generator therefore cannot authorize stale C.
        COMMAND "${CMAKE_COMMAND}" -E remove -f
            "${_stamp}" ${_outputs} "${_audit}" "${_unresolved}"
        COMMAND "${CMAKE_COMMAND}" -E remove_directory "${_stage}"
        COMMAND "${CMAKE_COMMAND}"
            "-DREQUIRED_FILE=${_rom}"
            "-DREQUIRED_LABEL=${PREFIX} ROM"
            -P "${RECOMP_ROOT}/cmake/RequireFile.cmake"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stage}" "${_gen}"
        COMMAND "$<TARGET_FILE:GenesisRecomp>" ${_recompiler_args}
        # Diagnostics are promoted with the generation. Compiled outputs have
        # explicit rules below so Visual Studio orders them before compilation.
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_stage_audit}" "${_audit}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_stage_unresolved}" "${_unresolved}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
        DEPENDS
            GenesisRecomp
            ${_rom_input}
            "${_game}/game.toml"
            "${_options_file}"
            ${_config_inputs}
        WORKING_DIRECTORY "${_game}"
        COMMENT "Regenerating ${PREFIX} native C from ${ROM_NAME}"
        VERBATIM
        COMMAND_EXPAND_LISTS)

    add_custom_command(OUTPUT "${_full}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_stage_full}" "${_full}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_full}"
        DEPENDS "${_stamp}" VERBATIM)
    add_custom_command(OUTPUT "${_dispatch}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_stage_dispatch}" "${_dispatch}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_dispatch}"
        DEPENDS "${_stamp}" VERBATIM)
    add_custom_command(OUTPUT "${_layout}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_stage_layout}" "${_layout}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_layout}"
        DEPENDS "${_stamp}" VERBATIM)
    add_custom_command(OUTPUT "${_cycles}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_stage_cycles}" "${_cycles}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_cycles}"
        DEPENDS "${_stamp}" VERBATIM)

    set_source_files_properties(${_outputs} PROPERTIES GENERATED TRUE)
    add_custom_target("genesisrecomp_generate_${PREFIX}" DEPENDS ${_outputs})
    set(${OUT_VAR} ${_sources} PARENT_SCOPE)
    set(${OUT_VAR}_FULL "${_full}" PARENT_SCOPE)
    set(${OUT_VAR}_CYCLES "${_cycles}" PARENT_SCOPE)
    set(${OUT_VAR}_DIR "${_gen}" PARENT_SCOPE)
    set(${OUT_VAR}_TARGET "genesisrecomp_generate_${PREFIX}" PARENT_SCOPE)
endfunction()
