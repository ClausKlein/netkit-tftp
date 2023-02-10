set(CPM_DOWNLOAD_VERSION 0.37.0)

if(CPM_SOURCE_CACHE)
    # Expand relative path. This is important if the provided path contains a tilde (~)
    get_filename_component(CPM_SOURCE_CACHE ${CPM_SOURCE_CACHE} ABSOLUTE)
    set(_CPM_DOWNLOAD_LOCATION
        "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake"
    )
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(_CPM_DOWNLOAD_LOCATION
        "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake"
    )
else()
    set(_CPM_DOWNLOAD_LOCATION
        "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake"
    )
endif()
set(CPM_DOWNLOAD_LOCATION
    "${_CPM_DOWNLOAD_LOCATION}"
    CACHE STRING "Full path to CPM.cmake file"
)

if(NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(
        DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        ${CPM_DOWNLOAD_LOCATION}
    )
    # Adjust version in original CPM.cmake file as also done in GitHub publish step:
    # https://github.com/cpm-cmake/CPM.cmake/blob/master/.github/workflows/publish.yaml
    # Avoid warnings concerning development version.
    file(READ ${CPM_DOWNLOAD_LOCATION} FILE_CONTENTS)
    string(REPLACE "1.0.0-development-version" "${CPM_DOWNLOAD_VERSION}"
                   FILE_CONTENTS "${FILE_CONTENTS}"
    )
    file(WRITE ${CPM_DOWNLOAD_LOCATION} "${FILE_CONTENTS}")
endif()

include(${CPM_DOWNLOAD_LOCATION})
