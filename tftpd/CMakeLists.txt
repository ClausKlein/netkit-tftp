
include_directories(
    BEFORE
    "${CMAKE_SOURCE_DIR}/tftp/"
)

add_executable(
    in.tftpd
    tftpd.c
    "${CMAKE_SOURCE_DIR}/tftp/tftpsubs.c"
)
install(
    TARGETS in.tftpd
    DESTINATION ${SBIN_DIR}
)

install(
    FILES tftpd.8
    DESTINATION ${MAN_DIR}/man8/
    RENAME in.tftpd.8
)
INSTALL(
    CODE "execute_process( \
        COMMAND ${CMAKE_COMMAND} -E create_symlink \
        in.tftpd.8 \$ENV{DESTDIR}${MAN_DIR}/man8/tftpd.8 \
    )"
)
