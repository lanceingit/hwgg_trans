#pragma once

#include "espconn.h"

typedef void net_conn_cb(void* param);

typedef enum {
    DNS_STATE_NO_FOUND,
    DNS_STATE_FOUND,
    DNS_STATE_FOUNDING,
} DnsState;

void net_init(net_conn_cb* cb);
void net_update(void);

void net_send(uint8_t* data, uint16_t len);
void server_connect(void);
void server_connect_port(uint16_t port, espconn_connect_callback connect_cb);
void server_abort(void);

void net_set_ip(uint8_t* ip);
void net_set_port(uint16_t port);
void net_set_domain(char* domai);
void net_set_use_ip(bool use);

bool get_is_conncect_server(void);

