# clownmdemu-core local patches — applied post-clone to the submodules.
# Mirrors the pattern used in nesrecomp/runner/nestopia_cmake.cmake.
#
# Callers set CLOWNMDEMU_DIR before including (or it defaults to
# ${RECOMP_ROOT}/clownmdemu-core). Each patch is applied idempotently:
# `git apply --check` succeeds only when the patch is not yet applied.

if(NOT DEFINED CLOWNMDEMU_DIR)
    set(CLOWNMDEMU_DIR "${RECOMP_ROOT}/clownmdemu-core")
endif()

set(_CLOWN68000_DIR "${CLOWNMDEMU_DIR}/libraries/clown68000")

function(_apply_clownmdemu_patch patch_path target_dir label)
    if(NOT EXISTS "${patch_path}")
        return()
    endif()
    execute_process(
        COMMAND git -C "${target_dir}" apply --check "${patch_path}"
        RESULT_VARIABLE _check_rc
        ERROR_QUIET
    )
    if(_check_rc EQUAL 0)
        message(STATUS "Applying ${label} patch...")
        execute_process(
            COMMAND git -C "${target_dir}" apply "${patch_path}"
            RESULT_VARIABLE _apply_rc
        )
        if(NOT _apply_rc EQUAL 0)
            message(WARNING "Failed to apply ${label} patch")
        endif()
    else()
        message(STATUS "${label} patch already applied (or not applicable)")
    endif()
endfunction()

_apply_clownmdemu_patch(
    "${CMAKE_CURRENT_LIST_DIR}/clownmdemu_patch.patch"
    "${CLOWNMDEMU_DIR}"
    "clownmdemu-core"
)

_apply_clownmdemu_patch(
    "${CMAKE_CURRENT_LIST_DIR}/clown68000_patch.patch"
    "${_CLOWN68000_DIR}"
    "clown68000"
)
