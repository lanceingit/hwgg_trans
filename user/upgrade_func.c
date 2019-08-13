#include "user_interface.h"
#include "osapi.h"
#include "user_interface.h"
#include "espconn.h"

#include "board.h"
#include "upgrade_func.h"
#include "upgrade.h"
#include "utils.h"
#include "secure.h"
#include "net.h"
#include "protocol.h"
#include "http_func.h"
#include "version_func.h"
#include "mcu_boot.h"
#include "key.h"

#define UPGRADE_RETRY_MAX   3

static UpgradeState upgrade_state = UPGRADE_STATE_IDLE;
static bool request_latest_version = false;
static uint8_t upgrade_type = UPGRADE_TYPE_MCU_REQUEST;
static UpgradeResult mcu_upgrade_ret = UPGRADE_RET_SUCCESS;
static UpgradeResult wifi_upgrade_ret = UPGRADE_RET_SUCCESS;
static uint8_t upgrade_retry_cnt = 0;

static UpgradeInfo upgrade_info STORE_ATTR;

uint32_t request_ver_check_timer; 
uint32_t upgrade_server_check_timer; 

static uint8_t read_buf[20] STORE_ATTR;
static uint8_t flash_buf[SPI_FLASH_SEC_SIZE] STORE_ATTR;

static uint8_t lastest_ver[3];
static char url[50];

void ICACHE_FLASH_ATTR set_send_done(void)
{
    if(upgrade_state == UPGRADE_STATE_WAIT_PERCENT3_DONE) {
        upgrade_state = UPGRADE_STATE_UPDATE_WIFI;
    }
    else if(upgrade_state == UPGRADE_STATE_WAIT_PERCENT4_DONE) {
        system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
        system_upgrade_reboot();          
        while(1);
    }
}

void ICACHE_FLASH_ATTR set_get_percent(void)
{
    if(upgrade_state == UPGRADE_STATE_WAIT_PERCENT1) {
        upgrade_state = UPGRADE_STATE_CONNECTING;
        http_get(url, UPGRADE_SERVER_PORT);
    }
    else if(upgrade_state == UPGRADE_STATE_WAIT_PERCENT2) {
        upgrade_state = UPGRADE_STATE_MCU_FILE_SAVE;
    }
    else if(upgrade_state == UPGRADE_STATE_WAIT_PERCENT3) {
        upgrade_state = UPGRADE_STATE_WAIT_PERCENT3_DONE;
    }
    else if(upgrade_state == UPGRADE_STATE_WAIT_PERCENT4) {
        upgrade_state = UPGRADE_STATE_WAIT_PERCENT4_DONE;
    }
}

UpgradeInfo ICACHE_FLASH_ATTR get_upgrade_info(void)
{
    UpgradeInfo self;
    spi_flash_read(UPGRADE_INFO_ADDR, (uint32_t*)&self, sizeof(UpgradeInfo));  
    return self;
}

void ICACHE_FLASH_ATTR set_upgrade_info(UpgradeInfo* self)
{
    self->magic = UPGRADE_INFO_MAGIC;
    spi_flash_erase_sector(ADDR2SECTOR(UPGRADE_INFO_ADDR));
    spi_flash_write(UPGRADE_INFO_ADDR, (uint32_t*)self, sizeof(UpgradeInfo));    
}

void ICACHE_FLASH_ATTR set_upgrade_type(uint8_t type)
{
    upgrade_type = type;
    upgrade_info.upgrade_type = type;
    if(type == UPGRADE_TYPE_MCU_FORCE) {
        upgrade_info.is_need_upgrade = false;
    } else {
        upgrade_info.is_need_upgrade = true;
    }
    set_upgrade_info(&upgrade_info);    
}

void ICACHE_FLASH_ATTR clear_is_need_upgrade(void)
{
    upgrade_info.is_need_upgrade = false;
    set_upgrade_info(&upgrade_info);       
    upgrade_state = UPGRADE_STATE_IDLE;
}


bool ICACHE_FLASH_ATTR get_is_mcu_in_upgrade(void)
{
//    return upgrade_type == UPGRADE_TYPE_MCU_FORCE;
    return (upgrade_type == UPGRADE_TYPE_MCU_FORCE
         || get_is_mcu_in_boot() 
         || upgrade_state == UPGRADE_STATE_WAIT_PERCENT2
         );
}

bool ICACHE_FLASH_ATTR get_is_upgrade_need_net(void)
{
    return (upgrade_state==UPGRADE_STATE_CONNECTING || upgrade_state==UPGRADE_STATE_DOWNLOAD);
}

void ICACHE_FLASH_ATTR set_request_latest_version(void)
{
    if(get_is_conncect_server()) {
        protocol_send(PROTOCOL_CH_NET, CMD_GET_FIRMWARE_VER, true);
        request_latest_version = false;
    } else {
        request_latest_version = true;
    }
}

void ICACHE_FLASH_ATTR set_mcu_upgrade_result(UpgradeResult ret)
{
    mcu_upgrade_ret = ret;
    os_printf("upgrade type:%d state:%d\n", upgrade_type, upgrade_state);
    if(upgrade_type != UPGRADE_TYPE_MCU_FORCE) {
        if(upgrade_state != UPGRADE_STATE_IDLE) {
            upgrade_state = UPGRADE_STATE_UPDATE_WIFI;
        }
    }
}

void ICACHE_FLASH_ATTR set_upgrade_get_version(uint8_t* ver)
{
    request_latest_version = false;
    os_printf("latest version:%d.%d.%d\n", ver[0], ver[1], ver[2]);
    if(upgrade_type != UPGRADE_TYPE_MCU_FORCE) {
        os_printf("mcu version:%d wifi version:%d\n", get_mcu_ver(), version_get_major());
        if(ver[2] <= get_mcu_ver() && ver[1] <= version_get_major()) {
            upgrade_state = UPGRADE_STATE_IDLE;
            upgrade_info.is_need_upgrade = false;
            set_upgrade_info(&upgrade_info);             
            return;
        } 
    }
    memcpy(lastest_ver, ver, 3);
    upgrade_state = UPGRADE_STATE_WAIT_PERCENT1;    
    os_sprintf(url, "firmware/%d.%d.%d.fmw", lastest_ver[0], lastest_ver[1], lastest_ver[2]);
}

UpgradeState ICACHE_FLASH_ATTR get_upgrade_state(void)
{
    return upgrade_state;
}

void ICACHE_FLASH_ATTR set_upgrade_connect_done(void)
{
    upgrade_state = UPGRADE_STATE_DOWNLOAD;
}

void ICACHE_FLASH_ATTR set_upgrade_download_done(void)
{
    if(wifi_upgrade_ret == UPGRADE_RET_CRC_ERR) {
        upgrade_state = UPGRADE_STATE_UPDATE_WIFI;
    } else {
        server_abort();
        upgrade_state = UPGRADE_STATE_WAIT_PERCENT2;
    }
}

void ICACHE_FLASH_ATTR set_upgrade_wifi(void)
{
    upgrade_state = UPGRADE_STATE_UPDATE_WIFI;
}

bool upgrade_firmware_phase(uint32_t start_addr, uint32_t write_addr, bool need_decrypt)
{
    spi_flash_read(start_addr, (uint32_t*)read_buf, 4);
    uint32_t firmware_size = BIG32(read_buf);
    os_printf("firmware size:%d\n", firmware_size);
    spi_flash_read(start_addr+4+firmware_size, (uint32_t*)read_buf, 4);
    os_printf("spi_flash_read addr %x: %x %x %x %x\n", start_addr+4+firmware_size, read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
    uint16_t firmware_crc = BIG16(read_buf);
    os_printf("firmware crc:%d %x\n", firmware_crc, firmware_crc);    

    crc_startChecksum();
    uint8_t sector_num = firmware_size/SPI_FLASH_SEC_SIZE;
    start_addr = start_addr + 4;
    for(uint8_t i = 0; i < sector_num; i++) {
        //file read
        spi_flash_read(start_addr+i*SPI_FLASH_SEC_SIZE, (uint32_t*)flash_buf, SPI_FLASH_SEC_SIZE);

        //checksum
        os_printf("crc sector:%d\n", i);
        for(int j = 0; j < SPI_FLASH_SEC_SIZE; j++) {
            crc_updateChecksum(flash_buf[j]);
        }

        //decrypt
        if(need_decrypt) {
            bin_decrypt_buf(flash_buf, flash_buf, SPI_FLASH_SEC_SIZE);
        }

        //firmware write
        os_printf("erase sector:%x\n", ADDR2SECTOR(write_addr)+i);
        spi_flash_erase_sector((uint16)ADDR2SECTOR(write_addr)+i);
        os_printf("flash write addr:%x len:%d\n", write_addr+(i*SPI_FLASH_SEC_SIZE), SPI_FLASH_SEC_SIZE);
        spi_flash_write(write_addr+(i*SPI_FLASH_SEC_SIZE), (uint32_t*)flash_buf, SPI_FLASH_SEC_SIZE);
    }
    uint32_t firmware_last = firmware_size%SPI_FLASH_SEC_SIZE;
    if(firmware_last > 0) {
        //file read last
        spi_flash_read(start_addr+sector_num*SPI_FLASH_SEC_SIZE, (uint32_t*)flash_buf, firmware_last);

        //checksum
        os_printf("crc sector:%d\n", sector_num);
        for(int j = 0; j < firmware_last; j++) {
            crc_updateChecksum(flash_buf[j]);
        }

        //decrypt
        if(need_decrypt) {
            bin_decrypt_buf(flash_buf, flash_buf, firmware_last);
        }

        //firmware write last
        os_printf("erase sector:%x\n", ADDR2SECTOR(write_addr)+sector_num);
        spi_flash_erase_sector((uint16)ADDR2SECTOR(write_addr)+sector_num);
        os_printf("flash write addr:%x len:%d\n", write_addr+(sector_num*SPI_FLASH_SEC_SIZE), firmware_last);
        spi_flash_write(write_addr+(sector_num*SPI_FLASH_SEC_SIZE), (uint32_t*)flash_buf, firmware_last);
    }

    crc_endChecksum();
    os_printf("firmware crc:%d %x\n", crc_get(), crc_get());       
    os_printf("crc get:%x cal:%x\n", firmware_crc, crc_get());           

    if(firmware_crc == crc_get()) {
        return true;
    } else {
        return false;
    }   
}

void upgrade_mcu_save(void)
{   
    os_printf("upgrade mcu save!\n");
    spi_flash_read(DOWNLOAD_FILE_ADDR, (uint32_t*)read_buf, 8);
    uint8_t device_type = read_buf[0];
    uint8_t wifi_ver = read_buf[2];
    uint8_t mcu_ver = read_buf[3];
    os_printf("version:%d.%d.%d\n", read_buf[1], wifi_ver, mcu_ver);
    uint32_t mcu_firmware_size = BIG32(&read_buf[4]);

    if(upgrade_type != UPGRADE_TYPE_MCU_FORCE) {
        os_printf("version now:%d get:%d\n", get_mcu_ver(), mcu_ver);
        if(mcu_ver <= get_mcu_ver()) {
            upgrade_state = UPGRADE_STATE_UPDATE_WIFI;
            return;
        }
    }

    os_printf("\nmcu firmware save! start_addr:%x write_addr:%x\n", DOWNLOAD_FILE_ADDR+4, MCU_FIRMWARE_ADDR);
    bool mcu_checksum_ret = upgrade_firmware_phase(DOWNLOAD_FILE_ADDR+4, MCU_FIRMWARE_ADDR, false);
    if(mcu_checksum_ret == false) {
        mcu_upgrade_ret = UPGRADE_RET_CRC_ERR;
        if(upgrade_type == UPGRADE_TYPE_MCU_FORCE) {
            upgrade_state = UPGRADE_STATE_UPDATE_END;
        } 
        else {
            protocol_send(PROTOCOL_CH_UART, CMD_MCU_UPGRADE_REQUEST, false, 0);
            upgrade_state = UPGRADE_STATE_UPDATE_WIFI;
        }
    } else {
        if(upgrade_type == UPGRADE_TYPE_MCU_FORCE) {
            mcu_upgrade_start(mcu_firmware_size);           
        }
        else {
            set_mcu_firmware_size(mcu_firmware_size);
            mcu_boot_start();
        }
        upgrade_state = UPGRADE_STATE_UPDATE_MCU;
    }
}

void upgrade_wifi(void)
{   
    upgrade_info.is_need_upgrade = false;
    set_upgrade_info(&upgrade_info);        

    os_printf("upgrade wifi!\n");
    spi_flash_read(DOWNLOAD_FILE_ADDR, (uint32_t*)read_buf, 8);
    uint8_t device_type = read_buf[0];
    uint8_t wifi_ver = read_buf[2];
    uint8_t mcu_ver = read_buf[3];
    os_printf("version:%d.%d.%d\n", read_buf[1], wifi_ver, mcu_ver);
    uint32_t mcu_firmware_size = BIG32(&read_buf[4]);
    
    os_printf("version now:%d get:%d\n", version_get_major(), wifi_ver);
    if(wifi_ver <= version_get_major()) {
        upgrade_state = UPGRADE_STATE_IDLE;
        upgrade_info.is_need_upgrade = false;
        set_upgrade_info(&upgrade_info);              
        return;
    }

    spi_flash_read(DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+2, (uint32_t*)read_buf, 4);
    uint32_t wifi1_firmware_size = BIG32(read_buf); 
    uint8_t curr_userbin = system_upgrade_userbin_check();
    os_printf("wifi user bin:%d\n", curr_userbin+1);
    uint32_t start_addr;
    uint32_t write_addr;
    if(curr_userbin==0) {
        start_addr = DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+6+wifi1_firmware_size+2;
        write_addr = WIFI2_FIRMWARE_ADDR;
    }
    else if(curr_userbin==1) {
        start_addr = DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+2;
        write_addr = WIFI1_FIRMWARE_ADDR;
    } 
    else {
        os_printf("user bin err!\n");      
        goto upgrade_err;  
    }
    os_printf("\nwifi firmware phase! start_addr:%x write_addr:%x\n", start_addr, write_addr);
    bool wifi_checksum_ret = upgrade_firmware_phase(start_addr, write_addr, true);
    if(wifi_checksum_ret == true) {
        upgrade_info.is_need_upgrade = false;
        set_upgrade_info(&upgrade_info);              
        // system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
        // system_upgrade_reboot();      
        upgrade_state = UPGRADE_STATE_WAIT_PERCENT4;  
        //while(1); 
    } else {
        os_printf("wifi firmware crc err!\n");
        goto upgrade_err;
    }   
    return;
upgrade_err:
    os_printf("upgrade err!\n");    
}

// void upgrade_handle(void)
// {
//     os_printf("upgrade handle!\n");
//     spi_flash_read(DOWNLOAD_FILE_ADDR, (uint32_t*)read_buf, 8);
//     uint8_t device_type = read_buf[0];
//     uint8_t wifi_ver = read_buf[2];
//     uint8_t mcu_ver = read_buf[3];
//     os_printf("version:%d.%d.%d\n", read_buf[1], wifi_ver, mcu_ver);
//     uint32_t mcu_firmware_size = BIG32(&read_buf[4]);

//     os_printf("\nmcu firmware phase! start_addr:%x write_addr:%x\n", DOWNLOAD_FILE_ADDR+8, MCU_FIRMWARE_ADDR);
//     bool mcu_checksum_ret = upgrade_firmware_phase(DOWNLOAD_FILE_ADDR+8, MCU_FIRMWARE_ADDR, false);
     
//     spi_flash_read(DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+2, (uint32_t*)read_buf, 4);
//     uint32_t wifi1_firmware_size = BIG32(read_buf); 
//     uint8_t curr_userbin = system_upgrade_userbin_check();
//     os_printf("wifi user bin:%d\n", curr_userbin+1);
//     uint32_t start_addr;
//     uint32_t write_addr;
//     if(curr_userbin==0) {
//         start_addr = DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+6+wifi1_firmware_size+2;
//         write_addr = WIFI2_FIRMWARE_ADDR;
//     }
//     else if(curr_userbin==1) {
//         start_addr = DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+2;
//         write_addr = WIFI1_FIRMWARE_ADDR;
//     } 
//     else {
//         os_printf("user bin err!\n");      
//         goto upgrade_err;  
//     }
//     os_printf("\nwifi firmware phase! start_addr:%x write_addr:%x\n", DOWNLOAD_FILE_ADDR+8, MCU_FIRMWARE_ADDR);
//     bool wifi_checksum_ret = upgrade_firmware_phase(start_addr, write_addr, true);
//     if(wifi_checksum_ret == true) {
//         // system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
//         // system_upgrade_reboot();   
//         while(1);     
//     } else {
//         os_printf("wifi firmware crc err!\n");
//         goto upgrade_err;
//     }   

//     return;
// upgrade_err:
//     os_printf("upgrade err!\n");
// }

// void upgrade_handle(void)
// {
//     spi_flash_read(DOWNLOAD_FILE_ADDR, (uint32_t*)read_buf, 8);
//     uint8_t device_type = read_buf[0];
//     uint8_t wifi_ver = read_buf[2];
//     uint8_t mcu_ver = read_buf[3];
//     os_printf("version:%d.%d.%d\n", read_buf[1], wifi_ver, mcu_ver);
//     uint32_t mcu_firmware_size = BIG32(&read_buf[4]);

// {    
//     os_printf("mcu size:%d\n", mcu_firmware_size);
//     spi_flash_read(DOWNLOAD_FILE_ADDR+8+mcu_firmware_size, (uint32_t*)read_buf, 4);
//     os_printf("spi_flash_read addr %x: %x %x %x %x\n", DOWNLOAD_FILE_ADDR+8+mcu_firmware_size, read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
//     uint16_t mcu_firmware_crc = BIG16(read_buf);
//     os_printf("mcu crc:%d %x\n", mcu_firmware_crc, mcu_firmware_crc);

//     crc_startChecksum();
//     uint8_t sector_num = mcu_firmware_size/SPI_FLASH_SEC_SIZE;
//     uint32_t start_addr = DOWNLOAD_FILE_ADDR+8;
//     for(uint8_t i = 0; i < sector_num; i++) {
//         //file read
//         spi_flash_read(start_addr+i*SPI_FLASH_SEC_SIZE, (uint32_t*)flash_buf, SPI_FLASH_SEC_SIZE);

//         //checksum
//         os_printf("crc sector:%d\n", i);
//         for(int j = 0; j < SPI_FLASH_SEC_SIZE; j++) {
//             crc_updateChecksum(flash_buf[j]);
//         }

//         //firmware write
//         os_printf("erase sector:%x\n", ADDR2SECTOR(MCU_FIRMWARE_ADDR)+i);
//         spi_flash_erase_sector((uint16)ADDR2SECTOR(MCU_FIRMWARE_ADDR)+i);
//         os_printf("flash write addr:%x len:%d\n", MCU_FIRMWARE_ADDR+(i*SPI_FLASH_SEC_SIZE), SPI_FLASH_SEC_SIZE);
//         spi_flash_write(MCU_FIRMWARE_ADDR+(i*SPI_FLASH_SEC_SIZE), (uint32_t*)flash_buf, SPI_FLASH_SEC_SIZE);
//     }
//     uint32_t firmware_last = mcu_firmware_size%SPI_FLASH_SEC_SIZE;
//     if(firmware_last > 0) {
//         //file read last
//         spi_flash_read(start_addr+sector_num*SPI_FLASH_SEC_SIZE, (uint32_t*)flash_buf, firmware_last);

//         //checksum
//         os_printf("crc sector:%d\n", sector_num);
//         for(int j = 0; j < firmware_last; j++) {
//             crc_updateChecksum(flash_buf[j]);
//         }

//         //firmware write last
//         os_printf("erase sector:%x\n", ADDR2SECTOR(MCU_FIRMWARE_ADDR)+sector_num);
//         spi_flash_erase_sector((uint16)ADDR2SECTOR(MCU_FIRMWARE_ADDR)+sector_num);
//         os_printf("flash write addr:%x len:%d\n", MCU_FIRMWARE_ADDR+(sector_num*SPI_FLASH_SEC_SIZE), firmware_last);
//         spi_flash_write(MCU_FIRMWARE_ADDR+(sector_num*SPI_FLASH_SEC_SIZE), (uint32_t*)flash_buf, firmware_last);
//     }

//     crc_endChecksum();
//     os_printf("firmware crc:%d %x\n", crc_get(), crc_get());       
//     os_printf("mcu crc get:%x cal:%x\n", mcu_firmware_crc, crc_get());       
// }
     
//     //wifi firmware
// {
//     spi_flash_read(DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+2, (uint32_t*)read_buf, 4);
//     uint32_t wifi1_firmware_size = BIG32(read_buf); 
//     os_printf("wifi1 firmware size:%d\n", wifi1_firmware_size);
//     uint8_t curr_userbin = system_upgrade_userbin_check();
//     os_printf("wifi user bin:%d\n", curr_userbin+1);
//     uint32_t start_addr;
//     uint32_t write_addr;
//     if(curr_userbin==0) {
//         start_addr = DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+6+wifi1_firmware_size+2;
//         write_addr = WIFI2_FIRMWARE_ADDR;
//     }
//     else if(curr_userbin==1) {
//         start_addr = DOWNLOAD_FILE_ADDR+8+mcu_firmware_size+2;
//         write_addr = WIFI1_FIRMWARE_ADDR;
//     } 
//     else {
//         os_printf("user bin err!\n");      
//         goto upgrade_err;  
//     }
//     os_printf("wifi start addr:%x\n", start_addr);
//     os_printf("wifi write addr:%x\n", write_addr);
//     spi_flash_read(start_addr, (uint32_t*)read_buf, 4);
//     uint32_t wifi_firmware_size = BIG32(read_buf);
//     os_printf("wifi size:%d\n", wifi_firmware_size);
//     spi_flash_read(start_addr+4+wifi_firmware_size, (uint32_t*)read_buf, 4);
//     os_printf("spi_flash_read addr %x: %x %x %x %x\n", start_addr+4+wifi_firmware_size, read_buf[0], read_buf[1], read_buf[2], read_buf[3]);
//     uint16_t wifi_firmware_crc = BIG16(read_buf);
//     os_printf("wifi crc:%d %x\n", wifi_firmware_crc, wifi_firmware_crc);    

//     crc_startChecksum();
//     uint8_t sector_num = wifi_firmware_size/SPI_FLASH_SEC_SIZE;
//     start_addr = start_addr + 4;
//     for(uint8_t i = 0; i < sector_num; i++) {
//         //file read
//         spi_flash_read(start_addr+i*SPI_FLASH_SEC_SIZE, (uint32_t*)flash_buf, SPI_FLASH_SEC_SIZE);

//         //checksum
//         os_printf("crc sector:%d\n", i);
//         for(int j = 0; j < SPI_FLASH_SEC_SIZE; j++) {
//             crc_updateChecksum(flash_buf[j]);
//         }

//         //decrypt
//         bin_decrypt_buf(flash_buf, flash_buf, SPI_FLASH_SEC_SIZE);

//         //firmware write
//         os_printf("erase sector:%x\n", ADDR2SECTOR(write_addr)+i);
//         spi_flash_erase_sector((uint16)ADDR2SECTOR(write_addr)+i);
//         os_printf("flash write addr:%x len:%d\n", write_addr+(i*SPI_FLASH_SEC_SIZE), SPI_FLASH_SEC_SIZE);
//         spi_flash_write(write_addr+(i*SPI_FLASH_SEC_SIZE), (uint32_t*)flash_buf, SPI_FLASH_SEC_SIZE);
//     }
//     uint32_t firmware_last = wifi_firmware_size%SPI_FLASH_SEC_SIZE;
//     if(firmware_last > 0) {
//         //file read last
//         spi_flash_read(start_addr+sector_num*SPI_FLASH_SEC_SIZE, (uint32_t*)flash_buf, firmware_last);

//         //checksum
//         os_printf("crc sector:%d\n", sector_num);
//         for(int j = 0; j < firmware_last; j++) {
//             crc_updateChecksum(flash_buf[j]);
//         }

//         //decrypt
//         bin_decrypt_buf(flash_buf, flash_buf, firmware_last);

//         //firmware write last
//         os_printf("erase sector:%x\n", ADDR2SECTOR(write_addr)+sector_num);
//         spi_flash_erase_sector((uint16)ADDR2SECTOR(write_addr)+sector_num);
//         os_printf("flash write addr:%x len:%d\n", write_addr+(sector_num*SPI_FLASH_SEC_SIZE), firmware_last);
//         spi_flash_write(write_addr+(sector_num*SPI_FLASH_SEC_SIZE), (uint32_t*)flash_buf, firmware_last);
//     }

//     crc_endChecksum();
//     os_printf("firmware crc:%d %x\n", crc_get(), crc_get());       
//     os_printf("wifi crc get:%x cal:%x\n", wifi_firmware_crc, crc_get());           

//     if(wifi_firmware_crc == crc_get()) {
//         system_upgrade_flag_set(UPGRADE_FLAG_FINISH);
//         system_upgrade_reboot();        
//     } else {
//         os_printf("wifi firmware crc err!\n");
//         goto upgrade_err;
//     }   
// }

//     return;
// upgrade_err:
//     os_printf("upgrade err!\n");
// }

void upgrade_update(void)
{
	if(system_get_time() - request_ver_check_timer > (100)) {
        if(request_latest_version) {
            if(get_is_conncect_server()) {
                os_printf("upgrade request latest version!\n");
                protocol_send(PROTOCOL_CH_NET, CMD_GET_FIRMWARE_VER, true);
                request_latest_version = true;
            }
        }
        request_ver_check_timer = system_get_time();
	}

	if(system_get_time() - upgrade_server_check_timer > (100)) {
        if(upgrade_state == UPGRADE_STATE_CONNECTING) {
            http_connect(UPGRADE_SERVER_PORT);
        }
        upgrade_server_check_timer = system_get_time();
    }

    if(upgrade_state == UPGRADE_STATE_MCU_FILE_SAVE) {
        upgrade_mcu_save();
    }
    if(upgrade_state == UPGRADE_STATE_UPDATE_WIFI) {
        upgrade_wifi(); 
    }
    if(upgrade_state == UPGRADE_STATE_UPDATE_END) {
        if(upgrade_retry_cnt < UPGRADE_RETRY_MAX) {
            if(mcu_upgrade_ret == UPGRADE_RET_CRC_ERR) {
                upgrade_state = UPGRADE_STATE_IDLE;
                request_latest_version = true;
                upgrade_retry_cnt++;
            } 
            else if(wifi_upgrade_ret == UPGRADE_RET_CRC_ERR) {
                upgrade_state = UPGRADE_STATE_IDLE;
                request_latest_version = true;
                upgrade_retry_cnt++;
            }
        } else {
            upgrade_info.is_need_upgrade = false;
            set_upgrade_info(&upgrade_info);              
            upgrade_state = UPGRADE_STATE_IDLE;
        }
    }
}

void upgrade_init(void)
{
    upgrade_info = get_upgrade_info();
    if(upgrade_info.magic != UPGRADE_INFO_MAGIC) {
        os_printf("upgrade info corruption!\n");
        upgrade_info.magic = UPGRADE_INFO_MAGIC;
        upgrade_info.is_need_upgrade = false;
        set_upgrade_info(&upgrade_info);
    } else {
        os_printf("upgrade info:%d %d\n", upgrade_info.upgrade_type, upgrade_info.is_need_upgrade);
        if(key_is_press()) {
            os_printf("key press in power on, clear upgrade boot info!\n");            
            upgrade_info.is_need_upgrade = false;
            set_upgrade_info(&upgrade_info);            
        } else {
            if(upgrade_info.upgrade_type != UPGRADE_TYPE_MCU_FORCE) {
                if(upgrade_info.is_need_upgrade) {
                    if(!get_is_mcu_need_upgrade_first()) {
                        upgrade_state = UPGRADE_STATE_WAIT_PERCENT3;
                    }
                }
            }
        }
    }
}
