/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    This product includes software developed by the University of
 *    California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

char copyright[] = "@(#) Copyright (c) 1983 Regents of the University of California.\n"
                   "All rights reserved.\n";

/*
 * From: @(#)tftpd.c    5.13 (Berkeley) 2/26/91
 */
// char rcsid[] = "$Id: tftpd.c,v 1.20 2000/07/29 18:37:21 dholland Exp $";

/*
 * This version includes many modifications by Jim Guyton <guyton@rand-unix>
 */

#include "async_tftpd_server.hpp"
#include "tftp/tftpsubs.h"

#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <arpa/inet.h>
#include <cstdlib>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <vector>

namespace tftpd {
extern const char *g_rootdir; // the only tftp root dir used!

void init_opt();
void do_opt(const char *opt, const char *val, char **ackbuf_ptr);
int validate_access(std::string &filename, int mode, FILE *&file);
int tftp(const std::vector<char> &rxbuffer, FILE *&file, std::string &file_path, std::vector<char> &optack);

/// the only directory used by the tftpd
///
/// @note this can't be changed!
/// @attention it has to be created before server is started!
constexpr const char *default_dirs[]{"/tmp/tftpboot", ""};
const char *const *dirs = static_cast<const char *const *>(default_dirs);

// Avoid storms of naks to a RRQ broadcast for a relative bootfile pathname from a diskless Sun.
constexpr bool suppress_error{false};
// Change root directory on startup. This means the remote host does not need to pass along the directory as part of the
// transfer, and may add security.
constexpr bool secure_tftp{true};
// Allow new files to be created. Normaly, tftpd will only allow upload of files that already exist.
constexpr bool allow_create{true};

struct formats
{
    const char *f_mode;
    // XXX int (*f_validate)(const char *, int);
    // XXX int (*f_send)(struct formats *);
    // XXX int (*f_recv)(struct formats *);
    bool f_convert;
};
static struct formats formats[] = { // XXX {"netascii", /* validate_access, sendfile, recvfile, */ true},
    {"octet", /* validate_access, sendfile, recvfile, */ false},
    {nullptr, false}};

/*
 * Handle initial connection protocol.
 */
int tftp(const std::vector<char> &rxbuffer, FILE *&file, std::string &file_path, std::vector<char> &optack)
{
    syslog(LOG_NOTICE, "%s(%lu)\n", BOOST_CURRENT_FUNCTION, rxbuffer.size());
    init_opt();

    assert(rxbuffer.size() >= TFTP_HEADER);

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
    struct tftphdr *tp = (struct tftphdr *)(rxbuffer.data());
    u_short th_opcode = ntohs(tp->th_opcode);
    if ((th_opcode != RRQ) && (th_opcode != WRQ)) {
        syslog(LOG_ERR, "tftpd: invalid opcode request!\n");
        return (EBADID);
    }

    const char *cp = nullptr;
    const char *mode = nullptr;
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
    const char *filename = cp = static_cast<const char *>(tp->th_stuff);
    const char *val = nullptr;
    const char *opt = nullptr;

    optack.resize(PKTSIZE);
    char *pktbuf = optack.data();
    char *ap = pktbuf + 2;
    ((struct tftphdr *)pktbuf)->th_opcode = htons(OACK); // NOLINT

    int argn = 0;
    const struct formats *pf = nullptr;
    const char *end = rxbuffer.data() + rxbuffer.size();

    while (cp < end && *cp != 0) {
        do {
            cp++;
        } while (cp < end && *cp != 0);

        if (*cp != 0) {
            optack.clear();
            syslog(LOG_ERR, "tftpd: Request not null-terminated");
            return EBADOP;
        }

        argn++;
        if (argn == 1) {
            mode = ++cp;
        } else if (argn == 2) {
            std::string l_mode(mode);
            boost::to_lower(l_mode);
            for (pf = formats; pf->f_mode != nullptr; pf++) {
                if (l_mode == pf->f_mode) {
                    break;
                }
            }
            if (pf->f_mode == nullptr) {
                optack.clear();
                syslog(LOG_ERR, "tftpd: Unknown or not supported mode");
                return EBADOP;
            }

            // NOTE: set g_tsize and tsize_ok flag in case of RRQ (unsupported yet)! CK
            file_path = filename;
            int ecode = validate_access(file_path, th_opcode, file);
            if (ecode != 0) {
                optack.clear();
                if (suppress_error && *filename != '/' && ecode == ENOTFOUND) {
                    syslog(LOG_WARNING, "tftpd: Deny to access file: %s\n", filename);
                    return 0; // OK
                }
                return (ecode);
            }

            opt = ++cp;
        } else if ((argn & 1) != 0) {
            val = ++cp; // NOTE: odd arg has to be the value
        } else {
            do_opt(opt, val, &ap);
            opt = ++cp;
        }
    }

    size_t ack_length = ap - optack.data();
    if (argn == 2) {
        optack.clear();
        syslog(LOG_NOTICE, "tftpd: Request has no options");
    }

    if (th_opcode == RRQ) {
        optack.clear();
        // NOTE: NEVER! (*pf->f_send)(pf); // sendfile() CK
        syslog(LOG_WARNING, "tftpd: Only upload supported!\n");
        return (EBADOP);
    }

    if (argn > 2) {
        optack.resize(ack_length);
    }
    return 0; // OK
}

/*
 * Validate file access.
 *
 * Since we have no uid or gid, for now require file to exist and be
 * publicly readable/writable.  If we were invoked with arguments from inetd
 * then the file must also be in one of the given directory prefixes.
 * Note also, full path name must be given as we have no login directory.
 */
int validate_access(std::string &filename, int mode, FILE *&file)
{
    using boost::algorithm::starts_with;

    struct stat stbuf = {};
    int fd = 0;
    const char *const *dirp = nullptr;

    syslog(LOG_NOTICE, "tftpd: Validate access to file: %s\n", filename.c_str());

    /*
     * prevent tricksters from getting around the directory restrictions
     */
    if (starts_with(filename, "../")) {
        syslog(LOG_WARNING, "tftpd: Blocked illegal request for %s\n", filename.c_str());
        return EACCESS;
    }

    if (filename.find("/../") != std::string::npos) {
        syslog(LOG_WARNING, "tftpd: Blocked illegal request for %s\n", filename.c_str());
        return (EACCESS);
    }

    if (secure_tftp || filename[0] != '/') {
        syslog(LOG_NOTICE, "tftpd: Check file access at %s\n", g_rootdir);
        if (chdir(g_rootdir) < 0) {
            syslog(LOG_WARNING, "tftpd: chdir: %s\n", strerror(errno));
            return (EACCESS);
        }

        while (filename[0] == '/') {
            filename = filename.substr(1);
        }
        filename = std::string(g_rootdir) + "/" + filename;
    } else {
        // NOLINTNEXTLINE
        for (dirp = dirs; *dirp != 0; dirp++) {
            if (starts_with(filename, *dirp)) {
                break;
            }
        }
        if (*dirp == nullptr && dirp != dirs) {
            syslog(LOG_WARNING, "tftpd: invalid root dir!\n");
            return (EACCESS);
        }
        syslog(LOG_NOTICE, "tftpd: Check access to file %s\n", filename.c_str());
    }

    if (stat(filename.c_str(), &stbuf) < 0) {
        // stat error, no such file or no read access
        if (mode == RRQ && secure_tftp) {
            syslog(LOG_WARNING, "tftpd: File not found %s\n", filename.c_str());
            return (errno == ENOENT ? ENOTFOUND : EACCESS);
        }
    }

#if 0
    /*
     * The idea is that symlinks are dangerous. However, a symlink
     * in the tftp area has to have been put there by root, and it's
     * not part of the philosophy of Unix to keep root from shooting
     * itself in the foot if it tries to. So basically we assume if
     * there are symlinks they're there on purpose and not pointing
     * to /etc/passwd or /tmp or other dangerous places.
     *
     * Note if this gets turned on the stat above needs to be made
     * an lstat, or the check is useless.
     */
    /* symlinks prohibited */
    if (S_ISLNK(stbuf.st_mode)) {
        return (EACCESS);
    }
#endif

    std::string tmpname = filename;
    if (mode == RRQ) {
        if ((stbuf.st_mode & S_IROTH) == 0) {
            syslog(LOG_WARNING, "tftpd: File has not S_IROTH set\n");
            return (EACCESS);
        }

    } else if (!allow_create) {
        if ((stbuf.st_mode & S_IWOTH) == 0) {
            syslog(LOG_WARNING, "tftpd: File has not S_IWOTH set\n");
            return (EACCESS);
        }
    } else {
        tmpname.append(".upload");
    }

    fd = open(tmpname.c_str(), (mode == RRQ ? O_RDONLY : (O_WRONLY | O_TRUNC | O_CREAT)), 0666);
    if (fd < 0) {
        return (errno + ERRNO_OFFSET);
    }
    file = fdopen(fd, (mode == RRQ) ? "r" : "w");
    if (file == nullptr) {
        return errno + ERRNO_OFFSET;
    }

    syslog(LOG_NOTICE, "tftpd: successfully open file: %s\n", tmpname.c_str());
    return 0; // OK
}

} // namespace tftpd
