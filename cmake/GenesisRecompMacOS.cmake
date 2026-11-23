include_guard(GLOBAL)

set_property(GLOBAL PROPERTY _GENESIS_RECOMP_MACOS_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

option(GENESIS_MACOS_ADHOC_SIGN
    "Ad-hoc sign macOS app bundles after embedding runtime dependencies" ON)

function(genesisrecomp_macos_bundle target)
    set(one_value_args BUNDLE_IDENTIFIER BUNDLE_NAME VERSION ICON)
    set(multi_value_args RESOURCES RUNTIME_LIBRARIES)
    cmake_parse_arguments(GMB "" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(GMB_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
            "genesisrecomp_macos_bundle(${target}): unknown arguments: "
            "${GMB_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT APPLE)
        return()
    endif()
    if(NOT CMAKE_HOST_APPLE)
        message(FATAL_ERROR
            "genesisrecomp_macos_bundle(${target}) requires a macOS build host")
    endif()
    if(NOT TARGET ${target})
        message(FATAL_ERROR
            "genesisrecomp_macos_bundle(${target}): target does not exist")
    endif()
    if(NOT GMB_BUNDLE_IDENTIFIER)
        message(FATAL_ERROR
            "genesisrecomp_macos_bundle(${target}) requires BUNDLE_IDENTIFIER")
    endif()
    if(NOT GMB_BUNDLE_NAME)
        set(GMB_BUNDLE_NAME "${target}")
    endif()
    if(NOT GMB_VERSION)
        if(PROJECT_VERSION)
            set(GMB_VERSION "${PROJECT_VERSION}")
        else()
            set(GMB_VERSION "0")
        endif()
    endif()

    get_property(_module_dir GLOBAL PROPERTY _GENESIS_RECOMP_MACOS_CMAKE_DIR)
    foreach(_value IN ITEMS "${GMB_BUNDLE_NAME}" "${GMB_BUNDLE_IDENTIFIER}"
                            "${GMB_VERSION}" "${GMB_ICON}")
        string(FIND "${_value}" "]]>" _cdata_end)
        if(NOT _cdata_end EQUAL -1)
            message(FATAL_ERROR
                "genesisrecomp_macos_bundle(${target}): metadata cannot contain ]]>")
        endif()
    endforeach()

    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "${GMB_BUNDLE_NAME}"
        MACOSX_BUNDLE_BUNDLE_NAME "${GMB_BUNDLE_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${GMB_BUNDLE_IDENTIFIER}"
        MACOSX_BUNDLE_INFO_PLIST
            "${_module_dir}/GenesisRecompInfo.plist.in"
        MACOSX_BUNDLE_INFO_STRING "${GMB_BUNDLE_NAME} ${GMB_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${GMB_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${GMB_VERSION}")
    target_compile_definitions(${target} PRIVATE GENESIS_MACOS_BUNDLE=1)

    set(_resources ${GMB_RESOURCES})
    if(GMB_ICON)
        list(APPEND _resources "${GMB_ICON}")
        get_filename_component(_icon_name "${GMB_ICON}" NAME)
        set_target_properties(${target} PROPERTIES
            MACOSX_BUNDLE_ICON_FILE "${_icon_name}")
    endif()
    foreach(_resource IN LISTS _resources)
        get_filename_component(
            _resource_abs "${_resource}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT EXISTS "${_resource_abs}")
            message(FATAL_ERROR
                "genesisrecomp_macos_bundle(${target}): missing resource "
                "${_resource_abs}")
        endif()
        target_sources(${target} PRIVATE "${_resource_abs}")
        set_source_files_properties("${_resource_abs}" PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
    endforeach()

    set(_runtime_libraries "")
    foreach(_library IN LISTS GMB_RUNTIME_LIBRARIES)
        if(TARGET "${_library}")
            list(APPEND _runtime_libraries "$<TARGET_FILE:${_library}>")
        else()
            get_filename_component(
                _library_abs "${_library}" ABSOLUTE
                BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            if(NOT EXISTS "${_library_abs}")
                message(FATAL_ERROR
                    "genesisrecomp_macos_bundle(${target}): missing runtime "
                    "library ${_library_abs}")
            endif()
            list(APPEND _runtime_libraries "${_library_abs}")
        endif()
    endforeach()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            "-DGENESIS_APP_BUNDLE=$<TARGET_BUNDLE_DIR:${target}>"
            "-DGENESIS_ADHOC_SIGN=$<BOOL:${GENESIS_MACOS_ADHOC_SIGN}>"
            "-DGENESIS_RUNTIME_LIBRARIES=$<JOIN:${_runtime_libraries},|>"
            -P "${_module_dir}/GenesisFixupMacOSBundle.cmake"
        COMMENT "Embedding and signing ${GMB_BUNDLE_NAME}.app"
        VERBATIM)
endfunction()
