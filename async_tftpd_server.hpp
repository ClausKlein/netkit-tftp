#pragma once

#include <string>

// NOTE: prevent clang-tidy warings XXX #include <boost/current_function.hpp>
#define BOOST_CURRENT_FUNCTION static_cast<const char *>(__PRETTY_FUNCTION__)

namespace tftpd {

/// the only directory used by the tftpd
///
/// @note this can't be changed!
/// @attention it has to be created before server is started!
constexpr const char *default_dirs[]{"/tmp/tftpboot", ""};

/// receive 1 file with tftp protocol
///
/// @param port the UDP port used by tftpd
/// @return path to file received or
/// @throw std::exception on error
std::string receive_file(short port = 69);

} // namespace tftpd
