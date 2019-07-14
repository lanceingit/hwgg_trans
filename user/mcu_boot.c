#include "board.h"
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include "mcu_boot.h"
#include "mcu_link.h"
#include "protocol.h"
#include "timer.h"
#include <stdlib.h>


#define RETRY_CNT 		3

uint8_t resend_try=0;
static bool is_mcu_in_boot = false;
static uint32_t is_wait_mcu_boot_ack = false;
static enum mcu_boot_status boot_status = MCUBOOT_IDEL;
static times_t last_send_time;
static uint16_t firmware_size;

static uint8_t firmware_offset=0;
static uint8_t firmware_buf[128];

void set_mcu_in_boot(void)
{
    is_mcu_in_boot = true;
}

void set_mcu_connected(void)
{
    boot_status = MCUBOOT_SEND_FIRMWARE;
}

void mcu_boot_start(void)
{
    protocol_send(PROTOCOL_CH_UART, CMD_MCU_UPGRADE, true);
}


void mcu_boot_send_firmware_package(uint8_t* data, uint8_t len)
{
    mcu_link_send_update_aprom(data, len);
    last_send_time = timer_now();
    resend_try = 0;
}

void mcu_boot_handle(uint8_t cmd, uint8_t* param, uint8_t param_len)
{
	switch(cmd) {
	case MCULINK_CONNECT:
        os_printf("[mcu boot]recv MCULINK_CONNECT, result:%d\n", param[0]);
        if(param[0] == 1) {
            boot_status = MCUBOOT_SEND_FIRMWARE;
            uint8_t tmp[5];
            spi_flash_read(MCU_FIRMWARE_SIZE_ADDR, (uint32_t*)tmp, 5);
            firmware_size = atoi(tmp);
            os_printf("firmware_size:%d\n", firmware_size);
            firmware_offset = 0;
            spi_flash_read(MCU_FIRMWARE_ADDR+firmware_offset, (uint32_t*)firmware_buf, 128);
            mcu_boot_send_firmware_package(firmware_buf, 128);
        }
		break;
	case MCULINK_UPDATE_APROM:
        os_printf("[mcu boot]recv MCULINK_UPDATE_APROM, result:%d\n", param[0]);
        if(boot_status == MCUBOOT_SEND_FIRMWARE) {
            if(param[0] == 1) {
                firmware_offset += 128;
            }
            if(firmware_offset < firmware_size) {
                spi_flash_read(MCU_FIRMWARE_ADDR+firmware_offset, (uint32_t*)firmware_buf, 128);
                mcu_boot_send_firmware_package(firmware_buf, 128);
            } else {
                boot_status = MCUBOOT_BOOT_FINISH;
                spi_flash_erase_sector(MCU_UPGRADE_SECTOR);
                uint32_t tmp = 1;
                spi_flash_write(MCU_UPGRADE_ADDR, &tmp, 4);
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
            mcu_link_send_connect();          
            break;
        case MCUBOOT_SEND_FIRMWARE:     
        {
            if(timer_check(&last_send_time, 1*1000*1000)) {
                if(resend_try < RETRY_CNT)
                {
                    spi_flash_read(MCU_FIRMWARE_ADDR+firmware_offset, (uint32_t*)firmware_buf, 128);
                    mcu_boot_send_firmware_package(firmware_buf, 128);
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
    spi_flash_read(MCU_UPGRADE_ADDR, &is_wait_mcu_boot_ack, 4);
}
