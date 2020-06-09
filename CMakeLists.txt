cmake_minimum_required(VERSION 3.13..3.15)

# list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/Workspace/cmake)

include(before_project_setup OPTIONAL)
# ===============================
project(netkit-tftp VERSION 0.17)
# ===============================
include(build_options OPTIONAL)


include(GNUInstallDirs)
include(FetchContent)
function(fetchcontent_makeavailable_asio name)
    fetchcontent_getproperties(${name})
    if(NOT ${name}_POPULATED)
        fetchcontent_populate(${name})
        add_subdirectory(${${name}_SOURCE_DIR}/${name} ${${name}_BINARY_DIR})
        include_directories(${${name}_SOURCE_DIR}/${name}/include)
    endif()
endfunction()


#---------------------------------------------------------------------------------------
# search required packages and libs
#---------------------------------------------------------------------------------------
# This variable controls whether the CMAKE_FIND_ROOT_PATH and CMAKE_SYSROOT are used by find_package().
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH CACHE STRING "NOTE: forced value!" FORCE)
find_package(Threads REQUIRED)
find_package(Boost REQUIRED)


#---------------------------------------------------------------------------------------
# Set NETKIT_TFTP_MASTER_PROJECT to ON if we are building netkit-tftp
#---------------------------------------------------------------------------------------
# Check if netkit-tftp is being used directly or via add_subdirectory, but allow overriding
if(NOT NETKIT_TFTP_MASTER_PROJECT)
    if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        message(STATUS "${CMAKE_CURRENT_SOURCE_DIR} == ${CMAKE_SOURCE_DIR} => NETKIT_TFTP_MASTER_PROJECT")
        set(NETKIT_TFTP_MASTER_PROJECT ON)
    else()
        set(NETKIT_TFTP_MASTER_PROJECT OFF)
    endif()
endif()


if(NETKIT_TFTP_MASTER_PROJECT)
    find_package(asio 1.14.1 CONFIG)
    if(NOT TARGET asio::standalone)
        message(WARNING "Try to FetchContentget asio with FetchContent ...")
        fetchcontent_declare(
            asio
            GIT_REPOSITORY git@code2.rsint.net:sdhr/third_party/product/asio.git
            GIT_TAG 3e0d227b9307e0b680907418dd9625914a518767 # rsint-asio-1.14.1
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

add_subdirectory(tftp)
add_subdirectory(tftpd)


# asio sample
# add_executable(third_party_lib third_party_lib.cpp)
# target_link_libraries(third_party_lib asio::standalone Threads::Threads)

# asio sample
# add_executable(async_udp_echo_server async_udp_echo_server.cpp)
# target_link_libraries(async_udp_echo_server asio::standalone Threads::Threads)

add_library(tftpd async_tftpd_server.cpp async_tftpd_server.hpp tftpd.hpp
            tftpd_utils.cpp tftpd_options.cpp tftp_subs.cpp tftp/tftpsubs.h
)
add_library(tftpd::tftpd ALIAS tftpd)
target_link_libraries(tftpd PUBLIC asio::standalone Threads::Threads)
target_include_directories(tftpd PRIVATE ${Boost_INCLUDE_DIRS})
target_include_directories(tftpd PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

#---------------------------------------------------------------------------------------
# Install and export tftpd-targets
#---------------------------------------------------------------------------------------
option(NETKIT_TFTP_INSTALL "Generate the install target." ${NETKIT_TFTP_MASTER_PROJECT})
if(NETKIT_TFTP_INSTALL)
    set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/tftpd)
    install(TARGETS tftpd EXPORT tftpd-targets DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES async_tftpd_server.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES tftpd-config.cmake DESTINATION ${ConfigPackageLocation})
    install(EXPORT tftpd-targets DESTINATION ${ConfigPackageLocation} NAMESPACE tftpd::)
endif()

#---------------------------------------------------------------------------------------
# ctest
#---------------------------------------------------------------------------------------
option(NETKIT_TFTP_TESTS "Build test target." ${NETKIT_TFTP_MASTER_PROJECT})
if(NETKIT_TFTP_TESTS)
    enable_testing()

    add_executable(option_test option_test.cpp async_tftpd_server.hpp)
    target_link_libraries(option_test tftpd)
    add_test(NAME option_test COMMAND option_test)

    add_executable(tftpd_test tftpd_test.cpp async_tftpd_server.hpp)
    target_link_libraries(tftpd_test tftpd)
    add_test(
        NAME tftpd_test
        COMMAND ${CMAKE_SOURCE_DIR}/test.sh
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )

    # test coverage option
    if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
        include(CodeCoverage)

        set(COVERAGE_EXCLUDES
            '/usr/*' '/opt/*'
            '${CMAKE_SOURCE_DIR}/tftp/*'
            '${CMAKE_SOURCE_DIR}/tftpd/*'
            '/Applications/Xcode.app/*'
        )
        setup_target_for_coverage(
            NAME lcov   # custom target.
            EXECUTABLE ctest    # the test driver executable that runs the tests.
        )
    endif()
endif()

