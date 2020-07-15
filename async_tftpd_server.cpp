#include "async_tftpd_server.hpp"

#include "tftpd.hpp"

#define USE_BOOST_FILESYSTEM
#ifdef USE_BOOST_FILESYSTEM
#    include <boost/filesystem.hpp>
#else
#    include <stdlib.h> // system() used
#endif

const char *tftpd::g_rootdir = "/tmp/tftpboot"; // the only tftp root dir used!
std::function<void(size_t)> tftpd::g_callback = nullptr;

std::string tftpd::receive_file(const char *rootdir, short port, std::function<void(size_t)> callback)
{
    asio::io_context io_context;
    tftpd::g_rootdir = rootdir;
    tftpd::g_callback = std::move(callback);
    tftpd::receiver s(io_context, port);

    // make sure the rootdir exists

#ifdef USE_BOOST_FILESYSTEM
    // NOTE: Creation failure because path resolves to an existing directory is not be treated as an error.
    boost::filesystem::path dir(rootdir);
    (void)boost::filesystem::create_directory(dir);
#else
    std::string mkdir("/bin/mkdir -p ");
    mkdir.append(rootdir);
    (void)system(mkdir.c_str()); // we hope the best and continue! CK
#endif

    io_context.run(); // the server runs here ...

    return s.get_filename();
}
