#pragma once

#include <functional>
#include <string>

namespace tftpd {

/// receive 1 file with tftp protocol
///
/// @param port the UDP port used by tftpd
/// @param rootdir the tftp upload dir used
/// @note the rootdir must exist and world writable!
/// @return path to file received or
/// @throw std::exception on error
std::string receive_file(const char *rootdir = "/srv/tftp", uint16_t port = 69,
                         std::function<void(size_t)> callback = nullptr);

} // namespace tftpd
