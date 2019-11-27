#include "tftpd.hpp"

int main(int argc, char *argv[])
{
    try {
        if (argc != 2) {
            std::cerr << "Usage: tftpd <port>\n";
            return 0; // OK
        }

        asio::io_context io_context;
        tftpd::receiver s(io_context, std::strtol(argv[1], nullptr, 10));

        io_context.run();

        auto filename = s.get_filename();
        if (!filename.empty()) {
            std::cout << "Successfully received: " << filename << "\n";
        }
    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n";
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
