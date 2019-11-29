// NOTE: example and test helper only! CK
#include "async_tftpd_server.hpp"

#include <iostream>
#include <unistd.h>

int main(int argc, char *argv[])
{
    try {
        if (argc != 2) {
            std::cerr << "Usage: tftpd <port>\n\n";
            return 0; // OK
        }

        short port = std::strtol(argv[1], nullptr, 10);
        auto filename = tftpd::receive_file("/tmp/tftpboot", port);
        if (!filename.empty()) {
            std::cout << "Successfully received: " << filename << "\n\n";
        }
    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n\n";
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
