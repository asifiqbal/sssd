/*
   SSSD

   tools_mc_util - interface to the memcache for userspace tools

   Copyright (C) Red Hat                                        2013

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <talloc.h>
#include <fcntl.h>

#include "util/util.h"
#include "tools/tools_util.h"
#include "util/mmap_cache.h"
#include "sss_client/sss_cli.h"

static errno_t sss_mc_set_recycled(int fd)
{
    uint32_t w = SSS_MC_HEADER_RECYCLED;
    struct sss_mc_header h;
    off_t offset;
    off_t pos;
    int ret;


    offset = MC_PTR_DIFF(&h.status, &h);

    pos = lseek(fd, offset, SEEK_SET);
    if (pos == -1) {
        /* What do we do now ? */
        return errno;
    }

    errno = 0;
    ret = sss_atomic_write_s(fd, (uint8_t *)&w, sizeof(h.status));
    if (ret == -1) {
        return errno;
    }

    if (ret != sizeof(h.status)) {
        /* Write error */
        return EIO;
    }

    return EOK;
}

errno_t sss_memcache_invalidate(const char *mc_filename)
{
    int mc_fd = -1;
    errno_t ret;
    errno_t pret;
    useconds_t t = 50000;
    int retries = 2;

    if (!mc_filename) {
        return EINVAL;
    }

    mc_fd = open(mc_filename, O_RDWR);
    if (mc_fd == -1) {
        ret = errno;
        if (ret == ENOENT) {
            DEBUG(SSSDBG_TRACE_FUNC,("Memory cache file %s "
                  "does not exist.\n", mc_filename));
            return EOK;
        } else {
            DEBUG(SSSDBG_CRIT_FAILURE, ("Unable to open file %s: %s\n",
                  mc_filename, strerror(ret)));
            return ret;
        }
    }

    ret = sss_br_lock_file(mc_fd, 0, 1, retries, t);
    if (ret == EACCES) {
        DEBUG(SSSDBG_TRACE_FUNC,
              ("File %s already locked by someone else.\n", mc_filename));
        goto done;
    } else if (ret != EOK) {
       DEBUG(SSSDBG_CRIT_FAILURE, ("Failed to lock file %s.\n", mc_filename));
       goto done;
    }
    /* Mark the mc file as recycled. */
    ret = sss_mc_set_recycled(mc_fd);
    if (ret != EOK) {
        DEBUG(SSSDBG_CRIT_FAILURE, ("Failed to mark memory cache file %s "
              "as recycled.\n", mc_filename));
        goto done;
    }

    ret = EOK;
done:
    if (mc_fd != -1) {
        /* Closing the file also releases the lock */
        close(mc_fd);

        /* Only unlink the file if invalidation was succesful */
        if (ret == EOK) {
            pret = unlink(mc_filename);
            if (pret == -1) {
                DEBUG(SSSDBG_MINOR_FAILURE,
                      ("Failed to unlink file %s. "
                       "Will be unlinked later by sssd_nss.\n"));
            }
        }
    }
    return ret;
}

static int clear_fastcache(bool *sssd_nss_is_off)
{
    int ret;
    ret = sss_memcache_invalidate(SSS_NSS_MCACHE_DIR"/passwd");
    if (ret != EOK) {
        if (ret == EACCES) {
            *sssd_nss_is_off = false;
            return EOK;
        } else {
            return ret;
        }
    }

    ret = sss_memcache_invalidate(SSS_NSS_MCACHE_DIR"/group");
    if (ret != EOK) {
        if (ret == EACCES) {
            *sssd_nss_is_off = false;
            return EOK;
        } else {
            return ret;
        }
    }

    *sssd_nss_is_off = true;
    return EOK;
}

errno_t sss_memcache_clear_all(void)
{
    errno_t ret;
    bool sssd_nss_is_off = false;
    FILE *clear_mc_flag;

    ret = clear_fastcache(&sssd_nss_is_off);
    if (ret != EOK) {
        DEBUG(SSSDBG_CRIT_FAILURE, ("Failed to clear caches.\n"));
        return EIO;
    }
    if (!sssd_nss_is_off) {
        /* sssd_nss is running -> signal monitor to invalidate fastcache */
        clear_mc_flag = fopen(SSS_NSS_MCACHE_DIR"/"CLEAR_MC_FLAG, "w");
        if (clear_mc_flag == NULL) {
            DEBUG(SSSDBG_CRIT_FAILURE,
                  ("Failed to create clear_mc_flag file. "
                   "Memory cache will not be cleared.\n"));
            return EIO;
        }
        ret = fclose(clear_mc_flag);
        if (ret != 0) {
            ret = errno;
            DEBUG(SSSDBG_CRIT_FAILURE,
                  ("Unable to close file descriptor: %s\n",
                   strerror(ret)));
            return EIO;
        }
        DEBUG(SSSDBG_TRACE_FUNC, ("Sending SIGHUP to monitor.\n"));
        ret = signal_sssd(SIGHUP);
        if (ret != EOK) {
            DEBUG(SSSDBG_CRIT_FAILURE,
                  ("Failed to send SIGHUP to monitor.\n"));
            return EIO;
        }
    }

    return EOK;
}

enum sss_tools_ent {
    SSS_TOOLS_USER,
    SSS_TOOLS_GROUP
};

static errno_t sss_mc_refresh_ent(const char *name, enum sss_tools_ent ent)
{
    enum sss_cli_command cmd;
    struct sss_cli_req_data rd;
    uint8_t *repbuf = NULL;
    size_t replen;
    enum nss_status nret;
    errno_t ret;

    cmd = SSS_CLI_NULL;
    switch (ent) {
        case SSS_TOOLS_USER:
            cmd = SSS_NSS_GETPWNAM;
            break;
        case SSS_TOOLS_GROUP:
            cmd = SSS_NSS_GETGRNAM;
            break;
    }

    if (cmd == SSS_CLI_NULL) {
        DEBUG(SSSDBG_OP_FAILURE, ("Unknown object %d to refresh\n", cmd));
        return EINVAL;
    }

    rd.data = name;
    rd.len = strlen(name) + 1;

    sss_nss_lock();
    nret = sss_nss_make_request(cmd, &rd, &repbuf, &replen, &ret);
    sss_nss_unlock();

    free(repbuf);
    if (nret != NSS_STATUS_SUCCESS && nret != NSS_STATUS_NOTFOUND) {
        return EIO;
    }

    return EOK;
}

errno_t sss_mc_refresh_user(const char *username)
{
    return sss_mc_refresh_ent(username, SSS_TOOLS_USER);
}

errno_t sss_mc_refresh_group(const char *groupname)
{
    return sss_mc_refresh_ent(groupname, SSS_TOOLS_GROUP);
}
