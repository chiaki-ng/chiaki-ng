# Apply the tracked extension to a build-local copy, leaving the submodule clean.
find_package(Git REQUIRED)
set(_aia_patch "${CMAKE_CURRENT_LIST_DIR}/curl-schannel-aia.patch")
set(CHIAKI_CURL_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/curl-patched-source")
set(CHIAKI_CURL_SCHANNEL_AIA_AVAILABLE FALSE)

file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/curl/"
    DESTINATION "${CHIAKI_CURL_SOURCE_DIR}" PATTERN ".git" EXCLUDE)
# Refresh patch inputs even if the previous patched copy has newer timestamps.
foreach(_input include/curl/curl.h lib/vtls/schannel_verify.c)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/curl/${_input}"
        "${CHIAKI_CURL_SOURCE_DIR}/${_input}" COPYONLY)
endforeach()
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
    "--directory=${CHIAKI_CURL_SOURCE_DIR}" --check "${_aia_patch}"
    RESULT_VARIABLE _can_apply)
if(_can_apply EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
        "--directory=${CHIAKI_CURL_SOURCE_DIR}" "${_aia_patch}"
        RESULT_VARIABLE _apply_result)
    if(NOT _apply_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply the Schannel AIA patch")
    endif()
    set(CHIAKI_CURL_SCHANNEL_AIA_AVAILABLE TRUE)
    message(STATUS "Schannel AIA patch applied to bundled curl")
else()
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths
        "--directory=${CHIAKI_CURL_SOURCE_DIR}" --check --reverse "${_aia_patch}"
        RESULT_VARIABLE _already_applied)
    if(_already_applied EQUAL 0)
        set(CHIAKI_CURL_SCHANNEL_AIA_AVAILABLE TRUE)
        message(STATUS "Bundled curl already includes the Schannel AIA patch")
    else()
        message(WARNING "Bundled curl does not match the Schannel AIA patch; disabling Schannel AIA recovery support")
    endif()
endif()

set(CHIAKI_CURL_SCHANNEL_AIA_AVAILABLE ${CHIAKI_CURL_SCHANNEL_AIA_AVAILABLE} PARENT_SCOPE)
