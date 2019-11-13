cmake_minimum_required(VERSION 3.7)
project(netkit-tftp)

set(BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin")
set(SBIN_DIR "${CMAKE_INSTALL_PREFIX}/sbin")
set(MAN_DIR "${CMAKE_INSTALL_PREFIX}/share/man")

add_subdirectory(tftp)
add_subdirectory(tftpd)
