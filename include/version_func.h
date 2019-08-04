#pragma once

uint8_t version_get_major(void);
uint8_t version_get_minor(void);

void set_mcu_ver(uint8_t ver);
uint8_t ICACHE_FLASH_ATTR get_mcu_ver(void);
