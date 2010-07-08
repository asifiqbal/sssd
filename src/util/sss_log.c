/*
    SSSD

    sss_log.c

    Authors:
        Stephen Gallagher <sgallagh@redhat.com>

    Copyright (C) 2010 Red Hat

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

#include "util/util.h"
#include <syslog.h>

static int sss_to_syslog(int priority)
{
    switch(priority) {
    case SSS_LOG_EMERG:
        return LOG_EMERG;
    case SSS_LOG_ALERT:
        return LOG_ALERT;
    case SSS_LOG_CRIT:
        return LOG_CRIT;
    case SSS_LOG_ERR:
        return LOG_ERR;
    case SSS_LOG_WARNING:
        return LOG_WARNING;
    case SSS_LOG_NOTICE:
        return LOG_NOTICE;
    case SSS_LOG_INFO:
        return LOG_INFO;
    case SSS_LOG_DEBUG:
        return LOG_DEBUG;
    default:
        /* If we've been passed an invalid priority, it's
         * best to assume it's an emergency.
         */
        return LOG_EMERG;
    }
}

void sss_log(int priority, const char *format, ...)
{
    va_list ap;
    int syslog_priority;

    syslog_priority = sss_to_syslog(priority);

    openlog(debug_prg_name, 0, LOG_DAEMON);

    va_start(ap, format);
    vsyslog(syslog_priority, format, ap);
    va_end(ap);

    closelog();
}
