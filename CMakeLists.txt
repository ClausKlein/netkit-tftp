cmake_minimum_required(VERSION 3.13)

list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/Workspace/cmake)
list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/cmake)

include(before_project_setup OPTIONAL)
# ===============================
project(netkit-tftp VERSION 0.17)
# ===============================
include(build_options OPTIONAL)

include(GNUInstallDirs)

find_package(Threads REQUIRED)
find_package(asio CONFIG REQUIRED)

add_subdirectory(tftp)
add_subdirectory(tftpd)

# asio sample
# add_executable(third_party_lib third_party_lib.cpp)
# target_link_libraries(third_party_lib asio::standalone Threads::Threads)

# asio sample
# add_executable(async_udp_echo_server async_udp_echo_server.cpp)
# target_link_libraries(async_udp_echo_server asio::standalone Threads::Threads)

add_library(tftpd async_tftpd_server.cpp async_tftpd_server.hpp
            tftpd_utils.cpp tftpsubs.cpp tftp/tftpsubs.h
)
target_link_libraries(tftpd INTERFACE Threads::Threads)

#---------------------------------------------------------------------------------------
# Install tftpd-targets
#---------------------------------------------------------------------------------------
set(ConfigPackageLocation ${CMAKE_INSTALL_LIBDIR}/cmake/tftpd)
install(TARGETS tftpd EXPORT tftpd-targets DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES async_tftpd_server.hpp EXPORT tftpd-targets DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES tftpd-config.cmake DESTINATION ${ConfigPackageLocation})
install(EXPORT tftpd-targets DESTINATION ${ConfigPackageLocation} NAMESPACE tftpd::)

#---------------------------------------------------------------------------------------
# ctest
#---------------------------------------------------------------------------------------
enable_testing()

add_executable(tftpd_test tftpd_test.cpp async_tftpd_server.hpp)
target_link_libraries(tftpd_test tftpd asio::standalone)
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

