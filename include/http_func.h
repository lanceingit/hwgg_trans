#pragma once

void http_get(char* path, uint16_t port);
void http_handle(uint8_t* data, uint16_t len);
void http_connect(uint16_t port);
void http_get_test(void);

