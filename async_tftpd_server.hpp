#pragma once

#include <functional>
#include <string>

// NOTE: prevent clang-tidy warings XXX #include <boost/current_function.hpp>
#define BOOST_CURRENT_FUNCTION static_cast<const char *>(__PRETTY_FUNCTION__)
#define BOOST_THROW_EXCEPTION_CURRENT_FUNCTION BOOST_CURRENT_FUNCTION

namespace tftpd {

/// receive 1 file with tftp protocol
///
/// @param port the UDP port used by tftpd
/// @param rootdir the tftp upload dir used
/// @note the rootdir must exist and world writable!
/// @return path to file received or
/// @throw std::exception on error
std::string receive_file(const char *rootdir = "/srv/tftp", short port = 69,
                         std::function<void(size_t)> callback = nullptr);

} // namespace tftpd
