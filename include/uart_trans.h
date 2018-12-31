#pragma once

void uart_trans_init(void);
void uart_trans_update(void);

void ICACHE_FLASH_ATTR uart_trans_send_ch(uint8_t ch);
void ICACHE_FLASH_ATTR uart_trans_send(uint8_t* data, uint8_t len);
