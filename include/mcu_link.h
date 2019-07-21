#pragma once

#pragma once

#define MCULINK_HEAD1       0x55
//#define HEAD2       0xAA

#define MCULINK_SERVER_MASK   0xAA  
#define MCULINK_CLIENT_MASK   0xCC 

#define MCULINK_TAIL        0x66

#define MCULINK_PACKAGE_MIN_SIZE     7

#define MCULINK_CONNECT             0xAE
#define MCULINK_UPDATE_APROM        0xA0
#define MCULINK_UPDATE_CONFIG       0xA1
#define MCULINK_RUN_APROM           0xAB   


enum mcu_parse_status
{
    MCULINK_WAIT_HEAD1 = 0,
    MCULINK_WAIT_DIR_MASK,
    MCULINK_WAIT_SEQ,
    MCULINK_WAIT_CMD,
    MCULINK_WAIT_LEN,
    MCULINK_WAIT_DATA,
    MCULINK_WAIT_CHECKSUM,
    MCULINK_WAIT_TAIL,
};

enum mcu_link_status
{
    MCULINK_STATUS_IDLE = 0,
    MCULINK_STATUS_WAIT_RECV,
};

void mcu_link_init(void);
void mcu_link_update(void);
void mcu_link_set_recv(uint8_t* data, uint16_t len);
bool mcu_link_msg_parse(uint8_t* data, uint16_t len, uint8_t* msg_id);

void mcu_link_send_connect(uint16_t firmware_size);
void mcu_link_send_update_aprom(uint8_t index, uint8_t* data, uint8_t len);
void mcu_link_send_update_config(uint8_t* data, uint8_t len);
void mcu_link_send_run_aprom(void);
