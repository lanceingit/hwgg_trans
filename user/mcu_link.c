#include "board.h"
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include <stdarg.h>

#include "mcu_link.h"
#include "mcu_boot.h"
#include "uart_trans.h"
#include "mem.h"

#define RETRY_CNT 		3
#define RECV_BUF_ZISE	255

static enum mcu_parse_status parse_step = MCULINK_WAIT_HEAD1;

static uint8_t checksum;
static uint8_t checksum_buf_index;
static uint8_t checksum_buf[256];
static uint8_t link_cmd;
static uint8_t data_len;
static uint8_t recv_data[256];
static uint8_t data_cnt;
static uint8_t resend_try;
static uint8_t send_buf[256];
static uint8_t mcu_link_buf[256];
static uint8_t recv_buf[256];
static uint8_t recv_buf_len;
static uint8_t link_seq=0;

void mcu_link_encode(uint8_t cmd, uint8_t* data, uint8_t len);


void ICACHE_FLASH_ATTR mcu_link_send_connect(uint16_t firmware_size)
{
    os_printf("[mcu link]send connect\n");
	uint8_t package_num = firmware_size/128+(firmware_size%128)?1:0;
    mcu_link_encode(MCULINK_CONNECT, &package_num, 1);    
}

void ICACHE_FLASH_ATTR mcu_link_send_update_aprom(uint8_t index, uint8_t* data, uint8_t len)
{
    if(len > 128) {
        os_printf("update aprom len exceed!\n");
        len = 128;
    }
	uint8_t* buf = os_malloc(129);
	if(buf != NULL) {
		buf[0] = index;
		memcpy(&buf[1], data, len);
		os_printf("[mcu link]send update_aprom\n");
		mcu_link_encode(MCULINK_UPDATE_APROM, buf, len+1);    
		os_free(buf);
	}
}

void ICACHE_FLASH_ATTR mcu_link_send_update_config(uint8_t* data, uint8_t len)
{
    if(len > 128) {
        os_printf("update aprom len exceed!\n");
        len = 128;
    }
    os_printf("[mcu link]send update_config\n");
    mcu_link_encode(MCULINK_UPDATE_CONFIG, data, len);    
}

void ICACHE_FLASH_ATTR mcu_link_send_run_aprom(void)
{
    os_printf("[mcu link]send run_aprom\n");
    mcu_link_encode(MCULINK_RUN_APROM, NULL, 0);    
}

//////////////////////////////////////////////////////////////////

uint16_t ICACHE_FLASH_ATTR mcu_link_get_checksum(uint8_t* data, uint16_t len)
{
    uint8_t crc = 0x00;
	uint8_t i;
    while (len--) {
        crc ^= *data++;
        for (i = 0; i <8; ++i) 
		{
            if (crc & 0x80)
                crc = (crc << 1) ^ 0xD5;
						
            else
                crc = (crc << 1);
        }
    }
    return (crc);
}

void ICACHE_FLASH_ATTR mcu_link_trans(uint8_t* data, uint8_t len)
{
    uart_trans_send(data, len);
}	

void ICACHE_FLASH_ATTR mcu_link_encode(uint8_t cmd, uint8_t* data, uint8_t len)
{
	mcu_link_buf[0] = MCULINK_HEAD1;
	mcu_link_buf[1] = MCULINK_SERVER_MASK;
	mcu_link_buf[2] = link_seq++;
	mcu_link_buf[3] = cmd;
	mcu_link_buf[4] = len;
	memcpy(&mcu_link_buf[5], data, len);
	mcu_link_buf[5+len] = mcu_link_get_checksum(&mcu_link_buf[0], mcu_link_buf[4]+5);
	mcu_link_buf[6+len] = MCULINK_TAIL;

	mcu_link_trans(mcu_link_buf, mcu_link_buf[4]+MCULINK_PACKAGE_MIN_SIZE);
}

bool ICACHE_FLASH_ATTR mcu_link_parse_char(uint8_t ch)
{
	bool ret = false;
	
	// os_printf("step:%d ch:%x\n", parse_step, ch);

	switch(parse_step){
		case MCULINK_WAIT_HEAD1:
            checksum_buf_index=0;
            checksum_buf[checksum_buf_index++] = ch;
			if(ch == MCULINK_HEAD1){
				parse_step = MCULINK_WAIT_DIR_MASK;
			}
			break;
		case MCULINK_WAIT_DIR_MASK:
            checksum_buf[checksum_buf_index++] = ch;
			if(ch == MCULINK_CLIENT_MASK){
				parse_step = MCULINK_WAIT_SEQ;
			} else {
                parse_step = MCULINK_WAIT_HEAD1;
            } 
			break;            
		case MCULINK_WAIT_SEQ:
            checksum_buf[checksum_buf_index++] = ch;
			parse_step = MCULINK_WAIT_CMD;
			break;
		case MCULINK_WAIT_CMD:
            checksum_buf[checksum_buf_index++] = ch;
			link_cmd = ch;
            parse_step = MCULINK_WAIT_LEN;
			break;
		case MCULINK_WAIT_LEN:
            checksum_buf[checksum_buf_index++] = ch;
            data_len = ch;
            if(data_len > 0) {
                data_cnt = 0;
                parse_step = MCULINK_WAIT_DATA;
            } else {
                parse_step = MCULINK_WAIT_CHECKSUM;
            }
            break;
		case MCULINK_WAIT_DATA:
            checksum_buf[checksum_buf_index++] = ch;
            recv_data[data_cnt] = ch;
			data_cnt++;
			if(data_cnt >= data_len){
				parse_step = MCULINK_WAIT_CHECKSUM;
			}
			break;
		case MCULINK_WAIT_CHECKSUM:
            checksum = mcu_link_get_checksum(checksum_buf, data_len+MCULINK_PACKAGE_MIN_SIZE-2);
			if(ch == checksum) {
				ret = true;
			} else {
    			os_printf("checksum err! cal:%x get:%x\n", checksum, ch);
            }
			parse_step = MCULINK_WAIT_HEAD1;
			break;
		default:
			parse_step = MCULINK_WAIT_HEAD1;
			break;	
	}

	return ret;
}

uint8_t ICACHE_FLASH_ATTR mcu_link_msg_handle(void)
{
	uint8_t cmd = link_cmd;
	uint8_t param_len = data_len;
	uint8_t* param = recv_data;

	//os_printf("param_len:%d\n", param_len);

	switch(cmd) {
	case MCULINK_CONNECT:
		break;
	case MCULINK_UPDATE_APROM:
		break;		
	case MCULINK_UPDATE_CONFIG:
		break;
	case MCULINK_RUN_APROM:
		break;		
	default:break;					
	}

    mcu_boot_handle(cmd, param, param_len);

	return cmd;
}

void ICACHE_FLASH_ATTR mcu_link_set_recv(uint8_t* data, uint16_t len)
{
	memcpy(recv_buf, data, len);
	recv_buf_len = len;
}

void ICACHE_FLASH_ATTR mcu_link_update(void)
{
	uint8_t ch;
	for(uint8_t i=0; i<recv_buf_len; i++) {
		if(mcu_link_parse_char(recv_buf[i]))
		{
			// os_printf("get msg\n");
			mcu_link_msg_handle();
		}
		if(i+1 == recv_buf_len) {
			recv_buf_len = 0;
		}
	}
}

void ICACHE_FLASH_ATTR mcu_link_init(void)
{
    parse_step = MCULINK_WAIT_HEAD1;
    link_seq = 0;
}

