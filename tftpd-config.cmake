include(CMakeFindDependencyMacro)

find_dependency(Threads)
find_dependency(asio 1.14.1)
find_dependency(Boost 1.67.0 COMPONENTS filesystem)

if(NOT TARGET tftpd::tftpd)
    include(${CMAKE_CURRENT_LIST_DIR}/tftpd-targets.cmake)
endif()
