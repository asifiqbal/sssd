/*
    Authors:
        Pavel Březina <pbrezina@redhat.com>

    Copyright (C) 2016 Red Hat

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

#ifndef _CACHE_REQ_H_
#define _CACHE_REQ_H_

#include "util/util.h"
#include "confdb/confdb.h"
#include "responder/common/negcache.h"

enum cache_req_type {
    CACHE_REQ_USER_BY_NAME,
    CACHE_REQ_USER_BY_UPN,
    CACHE_REQ_USER_BY_ID,
    CACHE_REQ_USER_BY_CERT,
    CACHE_REQ_USER_BY_FILTER,

    CACHE_REQ_GROUP_BY_NAME,
    CACHE_REQ_GROUP_BY_ID,
    CACHE_REQ_GROUP_BY_FILTER,

    CACHE_REQ_INITGROUPS,
    CACHE_REQ_INITGROUPS_BY_UPN,

    CACHE_REQ_OBJECT_BY_SID,

    CACHE_REQ_ENUM_USERS,
    CACHE_REQ_ENUM_GROUPS,

    CACHE_REQ_SENTINEL
};

/*
 * Request optimization of saving the data provider results. The data provider
 * might "downgrade" the optimization for example if the back end doesn't
 * support modifyTimestamps, but never "upgrade" it to more aggressive.
 */
enum dp_req_opt_level {
    /*
     * Never optimize anything, always save all data in both the synchronous
     * cache and the timestamp cache. Suitable for authentication lookups
     * such as initgroups from the PAM responder
     */
    DP_REQ_OPT_NONE,
    /*
     * Compare the returned attribute values with what is stored in the
     * synchronous cache. Only update the timestamp cache if none of the
     * attributes differ
     */
    DP_REQ_OPT_ATTR_VAL,
    /* Only update the timestamp cache if the modifyTimestamp attribute values
     * are the same between the cached object and the remote object. If the
     * modstamp value differs, compare the attribute values as if
     * CREQ_OPT_ATTR_VAL was selected
     */
    DP_REQ_OPT_MODSTAMP,
};

/* Input data. */

struct cache_req_data;

struct cache_req_data *
cache_req_data_name(TALLOC_CTX *mem_ctx,
                    enum cache_req_type type,
                    enum dp_req_opt_level dp_optimize_level,
                    const char *name);

struct cache_req_data *
cache_req_data_id(TALLOC_CTX *mem_ctx,
                  enum cache_req_type type,
                  enum dp_req_opt_level dp_optimize_level,
                  uint32_t id);

struct cache_req_data *
cache_req_data_cert(TALLOC_CTX *mem_ctx,
                    enum cache_req_type type,
                    enum dp_req_opt_level dp_optimize_level,
                    const char *cert);

struct cache_req_data *
cache_req_data_sid(TALLOC_CTX *mem_ctx,
                   enum cache_req_type type,
                   enum dp_req_opt_level dp_optimize_level,
                   const char *sid,
                   const char **attrs);

struct cache_req_data *
cache_req_data_enum(TALLOC_CTX *mem_ctx,
                    enum cache_req_type type,
                    enum dp_req_opt_level dp_optimize_level);

/* Output data. */

struct cache_req_result {
    /**
     * SSSD domain where the result was obtained.
     */
    struct sss_domain_info *domain;

    /**
     * Result from ldb lookup.
     */
    struct ldb_result *ldb_result;

    /**
     * Shortcuts into ldb_result. This shortens the code a little since
     * callers usually don't don't need to work with ldb_result directly.
     */
    unsigned int count;
    struct ldb_message **msgs;

    /**
     * If name was used as a lookup parameter, @lookup_name contains name
     * normalized to @domain rules.
     */
    const char *lookup_name;
};

/**
 * Shallow copy of cache request result, limiting the result to a maximum
 * numbers of records.
 */
struct cache_req_result *
cache_req_copy_limited_result(TALLOC_CTX *mem_ctx,
                              struct cache_req_result *result,
                              uint32_t start,
                              uint32_t limit);

/* Generic request. */
struct tevent_req *cache_req_send(TALLOC_CTX *mem_ctx,
                                  struct tevent_context *ev,
                                  struct resp_ctx *rctx,
                                  struct sss_nc_ctx *ncache,
                                  int midpoint,
                                  const char *domain,
                                  struct cache_req_data *data);

errno_t cache_req_recv(TALLOC_CTX *mem_ctx,
                       struct tevent_req *req,
                       struct cache_req_result ***_result);

errno_t cache_req_single_domain_recv(TALLOC_CTX *mem_ctx,
                                     struct tevent_req *req,
                                     struct cache_req_result **_result);

/* Plug-ins. */

struct tevent_req *
cache_req_user_by_name_send(TALLOC_CTX *mem_ctx,
                            struct tevent_context *ev,
                            struct resp_ctx *rctx,
                            struct sss_nc_ctx *ncache,
                            int cache_refresh_percent,
                            enum dp_req_opt_level dp_optimize_level,
                            const char *domain,
                            const char *name);

#define cache_req_user_by_name_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_user_by_id_send(TALLOC_CTX *mem_ctx,
                          struct tevent_context *ev,
                          struct resp_ctx *rctx,
                          struct sss_nc_ctx *ncache,
                          int cache_refresh_percent,
                          enum dp_req_opt_level dp_optimize_level,
                          const char *domain,
                          uid_t uid);

#define cache_req_user_by_id_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result);

struct tevent_req *
cache_req_user_by_cert_send(TALLOC_CTX *mem_ctx,
                            struct tevent_context *ev,
                            struct resp_ctx *rctx,
                            struct sss_nc_ctx *ncache,
                            int cache_refresh_percent,
                            enum dp_req_opt_level dp_optimize_level,
                            const char *domain,
                            const char *pem_cert);

#define cache_req_user_by_cert_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_group_by_name_send(TALLOC_CTX *mem_ctx,
                             struct tevent_context *ev,
                             struct resp_ctx *rctx,
                             struct sss_nc_ctx *ncache,
                             int cache_refresh_percent,
                             enum dp_req_opt_level dp_optimize_level,
                             const char *domain,
                             const char *name);

#define cache_req_group_by_name_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_group_by_id_send(TALLOC_CTX *mem_ctx,
                           struct tevent_context *ev,
                           struct resp_ctx *rctx,
                           struct sss_nc_ctx *ncache,
                           int cache_refresh_percent,
                           enum dp_req_opt_level dp_optimize_level,
                           const char *domain,
                           gid_t gid);

#define cache_req_group_by_id_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_initgr_by_name_send(TALLOC_CTX *mem_ctx,
                              struct tevent_context *ev,
                              struct resp_ctx *rctx,
                              struct sss_nc_ctx *ncache,
                              int cache_refresh_percent,
                              enum dp_req_opt_level dp_optimize_level,
                              const char *domain,
                              const char *name);

#define cache_req_initgr_by_name_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_user_by_filter_send(TALLOC_CTX *mem_ctx,
                              struct tevent_context *ev,
                              struct resp_ctx *rctx,
                              enum dp_req_opt_level dp_optimize_level,
                              const char *domain,
                              const char *filter);

#define cache_req_user_by_filter_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_group_by_filter_send(TALLOC_CTX *mem_ctx,
                              struct tevent_context *ev,
                              struct resp_ctx *rctx,
                              enum dp_req_opt_level dp_optimize_level,
                              const char *domain,
                              const char *filter);

#define cache_req_group_by_filter_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_object_by_sid_send(TALLOC_CTX *mem_ctx,
                             struct tevent_context *ev,
                             struct resp_ctx *rctx,
                             struct sss_nc_ctx *ncache,
                             int cache_refresh_percent,
                             enum dp_req_opt_level dp_optimize_level,
                             const char *domain,
                             const char *sid,
                             const char **attrs);

#define cache_req_object_by_sid_recv(mem_ctx, req, _result) \
    cache_req_single_domain_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_enum_users_send(TALLOC_CTX *mem_ctx,
                          struct tevent_context *ev,
                          struct resp_ctx *rctx,
                          struct sss_nc_ctx *ncache,
                          int cache_refresh_percent,
                          enum dp_req_opt_level dp_optimize_level,
                          const char *domain);

#define cache_req_enum_users_recv(mem_ctx, req, _result) \
    cache_req_recv(mem_ctx, req, _result)

struct tevent_req *
cache_req_enum_groups_send(TALLOC_CTX *mem_ctx,
                          struct tevent_context *ev,
                          struct resp_ctx *rctx,
                          struct sss_nc_ctx *ncache,
                          int cache_refresh_percent,
                          enum dp_req_opt_level dp_optimize_level,
                          const char *domain);

#define cache_req_enum_groups_recv(mem_ctx, req, _result) \
    cache_req_recv(mem_ctx, req, _result)

#endif /* _CACHE_REQ_H_ */
