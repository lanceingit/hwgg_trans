#pragma once


typedef enum {
    MCUBOOT_IDEL = 0,
    MCUBOOT_CONNECT,
    MCUBOOT_SEND_FIRMWARE,
    MCUBOOT_BOOT_FINISH,
} McuBootState;

typedef struct {
    uint32_t magic;
    uint32_t is_need_upgrade;
    uint32_t upgrade_status;  //0:none 1:In the process 2:done
    uint32_t firmware_size;
} McuBootInfo;


void mcu_boot_init(void);
void mcu_boot_start(void);
void mcu_boot_run(void);
void mcu_boot_handle(uint8_t cmd, uint8_t* param, uint8_t param_len);
void set_mcu_need_upgrade(void);
bool get_is_need_upgrade(void);
void clear_is_need_upgrade(void);
void set_mcu_upgrade_success(void);
void set_mcu_in_boot(void);
bool get_is_mcu_in_boot(void);
void set_mcu_connected(void);
void set_mcu_upgrade_get_version(uint8_t* ver);
void mcu_upgrade_start(uint32_t firmware_size);
void set_mcu_firmware_size(uint32_t firmware_size);
