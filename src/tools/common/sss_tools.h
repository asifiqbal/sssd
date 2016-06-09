/*
    Authors:
        Pavel Březina <pbrezina@redhat.com>

    Copyright (C) 2015 Red Hat

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

#ifndef _SSS_TOOLS_H_
#define _SSS_TOOLS_H_

#include <talloc.h>
#include <popt.h>

#include "confdb/confdb.h"

struct sss_tool_ctx {
    struct confdb_ctx *confdb;

    char *default_domain;
    struct sss_domain_info *domains;
};

struct sss_tool_ctx *sss_tool_init(TALLOC_CTX *mem_ctx,
                                   int *argc, const char **argv);

struct sss_cmdline;

typedef errno_t
(*sss_route_fn)(struct sss_cmdline *cmdline,
                struct sss_tool_ctx *tool_ctx,
                void *pvt);

#define SSS_TOOL_COMMAND(cmd, msg, fn) {cmd, _(msg), fn}
#define SSS_TOOL_DELIMITER(message) {"", (message), NULL}

struct sss_route_cmd {
    const char *command;
    const char *description;
    sss_route_fn fn;
};

void sss_tool_usage(const char *tool_name,
                    struct sss_route_cmd *commands);

errno_t sss_tool_route(int argc, const char **argv,
                       struct sss_tool_ctx *tool_ctx,
                       struct sss_route_cmd *commands,
                       void *pvt);

typedef errno_t (*sss_popt_fn)(poptContext pc, char option, void *pvt);

enum sss_tool_opt {
    SSS_TOOL_OPT_REQUIRED,
    SSS_TOOL_OPT_OPTIONAL
};

errno_t sss_tool_popt_ex(struct sss_cmdline *cmdline,
                         struct poptOption *options,
                         enum sss_tool_opt require_option,
                         sss_popt_fn popt_fn,
                         void *popt_fn_pvt,
                         const char *fopt_name,
                         const char *fopt_help,
                         const char **_fopt,
                         bool *_opt_set);

errno_t sss_tool_popt(struct sss_cmdline *cmdline,
                      struct poptOption *options,
                      enum sss_tool_opt require_option,
                      sss_popt_fn popt_fn,
                      void *popt_fn_pvt);

int sss_tool_main(int argc, const char **argv,
                  struct sss_route_cmd *commands,
                  void *pvt);

errno_t sss_tool_parse_name(TALLOC_CTX *mem_ctx,
                            struct sss_tool_ctx *tool_ctx,
                            const char *input,
                            const char **_username,
                            struct sss_domain_info **_domain);

#endif /* SRC_TOOLS_COMMON_SSS_TOOLS_H_ */
