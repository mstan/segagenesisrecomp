# GenesisOracle.cmake — single source of truth for the optional, dev-only
# clownmdemu oracle.
#
# WHY THIS EXISTS. clownmdemu-core is AGPL-3.0 and is pinned behind a PRIVATE
# fork, so `git clone --recursive` failed outright for anyone outside the org.
# Nothing that ships needs it: native targets are the clean-room own backend
# and the recompiler stamps cycles from its own PRM model. The core is now an
# OPT-IN submodule used only by dev-only verification targets — the `_oracle`
# executables, the L3 semantic harness, and synth_replay.
#
# Every consumer must gate on this module rather than testing for the
# directory itself, so there is exactly one definition of "available".
#
#   include("${RECOMP_ROOT}/cmake/GenesisOracle.cmake")
#   genesis_oracle_resolve("${RECOMP_ROOT}")
#   if(GENESIS_ORACLE)
#       ...oracle-only targets...
#   endif()
#
# GENESIS_ORACLE defaults to whether the submodule is actually checked out:
# a maintainer tree that has it keeps building the oracle exactly as before,
# and a fresh public clone silently builds the shipping targets only. Force it
# either way with -DGENESIS_ORACLE=ON/OFF; ON without the submodule is a hard
# error rather than a confusing downstream failure.

if(DEFINED _GENESIS_ORACLE_CMAKE_INCLUDED)
    return()
endif()
set(_GENESIS_ORACLE_CMAKE_INCLUDED TRUE)

function(genesis_oracle_resolve RECOMP_ROOT)
    set(_core "${RECOMP_ROOT}/clownmdemu-core")
    # The submodule is present only when its content is checked out; an
    # uninitialised submodule leaves an empty directory behind.
    if(EXISTS "${_core}/CMakeLists.txt")
        set(_present TRUE)
    else()
        set(_present FALSE)
    endif()
    set(GENESIS_ORACLE_CORE_DIR "${_core}" PARENT_SCOPE)
    set(GENESIS_ORACLE_PRESENT ${_present} PARENT_SCOPE)

    if(NOT DEFINED CACHE{GENESIS_ORACLE})
        option(GENESIS_ORACLE
               "Build dev-only clownmdemu oracle targets (AGPL; never shipped)"
               ${_present})
    endif()

    if(GENESIS_ORACLE AND NOT _present)
        message(FATAL_ERROR
            "GENESIS_ORACLE=ON but the optional clownmdemu-core submodule is not "
            "checked out at:\n    ${_core}\n\n"
            "It is opt-in because it is AGPL-3.0 and lives behind a private fork. "
            "Fetch it with:\n"
            "    git submodule update --init --checkout --recursive clownmdemu-core\n\n"
            "Or configure with -DGENESIS_ORACLE=OFF to build only the shipping "
            "targets, which need none of it.")
    endif()

    if(NOT GENESIS_ORACLE)
        message(STATUS
            "Genesis oracle: OFF — building shipping targets only "
            "(no clownmdemu, no AGPL).")
    else()
        message(STATUS "Genesis oracle: ON — dev-only AGPL targets enabled.")
    endif()
endfunction()
