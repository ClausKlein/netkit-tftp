#include "async_tftpd_server.hpp"

#include "tftpd.hpp"

const char *tftpd::rootdir = "/tmp/tftpboot"; // the only tftp root dir used!
std::function<void(size_t)> tftpd::callback = nullptr;

std::string tftpd::receive_file(const char *rootdir, short port, std::function<void(size_t)> callback)
{
    asio::io_context io_context;
    tftpd::rootdir = rootdir;
    tftpd::callback = callback;
    tftpd::receiver s(io_context, port);

    io_context.run(); // the server runs here ...

    return s.get_filename();
}
