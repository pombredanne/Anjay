/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANJAY_SERVERS_H
#define ANJAY_SERVERS_H

#include <anjay/anjay.h>

#include "utils.h"
#include "sched.h"
#include "coap/stream.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct {
    anjay_oid_t oid;
    AVS_LIST(anjay_iid_t) instances;
} anjay_dm_cache_object_t;

typedef struct {
    int64_t lifetime_s;
    AVS_LIST(anjay_dm_cache_object_t) dm;
    anjay_binding_mode_t binding_mode;
} anjay_update_parameters_t;

typedef struct {
    AVS_LIST(const anjay_string_t) endpoint_path;
    struct timespec expire_time;
    anjay_update_parameters_t last_update_params;
} anjay_registration_info_t;

typedef enum {
    ANJAY_CONNECTION_DISABLED,
    ANJAY_CONNECTION_ONLINE,
    ANJAY_CONNECTION_QUEUE
} anjay_server_connection_mode_t;

typedef struct {
    /**
     * If queue mode is in use, this socket may be non-NULL, but closed (by
     * means of <c>avs_net_socket_close()</c>). Such closed socket still retains
     * some of its previous state (including the remote endpoint's hostname and
     * security keys etc.) in avs_commons' internal structures.
     *
     * This is used by <c>_anjay_connection_internal_ensure_online()</c> to
     * reconnect the socket if necessary.
     *
     * We cannot rely on reading the connection information from data model
     * instead, because it may be gone - for example when trying to De-register
     * from a server that has just been deleted by a Bootstrap Server.
     */
    avs_net_abstract_socket_t *socket;
    avs_net_resolved_endpoint_t preferred_endpoint;
    char last_local_port[ANJAY_MAX_URL_PORT_SIZE];
} anjay_server_connection_private_data_t;

typedef struct {
    anjay_server_connection_private_data_t conn_priv_data_;
#if defined(__GNUC__) \
        && !(defined(ANJAY_SERVERS_CONNECTION_INFO_C) || defined(ANJAY_TEST))
#pragma GCC poison conn_priv_data_
#endif

    bool needs_socket_update;

    bool queue_mode;

    /**
     * Sockets for queue mode connections are online for MAX_TRANSMIT_WAIT
     * (93 seconds) since last communication with the server.
     *
     * In Anjay, this is achieved by calling <c>queue_mode_activate_socket()</c>
     * from @ref _anjay_release_server_stream. This in turn (re)schedules
     * <c>queue_mode_close_socket()</c> to be called after MAX_TRANSMIT_WAIT.
     *
     * There is no explicit flag to denote online or suspended socket for queue
     * mode. This is because NULLness of this handle doubles as such a flag.
     * If this handle is non-NULL, it means that socket suspension is
     * scheduled - it is only natural that at that very moment the socket is
     * thus online. If it is NULL, it means that socket suspension is not
     * scheduled - and this means that it is already suspended.
     *
     * That's why checking
     * <c>queue_mode_close_socket_clb_handle != NULL</c> is enough to check
     * whether a queue mode socket should be online. This is indeed what
     * <c>should_connection_be_online()</c> does. However,
     * <c>queue_mode_close_socket()</c> actually closes the socket as well - so
     * it needs to be reconnected. That's why
     * <c>_anjay_connection_get_prepared_socket()</c> calls
     * <c>_anjay_connection_internal_ensure_online()</c>.
     */
    anjay_sched_handle_t queue_mode_close_socket_clb_handle;
} anjay_server_connection_t;

typedef struct {
    anjay_ssid_t ssid; // or ANJAY_SSID_BOOTSTRAP

    anjay_server_connection_t udp_connection;

    anjay_registration_info_t registration_info;
    anjay_sched_handle_t sched_update_handle;
} anjay_active_server_info_t;

// inactive servers include administratively disabled ones
// as well as those which were unreachable at connect attempt
typedef struct {
    anjay_ssid_t ssid;
    anjay_sched_handle_t sched_reactivate_handle;
    bool needs_activation;
} anjay_inactive_server_info_t;

typedef struct {
    AVS_LIST(anjay_active_server_info_t) active;
    AVS_LIST(anjay_inactive_server_info_t) inactive;
    anjay_sched_handle_t reload_sockets_sched_job_handle;

    AVS_LIST(avs_net_abstract_socket_t *const) nonqueue_sockets;
} anjay_servers_t;

typedef enum {
    ANJAY_CONNECTION_UDP,
    // ANJAY_CONNECTION_SMS,
    ANJAY_CONNECTION_WILDCARD
} anjay_connection_type_t;

typedef struct {
    anjay_active_server_info_t *server;
    anjay_connection_type_t conn_type;
} anjay_connection_ref_t;

static inline anjay_servers_t
_anjay_servers_create(void) {
    return (anjay_servers_t){ NULL, NULL, NULL, NULL };
}

/**
 * Clears up the @p servers struct, sending De-Register messages for each
 * active server and releasing any allocated resources.
 */
void _anjay_servers_cleanup(anjay_t *anjay,
                            anjay_servers_t *servers);

/**
 * Returns an active server object associated with given @p socket .
 */
anjay_active_server_info_t *
_anjay_servers_find_by_udp_socket(anjay_servers_t *servers,
                                  avs_net_abstract_socket_t *socket);

/**
 * Returns an active server object for given SSID.
 *
 * NOTE: the bootstrap server is identified by the ANJAY_SSID_BOOTSTRAP
 * constant instead of its actual SSID.
 */
anjay_active_server_info_t *_anjay_servers_find_active(anjay_servers_t *servers,
                                                       anjay_ssid_t ssid);

int _anjay_schedule_reload_sockets(anjay_t *anjay);

int _anjay_schedule_socket_update(anjay_t *anjay,
                                  anjay_iid_t security_iid);

#ifdef WITH_BOOTSTRAP
/**
 * Returns true if the client has successfully registered to any non-bootstrap
 * server and its registration has not yet expired.
 */
bool _anjay_servers_is_connected_to_non_bootstrap(anjay_servers_t *servers);
#endif

int _anjay_schedule_server_reconnect(anjay_t *anjay,
                                     anjay_active_server_info_t *server);

anjay_binding_mode_t
_anjay_server_cached_binding_mode(const anjay_active_server_info_t *server);

avs_net_abstract_socket_t *
_anjay_connection_get_prepared_socket(anjay_t *anjay,
                                      anjay_active_server_info_t *server,
                                      anjay_server_connection_t *connection);

void _anjay_connection_suspend(anjay_connection_ref_t conn_ref);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_SERVERS_H
