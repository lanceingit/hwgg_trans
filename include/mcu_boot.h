#pragma once


enum mcu_boot_status
{
    MCUBOOT_IDEL = 0,
    MCUBOOT_CONNECT,
    MCUBOOT_SEND_FIRMWARE,
    MCUBOOT_BOOT_FINISH,
};

void mcu_boot_init(void);
void mcu_boot_start(void);
void mcu_boot_run(void);
void mcu_boot_handle(uint8_t cmd, uint8_t* param, uint8_t param_len);
void set_mcu_need_upgrade(void);
void set_mcu_upgrade_success(void);
void set_mcu_in_boot(void);
void set_mcu_connected(void);
