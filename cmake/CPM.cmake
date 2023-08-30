set(CPM_DOWNLOAD_VERSION 0.38.7)

if(CPM_SOURCE_CACHE)
    set(_CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(_CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
    set(_CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

set(CPM_DOWNLOAD_LOCATION
    "${_CPM_DOWNLOAD_LOCATION}"
    CACHE STRING "Full path to CPM.cmake file"
)

# Expand relative path. This is important if the provided path contains a tilde (~)
get_filename_component(CPM_DOWNLOAD_LOCATION ${CPM_DOWNLOAD_LOCATION} ABSOLUTE)

function(download_cpm)
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    set(GITHUB_MIRROR "https://code.rsint.net/mirror/github.com")
    file(DOWNLOAD ${GITHUB_MIRROR}/cpm-cmake/CPM.cmake/-/raw/v${CPM_DOWNLOAD_VERSION}/cmake/CPM.cmake
         ${CPM_DOWNLOAD_LOCATION}
    )
    # Version number needs to be patched.
    file(READ ${CPM_DOWNLOAD_LOCATION} FILE_CONTENT)
    string(REPLACE "1.0.0-development-version" "${CPM_DOWNLOAD_VERSION}" FILE_CONTENT "${FILE_CONTENT}")
    file(WRITE ${CPM_DOWNLOAD_LOCATION} "${FILE_CONTENT}")
endfunction()

function(download_cpm_orig)
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        ${CPM_DOWNLOAD_LOCATION}
    )
endfunction()

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    download_cpm()
else()
    # resume download if it previously failed
    file(READ ${CPM_DOWNLOAD_LOCATION} check)
    if("${check}" STREQUAL "")
        download_cpm()
    endif()
endif()

include(${CPM_DOWNLOAD_LOCATION})
