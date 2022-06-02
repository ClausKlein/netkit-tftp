cmake_minimum_required(VERSION 3.16...3.23)

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

# ---- Add dependencies via CPM ----
# see https://github.com/cpm-cmake/CPM.cmake for more info

include(cmake/CPM.cmake)

# PackageProject.cmake will be used to make our target installable
CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.9.0")

if(EXISTS "$ENV{HOME}/.cache/CPM/boost_1_79_0")
  set(FETCHCONTENT_SOURCE_DIR_BOOST "$ENV{HOME}/.cache/CPM/boost_1_79_0" CACHE PATH "Boost source DIR")
endif()

CPMAddPackage(
  NAME boost-cmake GITHUB_REPOSITORY ClausKlein/boost-cmake
  GIT_TAG b7c741f42b089117221622c72783d2cb821843dc # feature/upgrade-to-boost-v1.79-0
)

CPMAddPackage(
    asio
    GIT_REPOSITORY https://github.com/ClausKlein/asio.git
    GIT_TAG fea320c422a5de8d2c48cd02ae42e6251acf0030 # feature/use-cmake-init v1.22.1
    SOURCE_SUBDIR asio
)

# CPMAddPackage(
#   NAME Boost
#   VERSION 1.79.0
#   GITHUB_REPOSITORY "boostorg/boost"
#   GIT_TAG "boost-1.79.0"
# )

#---------------------------------------------------------------------------------------
# search required packages and libs
#---------------------------------------------------------------------------------------
# This variable controls whether the CMAKE_FIND_ROOT_PATH and CMAKE_SYSROOT are used by find_package().
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
    BOTH
    CACHE STRING "NOTE: forced value!" FORCE
)
find_package(Threads REQUIRED)

# if(TARGET Boost::filesystem)
#     option(NETKIT_TFTP_INSTALL "Install targets." ${PROJECT_IS_TOP_LEVEL})
# else()
#     message(WARNING "Try to fetch Boost with CPMAddPackage ...")
#     CPMAddPackage(
#         Boost
#         GITHUB_REPOSITORY Orphis/boost-cmake
#         GIT_TAG d3951bc7f0b9d09005f92aedcf6acfc595f050ea # 1.71.0
#         OPTIONS "BOOST_LIBS_OPTIONAL filesystem"
#         # setting FIND_PACKAGE_ARGUMENTS allow usage with `CPM_USE_LOCAL_PACKAGES`
#         FIND_PACKAGE_ARGUMENTS "COMPONENTS filesystem"
#     )
# endif()

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
target_link_libraries(
    tftpd PUBLIC Boost::filesystem asio::asio
)
#NO! CK target_include_directories(tftpd SYSTEM PRIVATE include)
target_include_directories(
    tftpd PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                 $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

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
        DEPENDENCIES "asio 1.22.1; boost 1.79.0"
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
