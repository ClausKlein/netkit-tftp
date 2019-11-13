
add_executable(
    tftp
    tftp.c
    main.c
    tftpsubs.c
)
install(
    TARGETS tftp
    DESTINATION ${BIN_DIR}
)

install(
    FILES tftp.1
    DESTINATION ${MAN_DIR}/man1/
)

