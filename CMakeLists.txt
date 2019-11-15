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

set(BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin")
set(SBIN_DIR "${CMAKE_INSTALL_PREFIX}/sbin")
set(MAN_DIR "${CMAKE_INSTALL_PREFIX}/share/man")

add_subdirectory(tftp)
add_subdirectory(tftpd)

add_executable(tftpd_test tftpd.cpp tftpsubs.cpp)
target_link_libraries(tftpd_test asio::standalone Threads::Threads)
