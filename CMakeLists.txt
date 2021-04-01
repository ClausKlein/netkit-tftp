cmake_minimum_required(VERSION 3.13..3.20)

include(before_project_setup OPTIONAL)
# ===============================
project(
    netkit-tftp
    VERSION 0.17.6
    LANGUAGES CXX
)
# ===============================
include(build_options OPTIONAL)

include(GNUInstallDirs)
include(FetchContent)
function(fetchcontent_makeavailable_asio name)
    FetchContent_GetProperties(${name})
    if(NOT ${name}_POPULATED)
        FetchContent_Populate(${name})
        add_subdirectory(${${name}_SOURCE_DIR}/${name} ${${name}_BINARY_DIR})
        include_directories(${${name}_SOURCE_DIR}/${name}/include)
    endif()
endfunction()

#---------------------------------------------------------------------------------------
# search required packages and libs
#---------------------------------------------------------------------------------------
# This variable controls whether the CMAKE_FIND_ROOT_PATH and CMAKE_SYSROOT are used by find_package().
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
    BOTH
    CACHE STRING "NOTE: forced value!" FORCE
)
find_package(Threads REQUIRED)
find_package(Boost 1.71.0 REQUIRED COMPONENTS filesystem)

#---------------------------------------------------------------------------------------
# Set NETKIT_TFTP_MASTER_PROJECT to ON if we are building netkit-tftp
#---------------------------------------------------------------------------------------
# Check if netkit-tftp is being used directly or via add_subdirectory, but allow overriding
if(NOT DEFINED NETKIT_TFTP_MASTER_PROJECT)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        message(
            STATUS
                "${CMAKE_CURRENT_SOURCE_DIR} == ${CMAKE_SOURCE_DIR} => NETKIT_TFTP_MASTER_PROJECT"
        )
        set(NETKIT_TFTP_MASTER_PROJECT ON)
    else()
        set(NETKIT_TFTP_MASTER_PROJECT OFF)
    endif()
endif()

if(NETKIT_TFTP_MASTER_PROJECT)
    find_package(asio 1.14.1 CONFIG)
    if(NOT TARGET asio::standalone)
        message(WARNING "Try to FetchContentget asio with FetchContent ...")
        FetchContent_Declare(
            asio
            GIT_REPOSITORY https://github.com/ClausKlein/asio.git
            GIT_TAG 466972d7aedf41f95e3b5dfde01ddc6ad9f53596 # feature/add_cmake
        )
        fetchcontent_makeavailable_asio(asio)
    endif()
else()
    if(NOT TARGET asio::standalone)
        find_package(asio 1.14.1 CONFIG REQUIRED)
    endif()
endif()
if(asio_CONFIG)
    message(STATUS "Found ${asio_CONFIG}")
endif()

#NO! CK set(CMAKE_C_STANDARD_INCLUDE_DIRECTORIES SYSTEM include)
#XXX add_subdirectory(tftp)
#XXX add_subdirectory(tftpd)

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
add_library(tftpd::tftpd ALIAS tftpd)
target_link_libraries(
    tftpd PUBLIC Boost::filesystem asio::standalone Threads::Threads
)
#NO! CK target_include_directories(tftpd SYSTEM PRIVATE include)
target_include_directories(
    tftpd PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                 $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

#---------------------------------------------------------------------------------------
# Install and export tftpd-targets
#---------------------------------------------------------------------------------------
option(NETKIT_TFTP_INSTALL "Generate the install target."
       ${NETKIT_TFTP_MASTER_PROJECT}
)
if(NETKIT_TFTP_INSTALL)
    set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/tftpd)
    install(
        TARGETS tftpd
        EXPORT tftpd-targets
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES async_tftpd_server.hpp
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES tftpd-config.cmake DESTINATION ${ConfigPackageLocation})
    install(
        EXPORT tftpd-targets
        DESTINATION ${ConfigPackageLocation}
        NAMESPACE tftpd::
    )
endif()

#---------------------------------------------------------------------------------------
# ctest
#---------------------------------------------------------------------------------------
option(NETKIT_TFTP_TESTS "Build test target." ${NETKIT_TFTP_MASTER_PROJECT})
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
