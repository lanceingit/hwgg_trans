#pragma once

typedef void net_conn_cb(void* param);

void net_init(net_conn_cb* cb);
void net_update(void);

void net_send(uint8_t* data, uint8_t len);
void net_connect(void);
void net_abort(void);

