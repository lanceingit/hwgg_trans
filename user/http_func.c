#include "user_interface.h"
#include "osapi.h"
#include "user_interface.h"
#include "espconn.h"

#include "board.h"
#include "mem.h"
#include <string.h>
#include <stdlib.h>

#include "http_func.h"
#include "net.h"
#include "secure.h"
#include "upgrade_func.h"
#include "utils.h"
#include "mcu_boot.h"


#define HTTP_HEAD "Connection: keep-alive\r\n\
Cache-Control: no-cache\r\n\
User-Agent: Mozilla/5.0 (Windows NT 5.1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/30.0.1599.101 Safari/537.36 \r\n\
Accept: */*\r\n\
Authorization: token\r\n\
Accept-Encoding: gzip,deflate,sdch\r\n\
Accept-Language: zh-CN,zh;q=0.8\r\n\r\n"

static uint32_t file_cnt;

static char http_path[50];

void ICACHE_FLASH_ATTR http_connect_cb(void *arg)  
{
	os_printf("http connect success!\n");

    char* url = os_malloc(500);
    if(url!=NULL) {
        os_sprintf(url, "GET /%s HTTP/1.1\r\nHost: jl.hwei.net\r\n%s", http_path, HTTP_HEAD);
        net_send(url, strlen(url));
        os_free(url);
    }
    file_cnt = 0;
    set_upgrade_connect_done();
}

// /firmware/1.4.6.fmw
void ICACHE_FLASH_ATTR http_get(char* path, uint16_t port)
{
    os_printf("http get!\n");
    strcpy(http_path, path);
    server_abort();
    server_connect_port(port, http_connect_cb);
}

void ICACHE_FLASH_ATTR http_get_test(void)
{
    os_printf("http get test!\n");
    char* url = os_malloc(500);
    if(url!=NULL) {
        os_sprintf(url, "GET %s HTTP/1.1\r\nHost: jl.hwei.net\r\n%s", "/firmware/1.4.6.fmw", HTTP_HEAD);
        net_send(url, strlen(url));
        os_free(url);
    }
    file_cnt = 0;
}

void ICACHE_FLASH_ATTR http_connect(uint16_t port)
{
    os_printf("http connect %d!\n", port);
    server_connect_port(port, http_connect_cb);
}

bool ICACHE_FLASH_ATTR read_line(char* data, uint16_t len, char* line)
{
    for(int i=0; i<len; i++) {
        line[i] = data[i];
        if(line[i-1]==0x0D && line[i]==0x0A) {
            line[i+1] = 0;
            return true;
        }
    }

    return false;
}

bool ICACHE_FLASH_ATTR http_header_fields_get(char* line, char *fields, char* fields_data)
{
    char* pos = strstr(line, fields);
    if(pos != NULL) {
        if(read_line(line+(pos-line), strlen(line)-(pos-line), fields_data)) {
            return true;
        }
    }
    return false;
}

#define SECTOR_SIZE   SPI_FLASH_SEC_SIZE

static char fields_data[100];
static char line[100];
static bool is_get_len=false;
static uint32_t http_file_len=0;
static uint8_t flash_buf[SECTOR_SIZE] STORE_ATTR;
static uint32_t flash_write_index;
static uint32_t sector_index;

void http_handle(uint8_t* data, uint16_t len)
{
    SpiFlashOpResult ret;

    if(file_cnt == 0) {
        os_printf("http recv:");
        for(uint16_t i=0; i<len; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");        
        for(uint16_t i=0; i<len; i++) {
            os_printf("%c", data[i]);
        }
        os_printf("\n");        

        uint16_t line_index=0;
        while(len > line_index) {
            os_printf("len:%d index:%d last:%d\n", len, line_index, len-line_index);
            if(read_line(data+line_index, sizeof(line)-1, line)) {
                os_printf("line:%s\n", line);
                line_index += strlen(line);
                if(http_header_fields_get(line, "HTTP/", fields_data)) {
                    os_printf("status get: %s\n", fields_data);
                    char status_code[10]={0};
                    char* ptr = strstr(fields_data," ");
                    memcpy(status_code, ptr+1, 3);
                    int status = atoi(status_code);
                    os_printf("http response status:%d\n", status);
                    if(status != 200) {
                        clear_is_need_upgrade();
                        return;
                    } else {
                        
                    }
                }
                else if(http_header_fields_get(line, "Content-Length: ", fields_data)) {
                    os_printf("len get: %s\n", fields_data);
                    char* ptr = strstr(fields_data,": ");
                    http_file_len = atoi(ptr+2);
                    os_printf("http file len:%d\n", http_file_len);
                    is_get_len = true;
                }
            } 
            else {
                if(is_get_len) {
                    file_cnt = len - line_index;
                    os_printf("1:http file  total:%d get len:%d cnt:%d\n", http_file_len, file_cnt, file_cnt);
                    os_printf("erase sector:%d\n", 0);
                    spi_flash_erase_sector((uint16)ADDR2SECTOR(DOWNLOAD_FILE_ADDR));
                    flash_write_index=0;
                    sector_index=0;

                    memcpy(flash_buf, data+line_index, file_cnt);
                    flash_write_index += file_cnt;
                }
                return;
            }
        }
    }
    else {
        uint32_t flash_index = file_cnt;
        file_cnt += len;
        os_printf("http file  total:%d get len:%d cnt:%d flash:%d\n", http_file_len, len, file_cnt, flash_write_index);

        if(flash_write_index+len > SECTOR_SIZE) {
            uint32_t sector_last_len = SECTOR_SIZE-flash_write_index;
            memcpy(flash_buf+flash_write_index, data, sector_last_len);
            flash_write_index=0;
            ret = spi_flash_write(DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), (uint32_t*)flash_buf, SECTOR_SIZE);
            os_printf("flash write ret:%d addr:%x(%x) len:%d\n", ret, sector_index*SECTOR_SIZE, DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), SECTOR_SIZE);
            for(uint16_t i=0; i<10; i++) {
                os_printf("%02x ", flash_buf[i]);
            }
            os_printf("\n");                    
            
            sector_index++;
            ret = spi_flash_erase_sector((uint16)ADDR2SECTOR(DOWNLOAD_FILE_ADDR)+sector_index);
            os_printf("erase ret:%d sector:%d\n", ret, sector_index);

            uint32_t last_len = len-sector_last_len;
            while(last_len > SECTOR_SIZE) {
                memcpy(flash_buf+flash_write_index, data+(len-last_len), SECTOR_SIZE);    
                ret = spi_flash_write(DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), (uint32_t*)flash_buf, SECTOR_SIZE);
                os_printf("flash write ret:%d addr:%x(%x) len:%d\n", ret, sector_index*SECTOR_SIZE, DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), SECTOR_SIZE);
                for(uint16_t i=0; i<10; i++) {
                    os_printf("%02x ", flash_buf[i]);
                }
                os_printf("\n");                    

                flash_write_index=0;    
                last_len -= SECTOR_SIZE;

                sector_index++;
                ret = spi_flash_erase_sector((uint16)ADDR2SECTOR(DOWNLOAD_FILE_ADDR)+sector_index);
                os_printf("erase ret:%d sector:%d\n", ret, sector_index);
            }        

            memcpy(flash_buf, data+(len-last_len), last_len);
            flash_write_index = last_len; 
        } 
        else {
            memcpy(flash_buf+flash_write_index, data, len);
            flash_write_index += len;
        }



//         if(flash_write_index+len > SECTOR_SIZE) {
//             uint32_t last_len = SECTOR_SIZE-flash_write_index;
//             memcpy(flash_buf+flash_write_index, data, last_len);
//             flash_write_index=0;
// //            spi_flash_write(DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), (uint32_t*)flash_buf, SECTOR_SIZE);
//             os_printf("flash write addr:%x(%x) len:%d\n", sector_index*SECTOR_SIZE, DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), SECTOR_SIZE);
//             for(uint16_t i=0; i<10; i++) {
//                 os_printf("%02x ", flash_buf[i]);
//             }
//             os_printf("\n");                    

            
//             memcpy(flash_buf, data+last_len, len-last_len);
//             flash_write_index = len-last_len;
            
//             sector_index++;
//             os_printf("erase sector:%d\n", sector_index);
// //            spi_flash_erase_sector((uint16)ADDR2SECTOR(DOWNLOAD_FILE_ADDR)+sector_index);
//         } 
//         else {
//             memcpy(flash_buf+flash_write_index, data, len);
//             flash_write_index += len;
//         }
    }

    if(file_cnt >= http_file_len) {
            ret = spi_flash_write(DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), (uint32_t*)flash_buf, flash_write_index);
            os_printf("2:flash write addr:%x(%x) len:%d\n", sector_index*SECTOR_SIZE, DOWNLOAD_FILE_ADDR+(sector_index*SECTOR_SIZE), SECTOR_SIZE);
            for(uint16_t i=0; i<10; i++) {
                os_printf("%02x ", flash_buf[i]);
            }
            os_printf("\n");   
            os_printf("\n");   



        uint8_t data[10] STORE_ATTR ={0};
        spi_flash_read(DOWNLOAD_FILE_ADDR, (uint32_t*)data, 10);
        for(uint16_t i=0; i<10; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");
        spi_flash_read(DOWNLOAD_FILE_ADDR+0x100, (uint32_t*)data, 10);
        for(uint16_t i=0; i<10; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");
        spi_flash_read(DOWNLOAD_FILE_ADDR+0x1000, (uint32_t*)data, 10);
        for(uint16_t i=0; i<10; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");                
        spi_flash_read(DOWNLOAD_FILE_ADDR+0x10000, (uint32_t*)data, 10);
        for(uint16_t i=0; i<10; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");                
        spi_flash_read(DOWNLOAD_FILE_ADDR+0x20000, (uint32_t*)data, 10);
        for(uint16_t i=0; i<10; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");                
        spi_flash_read(DOWNLOAD_FILE_ADDR+0x2DAB0, (uint32_t*)data, 10);
        for(uint16_t i=0; i<10; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");

        spi_flash_read(DOWNLOAD_FILE_ADDR, (uint32_t*)data, 10);
        for(uint16_t i=0; i<10; i++) {
            os_printf("%02x ", data[i]);
        }
        os_printf("\n");        

        set_upgrade_download_done();
        file_cnt = 0;
        //upgrade_handle();
    }
}

