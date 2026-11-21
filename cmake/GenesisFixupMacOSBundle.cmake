if(NOT DEFINED GENESIS_APP_BUNDLE OR NOT IS_DIRECTORY "${GENESIS_APP_BUNDLE}")
    message(FATAL_ERROR "GENESIS_APP_BUNDLE must name an existing .app bundle")
endif()

include(BundleUtilities)
set(BU_CHMOD_BUNDLE_ITEMS TRUE)
fixup_bundle("${GENESIS_APP_BUNDLE}" "" "")

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
