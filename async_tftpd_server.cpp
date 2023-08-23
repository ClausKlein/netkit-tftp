#include "async_tftpd_server.hpp"

#include "tftpd.hpp"

#include <boost/filesystem.hpp>

const char *tftpd::g_rootdir = "/tmp/tftpboot"; // the only tftp root dir used!
std::function<void(size_t)> tftpd::g_callback = nullptr;

std::string tftpd::receive_file(const char *rootdir, uint16_t port, std::function<void(size_t)> callback)
{
    boost::asio::io_context io_context;
    tftpd::g_rootdir = rootdir;
    tftpd::g_callback = std::move(callback);
    tftpd::receiver s(io_context, port);

    // make sure the rootdir exists

    // NOTE: Creation failure because path resolves to an existing directory is not be treated as an error.
    boost::filesystem::path const dir(rootdir);
    (void)boost::filesystem::create_directory(dir);

    io_context.run(); // the server runs here ...

    return s.get_filename();
}
