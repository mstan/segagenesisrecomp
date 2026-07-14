if(NOT DEFINED REQUIRED_FILE OR REQUIRED_FILE STREQUAL "")
    message(FATAL_ERROR "REQUIRED_FILE was not provided")
endif()

if(NOT EXISTS "${REQUIRED_FILE}")
    if(DEFINED REQUIRED_LABEL AND NOT REQUIRED_LABEL STREQUAL "")
        set(_label "${REQUIRED_LABEL}")
    else()
        set(_label "Required input")
    endif()
    message(FATAL_ERROR
        "${_label} not found: ${REQUIRED_FILE}\n"
        "Copy your legally obtained ROM there, then build again.")
endif()
