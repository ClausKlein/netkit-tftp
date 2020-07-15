find_package(Threads REQUIRED)
find_package(Boost 1.67.0 REQUIRED COMPONENTS filesystem)

include(${CMAKE_CURRENT_LIST_DIR}/tftpd-targets.cmake)
