cmake_minimum_required(VERSION 3.21...3.25)

include(before_project_setup OPTIONAL)
# ===============================
project(
    tftpd # ORIG: netkit-tftp
    VERSION 0.17.6
    LANGUAGES CXX
)
# ===============================
include(build_options OPTIONAL)

include(GNUInstallDirs)

find_package(
    Boost 1.81 CONFIG
    COMPONENTS filesystem
    REQUIRED
)

# ---- Add other dependencies via CPM ----
# see https://github.com/cpm-cmake/CPM.cmake for more info

include(cmake/CPM.cmake)

# PackageProject.cmake will be used to make our target installable
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.10.0")

#---------------------------------------------------------------------------------------
# search required packages and libs
#---------------------------------------------------------------------------------------
# This variable controls whether the CMAKE_FIND_ROOT_PATH and CMAKE_SYSROOT are used by find_package().
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
    BOTH
    CACHE STRING "NOTE: forced value!" FORCE
)
find_package(Threads REQUIRED)

add_library(
    tftpd STATIC
    async_tftpd_server.cpp
    async_tftpd_server.hpp
    tftpd.hpp
    tftpd_utils.cpp
    tftpd_options.cpp
    tftp_subs.cpp
    tftp/tftpsubs.h
)
target_link_libraries(tftpd PUBLIC Boost::filesystem Boost::headers)
target_compile_definitions(tftpd PUBLIC BOOST_ASIO_NO_DEPRECATED)
target_include_directories(
    tftpd PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                 $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(PROJECT_IS_TOP_LEVEL)
    include(WarningsAsErrors)
endif()

#---------------------------------------------------------------------------------------
# Install and export tftpd-targets
#---------------------------------------------------------------------------------------
if(NETKIT_TFTP_INSTALL)
    packageProject(
        NAME ${PROJECT_NAME}
        VERSION ${PROJECT_VERSION}
        NAMESPACE ${PROJECT_NAME}
        BINARY_DIR ${PROJECT_BINARY_DIR}
        INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include
        INCLUDE_DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        # VERSION_HEADER ${VERSION_HEADER_LOCATION}
        # EXPORT_HEADER ${EXPORT_HEADER_LOCATION}
        COMPATIBILITY SameMajorVersion
        DISABLE_VERSION_SUFFIX YES
        DEPENDENCIES "boost 1.79.0"
    )
    # NOTE: implicit done! add_library(tftpd::tftpd ALIAS tftpd)
    install(FILES async_tftpd_server.hpp
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

#---------------------------------------------------------------------------------------
# ctest
#---------------------------------------------------------------------------------------
option(NETKIT_TFTP_TESTS "Build test target." ${PROJECT_IS_TOP_LEVEL})
if(NETKIT_TFTP_TESTS)
    enable_testing()

    add_executable(option_test option_test.cpp async_tftpd_server.hpp)
    #NO! CK target_include_directories(option_test SYSTEM PRIVATE include)
    target_link_libraries(option_test PRIVATE tftpd)
    add_test(NAME option_test COMMAND option_test)

    add_executable(tftpd_test tftpd_test.cpp async_tftpd_server.hpp)
    target_link_libraries(tftpd_test PRIVATE tftpd)
    add_test(
        NAME tftpd_test
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    # test coverage option
    if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        include(CodeCoverage)

        set(COVERAGE_EXCLUDES
            '/usr/*' '/opt/*' '${CMAKE_CURRENT_SOURCE_DIR}/tftp/*'
            '${CMAKE_CURRENT_SOURCE_DIR}/tftpd/*' '/Applications/Xcode.app/*'
        )
        setup_target_for_coverage(
            NAME lcov # custom target.
            EXECUTABLE ctest # the test driver executable that runs the tests.
        )
    endif()
endif()
