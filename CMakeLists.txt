cmake_minimum_required(VERSION 3.21...3.28)

# Not ideal to use this global variable, but necessary to make sure that tooling and projects use
# the same version
set(CMAKE_CXX_STANDARD 17)

# strongly encouraged to enable this globally to avoid conflicts between -Wpedantic being enabled
# and -std=c++20 and -std=gnu++20 for example when compiling with PCH enabled
set(CMAKE_CXX_EXTENSIONS NO)

# XXX include(before_project_setup OPTIONAL)
# ===============================
project(
    tftpd # ORIG: netkit-tftp v0.17.6
    VERSION 1.0.0.1
    LANGUAGES CXX
)
# ===============================
# XXX include(build_options OPTIONAL)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(GNUInstallDirs)

#---------------------------------------------------------------------------------------
# search required packages and libs
#---------------------------------------------------------------------------------------

# find_package(
#     Boost 1.81 CONFIG
#     COMPONENTS filesystem asio headers
#     REQUIRED
# )

# ---- Add other dependencies via CPM ----
# see https://github.com/cpm-cmake/CPM.cmake for more info

include(cmake/CPM.cmake)

option(BUILD_SHARED_LIBS "Build shared libraries" YES)
set(BOOST_INCLUDE_LIBRARIES filesystem asio headers)
CPMAddPackage("gh:ClausKlein/boost-cmake@1.81.0-rc3")

# PackageProject.cmake will be used to make our target installable
# XXX done with boost-cmake! CPMAddPackage("gh:TheLartians/PackageProject.cmake@1.11.0")

# see https://github.com/aminya/project_options for more info
CPMAddPackage("gh:aminya/project_options@0.32.1")
# XXX list(APPEND CMAKE_MODULE_PATH ${project_options_SOURCE_DIR})

add_library(
    tftpd # FIXME(CK) STATIC
    async_tftpd_server.cpp
    async_tftpd_server.hpp
    tftpd.hpp
    tftpd_utils.cpp
    tftpd_options.cpp
    tftp_subs.cpp
    tftp/tftpsubs.h
)
list(TRANSFORM BOOST_INCLUDE_LIBRARIES PREPEND Boost:: OUTPUT_VARIABLE BOOST_TARGETS)
target_link_libraries(${PROJECT_NAME} PUBLIC ${BOOST_TARGETS})
target_compile_definitions(${PROJECT_NAME} PUBLIC BOOST_ASIO_NO_DEPRECATED)
target_include_directories(
    ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
                           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(PROJECT_IS_TOP_LEVEL AND CMAKE_BUILD_TYPE STREQUAL "Debug")
# Enable sanitizers and static analyzers when running the tests
    if($ENV{CI})
        set(ENABLE_CLANG_TIDY "ENABLE_CLANG_TIDY")
    endif()

    check_sanitizers_support(
        ENABLE_SANITIZER_ADDRESS ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
        ENABLE_SANITIZER_LEAK ENABLE_SANITIZER_THREAD ENABLE_SANITIZER_MEMORY
    )

    project_options(
        PREFIX
        ${PROJECT_NAME}
        ENABLE_CACHE
        ENABLE_COVERAGE
        ${ENABLE_CLANG_TIDY}
        ${ENABLE_SANITIZER_ADDRESS}
        ${ENABLE_SANITIZER_LEAK}
        ${ENABLE_SANITIZER_MEMORY}
        ${ENABLE_SANITIZER_THREAD}
        ${ENABLE_SANITIZER_UNDEFINED_BEHAVIOR}
        ENABLE_CONTROL_FLOW_PROTECTION
        ENABLE_STACK_PROTECTION
        ENABLE_OVERFLOW_PROTECTION
        ENABLE_INCLUDE_WHAT_YOU_USE
        # XXX NO! WARNINGS_AS_ERRORS
    )

    target_link_libraries(
        ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_NAME}_project_warnings>
                               $<BUILD_INTERFACE:${PROJECT_NAME}_project_options>
    )
else()
    include(cmake/WarningsAsErrors.cmake)
endif()

#---------------------------------------------------------------------------------------
# Install and export tftpd-targets
#---------------------------------------------------------------------------------------
if(NOT CMAKE_SKIP_INSTALL_RULES)
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
        DEPENDENCIES "Boost 1.81"
    )
    # NOTE: implicit done! add_library(tftpd::tftpd ALIAS tftpd)
    install(FILES async_tftpd_server.hpp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    if(BUILD_SHARED_LIBS)
        install(IMPORTED_RUNTIME_ARTIFACTS ${PROJECT_NAME} RUNTIME_DEPENDENCY_SET
                _dependency_set
        )
        install(
            RUNTIME_DEPENDENCY_SET
            _dependency_set
            POST_EXCLUDE_REGEXES
            "${CMAKE_INSTALL_PREFIX}/lib"
            RUNTIME
            DESTINATION
            lib
        )
    endif()

    include(CPack)
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

    if(UNIX)
        add_test(
            NAME tftpd_test
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test.sh
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        )
    endif()

endif()
