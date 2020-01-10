#include "async_tftpd_server.hpp"

#include "tftpd.hpp"

#include <stdlib.h> // system() used

const char *tftpd::g_rootdir = "/tmp/tftpboot"; // the only tftp root dir used!
std::function<void(size_t)> tftpd::g_callback = nullptr;

std::string tftpd::receive_file(const char *rootdir, short port, std::function<void(size_t)> callback)
{
    asio::io_context io_context;
    tftpd::g_rootdir = rootdir;
    tftpd::g_callback = std::move(callback);
    tftpd::receiver s(io_context, port);

    // make sure the rootdir exists
    std::string mkdir("/usr/bin/mkdir -p ");
    mkdir.append(rootdir);
    (void) system(mkdir.c_str());   // we hope the best and continue! CK

    io_context.run(); // the server runs here ...

    return s.get_filename();
}
