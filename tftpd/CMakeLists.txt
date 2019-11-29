add_executable(
    in.tftpd
    tftpd.c
    "${CMAKE_SOURCE_DIR}/tftp/tftpsubs.c"
    "${CMAKE_SOURCE_DIR}/tftp/tftpsubs.h"
)
target_include_directories(in.tftpd PRIVATE
    "${CMAKE_SOURCE_DIR}/tftp/"
)

# install(TARGETS in.tftpd DESTINATION ${CMAKE_INSTALL_SBIN_DIR})
# install(FILES tftpd.8 DESTINATION ${CMAKE_INSTALL_MAN_DIR}/man8/ RENAME in.tftpd.8)
# install(
#     CODE "execute_process( \
#     COMMAND ${CMAKE_COMMAND} -E create_symlink \
#     in.tftpd.8 \$ENV{DESTDIR}${CMAKE_INSTALL_MAN_DIR}/man8/tftpd.8 \)"
# )
