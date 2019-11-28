#include "async_tftpd_server.hpp"

#include "tftpd.hpp"

std::string tftpd::receive_file(short port)
{
    asio::io_context io_context;
    tftpd::receiver s(io_context, port);

    io_context.run(); // the server runs here ...

    return s.get_filename();
}
