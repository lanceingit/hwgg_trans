#include "board.h"
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include "mcu_boot.h"
#include "mcu_link.h"
#include "protocol.h"
#include "timer.h"
#include <stdlib.h>
#include "c_types.h"


#define RETRY_CNT 		3

#define UPGRADE_NONE        0
#define UPGRADE_PROCESS     1
#define UPGRADE_DONE        2

typedef struct {
    uint32_t magic;
    uint32_t is_need_upgrade;
    uint32_t upgrade_status;  //0:none 1:In the process 2:done
} McuBootInfo;

uint8_t resend_try=0;
static bool is_mcu_in_boot = false;

static McuBootInfo mcu_boot_info STORE_ATTR = {0,false,0};
static enum mcu_boot_status boot_status = MCUBOOT_IDEL;
static times_t last_send_time;
static uint16_t firmware_size;
static uint16_t firmware_package_index=0;

static uint32_t firmware_offset=0;
static uint8_t firmware_buf[128] STORE_ATTR;


McuBootInfo get_mcu_boot_info(void)
{
    McuBootInfo self;
    spi_flash_read(MCU_UPGRADE_INFO_ADDR, (uint32_t*)&self, sizeof(McuBootInfo));  
    return self;
}
void set_mcu_boot_info(McuBootInfo* self)
{
    self->magic = MCU_UPGRADE_INFO_MAGIC;
    spi_flash_erase_sector(MCU_UPGRADE_INFO_SECTOR);
    spi_flash_write(MCU_UPGRADE_INFO_ADDR, (uint32_t*)self, sizeof(McuBootInfo));    
}

void set_mcu_upgrade_success(void)
{
    mcu_boot_info.is_need_upgrade = false;
    mcu_boot_info.upgrade_status = UPGRADE_NONE;
    set_mcu_boot_info(&mcu_boot_info);
}

void set_mcu_need_upgrade(void)
{
    mcu_boot_info.is_need_upgrade = true;
    mcu_boot_info.upgrade_status = UPGRADE_NONE;
    set_mcu_boot_info(&mcu_boot_info);
}

void set_mcu_in_boot(void)
{
    is_mcu_in_boot = true;
    boot_status = MCUBOOT_CONNECT;
}

void set_mcu_connected(void)
{
    boot_status = MCUBOOT_SEND_FIRMWARE;
}

void mcu_boot_start(void)
{
    protocol_send(PROTOCOL_CH_UART, CMD_MCU_UPGRADE, true);
}


void mcu_boot_send_firmware_package(uint8_t index, uint8_t* data, uint8_t len)
{
    mcu_link_send_update_aprom(index, data, len);
    last_send_time = timer_now();
    resend_try = 0;
}

void mcu_boot_handle(uint8_t cmd, uint8_t* param, uint8_t param_len)
{
	switch(cmd) {
	case MCULINK_CONNECT:
        os_printf("[mcu boot]recv MCULINK_CONNECT, result:%d\n", param[0]);
        if(param[0] == 1) {
            mcu_boot_info.is_need_upgrade = false;
            mcu_boot_info.upgrade_status = UPGRADE_PROCESS;
            set_mcu_boot_info(&mcu_boot_info);

            boot_status = MCUBOOT_SEND_FIRMWARE;
            firmware_offset = 0;
            firmware_package_index = 1;
            spi_flash_read(MCU_FIRMWARE_ADDR+firmware_offset, (uint32_t*)firmware_buf, 128);
            mcu_boot_send_firmware_package(firmware_package_index, firmware_buf, 128);
        }
		break;
	case MCULINK_UPDATE_APROM:
        os_printf("[mcu boot]recv MCULINK_UPDATE_APROM, result:%d\n", param[1]);
        if(boot_status == MCUBOOT_SEND_FIRMWARE) {
            if(param[1] == 1) {
                firmware_offset += 128;
                firmware_package_index++;
            }
            if(firmware_offset < firmware_size) {
                spi_flash_read(MCU_FIRMWARE_ADDR+firmware_offset, (uint32_t*)firmware_buf, 128);
                mcu_boot_send_firmware_package(firmware_package_index, firmware_buf, 128);
            } else {
                mcu_boot_info.is_need_upgrade = false;
                mcu_boot_info.upgrade_status = UPGRADE_DONE;
                set_mcu_boot_info(&mcu_boot_info);
                                
                mcu_link_send_run_aprom();
                last_send_time = timer_now();
                resend_try = 0;                
            }
        }
		break;		
	case MCULINK_RUN_APROM:
        os_printf("[mcu boot]recv MCULINK_RUN_APROM\n");
		break;		
	default:break;					
	}
}

void mcu_boot_run(void)
{
    if(is_mcu_in_boot) {
        switch (boot_status)
        {
        case MCUBOOT_IDEL:            
            break;
        case MCUBOOT_CONNECT: 
        {
            uint8_t tmp[5] STORE_ATTR;
            spi_flash_read(MCU_FIRMWARE_SIZE_ADDR, (uint32_t*)tmp, 5);
            firmware_size = atoi(tmp);
            os_printf("firmware_size:%d\n", firmware_size);
            mcu_link_send_connect(firmware_size);          
            break;
        }
        case MCUBOOT_SEND_FIRMWARE:     
        {
            if(timer_check(&last_send_time, 1*1000*1000)) {
                if(resend_try < RETRY_CNT)
                {
                    spi_flash_read(MCU_FIRMWARE_ADDR+firmware_offset, (uint32_t*)firmware_buf, 128);
                    mcu_boot_send_firmware_package(firmware_package_index, firmware_buf, 128);
                    resend_try++;   
                }
            }
            break;
        }
        case MCUBOOT_BOOT_FINISH:          
            if(timer_check(&last_send_time, 2*1000*1000)) {
                if(resend_try < RETRY_CNT)
                {
                    mcu_link_send_run_aprom();
                    resend_try++;   
                }
            }          
            break;
        default:
            break;
        }
    }
}

void mcu_boot_init(void)
{
    mcu_boot_info = get_mcu_boot_info();
    if(mcu_boot_info.magic != MCU_UPGRADE_INFO_MAGIC) {
        os_printf("mcu boot info corruption!\n");
        mcu_boot_info.magic = MCU_UPGRADE_INFO_MAGIC;
        mcu_boot_info.is_need_upgrade = false;
        mcu_boot_info.upgrade_status = UPGRADE_NONE;
        set_mcu_boot_info(&mcu_boot_info);
    } else {
        os_printf("mcu boot info:%d %d\n", mcu_boot_info.is_need_upgrade, mcu_boot_info.upgrade_status);
        if(mcu_boot_info.is_need_upgrade == true) {
            set_mcu_in_boot();
        }
        if(mcu_boot_info.upgrade_status == UPGRADE_PROCESS) {
            mcu_boot_start();
        } else if(mcu_boot_info.upgrade_status == UPGRADE_DONE) {
            protocol_send(PROTOCOL_CH_UART, CMD_MCU_UPGRADE_STATUS, true);
        }   
    }
}

