if(NOT DEFINED GENESIS_APP_BUNDLE OR NOT IS_DIRECTORY "${GENESIS_APP_BUNDLE}")
    message(FATAL_ERROR "GENESIS_APP_BUNDLE must name an existing .app bundle")
endif()

include(BundleUtilities)
set(BU_CHMOD_BUNDLE_ITEMS TRUE)
string(REPLACE "|" ";" _runtime_libraries "${GENESIS_RUNTIME_LIBRARIES}")
set(_frameworks "${GENESIS_APP_BUNDLE}/Contents/Frameworks")
set(_embedded_runtime_libraries "")
foreach(_library IN LISTS _runtime_libraries)
    get_filename_component(_library_name "${_library}" NAME)
    file(MAKE_DIRECTORY "${_frameworks}")
    file(COPY "${_library}" DESTINATION "${_frameworks}" FOLLOW_SYMLINK_CHAIN)
    list(APPEND _embedded_runtime_libraries "${_frameworks}/${_library_name}")
endforeach()
fixup_bundle("${GENESIS_APP_BUNDLE}" "${_embedded_runtime_libraries}" "")

if(GENESIS_ADHOC_SIGN)
    find_program(GENESIS_CODESIGN_EXECUTABLE codesign)
    if(NOT GENESIS_CODESIGN_EXECUTABLE)
        message(FATAL_ERROR "codesign is required to ad-hoc sign the app bundle")
    endif()
    execute_process(
        COMMAND "${GENESIS_CODESIGN_EXECUTABLE}"
            --force --deep --sign - "${GENESIS_APP_BUNDLE}"
        RESULT_VARIABLE _codesign_result
        ERROR_VARIABLE _codesign_error
        OUTPUT_QUIET)
    if(NOT _codesign_result EQUAL 0)
        message(FATAL_ERROR
            "codesign failed for ${GENESIS_APP_BUNDLE}: ${_codesign_error}")
    endif()
endif()
