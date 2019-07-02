#pragma once

typedef void net_conn_cb(void* param);

void net_init(net_conn_cb* cb);
void net_update(void);

void net_send(uint8_t* data, uint16_t len);
void net_connect(void);
void net_abort(void);

void net_set_ip(uint8_t* ip);
void net_set_port(uint16_t port);
void net_set_domain(char* domai);
void net_set_use_ip(bool use);

