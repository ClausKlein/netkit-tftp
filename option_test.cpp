#ifdef NDEBUG
#    undef NDEBUG
#endif

#include "tftpd.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

int main()
{
    using namespace std::string_literals;

    try {

        std::vector<char> ackbuf;
        std::string path;
        FILE *fp{nullptr};

        const char corrupt[]{"invalid data block"};
        int err = tftpd::tftp(std::vector<char>(corrupt, corrupt + sizeof(corrupt)), fp, path, ackbuf);
        assert(err);
        assert(tftpd::g_segsize == SEGSIZE);
        assert(path.empty());
        assert(ackbuf.empty());
        assert(fp == nullptr);

        std::string test1 = {"\0\2testfile.dat\0octet\0"s
                             "blksize\0"s
                             "1047\0"s
                             "blksize2\0"s
                             "1234\0"s
                             "rollover\0"s
                             "1\0"s
                             "timeout\0"s
                             "1\0"s
                             "utimeout\0"s
                             "33333\0"s
                             "tsize\0"s
                             "12345678910\0"s};
        std::vector<char> msg(test1.begin(), test1.end());
        msg.resize(PKTSIZE);
        err = tftpd::tftp(msg, fp, path, ackbuf);
        std::cout << path << " segsize:" << tftpd::g_segsize << " tsize:" << tftpd::g_tsize
                  << " timeout: " << tftpd::g_timeout << std::endl;
        assert(!err);
        assert(!ackbuf.empty());
        assert(tftpd::g_segsize == 1047);
        assert(tftpd::g_tsize == 12345678910);
        assert(tftpd::g_timeout == 33); // NOTE: ms

        std::string test2 = {"\0\2testfile.dat\0octet\0"s
                             "timeout\0"s
                             "2\0"s
                             "blksize2\0"s
                             "65464\0"s};
        err = tftpd::tftp(std::vector<char>(test2.begin(), test2.end()), fp, path, ackbuf);
        std::cout << path << " segsize:" << tftpd::g_segsize << " tsize:" << tftpd::g_tsize
                  << " timeout: " << tftpd::g_timeout << std::endl;
        assert(!err);
        assert(tftpd::g_segsize == (1 << 15));
        assert(tftpd::g_tsize == 0);
        assert(tftpd::g_timeout == 2000); // NOTE: ms

        std::string test3 = {"\0\2testfile.dat\0octet\0"s
                             "blksize2\0"s
                             "1234\0"s
                             "utimeout\0"s
                             "10000\0"s}; // us!
        err = tftpd::tftp(std::vector<char>(test3.begin(), test3.end()), fp, path, ackbuf);
        std::cout << path << " segsize:" << tftpd::g_segsize << " tsize:" << tftpd::g_tsize
                  << " timeout: " << tftpd::g_timeout << std::endl;
        assert(!err);
        assert(tftpd::g_segsize == 1024);
        assert(tftpd::g_timeout == 10); // NOTE: ms

        std::string test4 = {"\0\2minimal.dat\0octet\0"s
                             "blksize\0"s
                             "777777\0"s
                             "blksize\0"s
                             "0\0"s
                             "timeout\0"s
                             "333\0"s
                             "timeout\0"s
                             "0\0"s
                             "tsize\0"s
                             "NoNumber\0"s
                             "utimeout\0"s
                             "999\0"s}; // us!
        err = tftpd::tftp(std::vector<char>(test4.begin(), test4.end()), fp, path, ackbuf);
        std::cout << path << " segsize:" << tftpd::g_segsize << " tsize:" << tftpd::g_tsize
                  << " timeout: " << tftpd::g_timeout << std::endl;
        assert(!err);
        assert(!ackbuf.empty());
        assert(tftpd::g_segsize == MAXSEGSIZE);
        assert(tftpd::g_timeout == 1000); // NOTE: ms

        const char unknown[] = {"\0\1unknown_mode.dat\0netascii\0"};
        err = tftpd::tftp(std::vector<char>(unknown, unknown + sizeof(unknown)), fp, path, ackbuf);
        assert(err);
        assert(tftpd::g_segsize == SEGSIZE);
        assert(!path.empty());
        assert(ackbuf.empty());

        const char missing[] = {"\0\1missing_mode.dat\0"};
        err = tftpd::tftp(std::vector<char>(missing, missing + sizeof(missing)), fp, path, ackbuf);
        assert(ackbuf.empty());
        assert(err);

        err = tftpd::tftp(std::vector<char>(missing, missing + sizeof(missing) - 3), fp, path, ackbuf);
        assert(ackbuf.empty());
        assert(err);

    } catch (std::exception &e) {
        std::cerr << "Exception: " << e.what() << "\n\n";
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
