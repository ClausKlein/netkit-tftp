cmake_minimum_required(VERSION 3.13)

list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/Workspace/cmake)
list(APPEND CMAKE_MODULE_PATH $ENV{HOME}/cmake)

include(before_project_setup OPTIONAL)
# ===============================
project(netkit-tftp VERSION 0.17)
# ===============================
include(build_options OPTIONAL)

find_package(Threads REQUIRED)
find_package(asio CONFIG REQUIRED)

# set(BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin")
# set(SBIN_DIR "${CMAKE_INSTALL_PREFIX}/sbin")
# set(MAN_DIR "${CMAKE_INSTALL_PREFIX}/share/man")

add_subdirectory(tftp)
# add_subdirectory(tftpd)

# add_executable(third_party_lib third_party_lib.cpp)
# target_link_libraries(third_party_lib asio::standalone Threads::Threads)

# add_executable(async_udp_echo_server async_udp_echo_server.cpp)
# target_link_libraries(async_udp_echo_server asio::standalone Threads::Threads)

enable_testing()
add_executable(tftpd_test tftpd_test.cpp tftpd.hpp tftpd_utils.cpp tftpsubs.cpp tftp/tftpsubs.h)
target_link_libraries(tftpd_test asio::standalone Threads::Threads)
## add_test(NAME tftpd_test COMMAND tftpd_test 1234)
add_test(NAME tftpd_test
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

