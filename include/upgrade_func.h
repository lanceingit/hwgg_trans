#pragma once

#define UPGRADE_TYPE_MCU_FORCE          0
#define UPGRADE_TYPE_MCU_REQUEST        1
#define UPGRADE_TYPE_APP_REQUEST        2
#define UPGRADE_TYPE_SERVER_REQUEST     3

typedef enum {
    UPGRADE_STATE_IDLE,
    UPGRADE_STATE_CONNECTING,
    UPGRADE_STATE_DOWNLOAD,
    UPGRADE_STATE_MCU_FILE_SAVE,
    UPGRADE_STATE_UPDATE_MCU,
    UPGRADE_STATE_UPDATE_WIFI,
    UPGRADE_STATE_UPDATE_END,
} UpgradeState;

typedef enum {
    UPGRADE_RET_SUCCESS,
    UPGRADE_RET_CRC_ERR,
    UPGRADE_RET_CONNECT_ERR,
} UpgradeResult;

typedef struct {
    uint32_t magic;
    uint32_t upgrade_type;
    uint32_t is_need_upgrade;
} UpgradeInfo;

void upgrade_init(void);
void upgrade_update(void);
void upgrade_handle(void);

void set_request_latest_version(void);
void set_upgrade_type(uint8_t type);

UpgradeState get_upgrade_state(void);
void set_upgrade_connect_done(void);
void set_upgrade_download_done(void);
void set_upgrade_wifi(void);
void set_upgrade_get_version(uint8_t* ver);
void set_mcu_upgrade_result(UpgradeResult ret);
bool get_is_mcu_in_upgrade(void);

