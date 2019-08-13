#include "board.h"
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include <stdarg.h>

#include "protocol.h"
#include "net.h"
#include "uart_trans.h"
#include "version.h"
#include "timer.h"
#include "mcu_boot.h"
#include "upgrade_func.h"
#include "version_func.h"

#define RETRY_CNT 		3
#define RECV_BUF_ZISE	255

static enum parse_status parse_step = WAIT_HEAD1;
enum protocol_status protocol_step = STATUS_IDLE;

static uint16_t checksum;
// uint8_t protocol_dir;
static uint8_t data_len;
static uint8_t recv_data[256];
static uint8_t data_cnt;
static times_t last_send_time;
static uint8_t resend_try;
static uint8_t last_ch;
static uint8_t send_buf[256];
static uint8_t datalen;
static uint8_t protocol_buf[256];
static uint8_t recv_buf[256];
static uint8_t recv_buf_len;
static uint32_t recv_sn;

extern void exit_smartconfig(void);


bool ICACHE_FLASH_ATTR protocol_is_need_trans(uint8_t msg)
{
	if(msg==CMD_RECONNECT 
	|| msg==CMD_TRANS_VERSION
    || msg==CMD_MCU_UPGRADE
    || msg==CMD_MCU_UPGRADE_READY  
    || msg==CMD_MCU_UPGRADE_STATUS 
    || msg==CMD_MCU_UPGRADE_REQUEST
    || msg==CMD_GET_FIRMWARE_VER
	|| msg==CMD_REQUEST_UPGRADE_STATUS
	) {
		return false;
	} else {
		return true;
	}
}

uint16_t ICACHE_FLASH_ATTR protocol_get_checksum(uint8_t* data, uint16_t len)
{
	uint16_t sum=0;
	for(uint16_t i=0; i<len; i++) {
		sum += data[i];
	}

	return sum;
}

void ICACHE_FLASH_ATTR protocol_trans(uint8_t ch, uint8_t* data, uint8_t len)
{
	if(ch == PROTOCOL_CH_NET) {
		net_send(data, len);
	} else if(ch == PROTOCOL_CH_UART) {
		uart_trans_send(data, len);
	}
}	

void ICACHE_FLASH_ATTR protocol_encode(uint8_t ch, uint8_t* data, uint8_t len)
{
	protocol_buf[0] = HEAD1;
	protocol_buf[1] = HEAD2;
    if(ch==PROTOCOL_CH_UART) {
		protocol_buf[2] = RECV_MASK;
	} else {
		protocol_buf[2] = SEND_MASK;
	}
	protocol_buf[3] = BYTE3_MASK;
	protocol_buf[4] = 1+4+len;
	protocol_buf[5] = DEV_TYPE;
	uint32_t sn = recv_sn;
	protocol_buf[6] = (sn>>24)&0xFF;
	protocol_buf[7] = (sn>>16)&0xFF;
	protocol_buf[8] = (sn>>8)&0xFF;
	protocol_buf[9] = (sn)&0xFF;
	memcpy(&protocol_buf[10], data, len);
	uint16_t checksum = protocol_get_checksum(&protocol_buf[2], protocol_buf[4]+3);
	protocol_buf[10+len] = (checksum>>8)&0xFF;
	protocol_buf[11+len] = (checksum)&0xFF;
	protocol_buf[12+len] = TAIL;

	protocol_trans(ch, protocol_buf, protocol_buf[4]+8);
}

int8_t ICACHE_FLASH_ATTR protocol_send(uint8_t ch, uint8_t cmd, bool need_ack, ...)
{
	if(protocol_step != STATUS_IDLE) return -1;

	va_list args;
	va_start(args, need_ack); 
	datalen = 1;
	send_buf[0] = cmd;
	last_ch = ch;

	switch(cmd) {
	case CMD_RECONNECT:
		send_buf[1] = va_arg(args, int);
		datalen += 1;
		break;
	case CMD_TRANS_VERSION:
		send_buf[1] = va_arg(args, int);
		datalen += 1;
		break;		
	case CMD_MCU_UPGRADE:
		break;		
	case CMD_MCU_UPGRADE_REQUEST:
		send_buf[1] = va_arg(args, int);
		datalen += 1;	
		break;		
	case CMD_GET_FIRMWARE_VER:	
		break;		
	case CMD_REQUEST_UPGRADE_STATUS:
		send_buf[1] = va_arg(args, int);
		send_buf[2] = va_arg(args, int);
		datalen += 2;	
		break;	
	default:break;	
	}
	va_end(args);

	protocol_encode(ch, send_buf, datalen);
	if(need_ack) {
		protocol_step = STATUS_WAIT_RECV;
		last_send_time = timer_now();
	}
}

void ICACHE_FLASH_ATTR protocol_resend(void)
{
	protocol_encode(last_ch, send_buf, datalen);
	last_send_time = timer_now();	
}

bool ICACHE_FLASH_ATTR protocol_parse_char(uint8_t ch)
{
	bool ret = false;
	
	// os_printf("step:%d ch:%x\n", parse_step, ch);

	switch(parse_step){
		case WAIT_HEAD1:
			if(ch == HEAD1){
				parse_step = WAIT_HEAD2;
			}
			break;
		case WAIT_HEAD2:
			if(ch == HEAD2){
				parse_step = WAIT_DIR;
                checksum = 0;
			} else {
                parse_step = WAIT_HEAD1;
            } 
			break;            
		case WAIT_DIR:
            checksum += ch;
			// if(ch == SEND_MASK){
			// 	parse_step = WAIT_BYTE3;
			// } else {
            //     parse_step = WAIT_HEAD1;
            // } 
			parse_step = WAIT_BYTE3;
			break;
		case WAIT_BYTE3:
            checksum += ch;
            parse_step = WAIT_LEN;
			break;
		case WAIT_LEN:
            checksum += ch;
            data_len = ch;
            data_cnt = 0;
            parse_step = WAIT_DATA;
            break;
		case WAIT_DATA:
            checksum += ch;
            recv_data[data_cnt] = ch;
			data_cnt++;
			if(data_cnt >= data_len){
				parse_step = WAIT_CHECKSUM_H;
			}
			break;
		case WAIT_CHECKSUM_H:
			// os_printf("h:cal:%x get:%x\n", checksum, ch);
			if(ch == ((checksum >> 8) & 0xFF)){
				parse_step = WAIT_CHECKSUM_L;
			} else {
				parse_step = WAIT_HEAD1;
			}
			break;
		case WAIT_CHECKSUM_L:
			// os_printf("l:cal:%x get:%x\n", checksum, ch);
			if(ch == (checksum & 0xFF)) {
				ret = true;
			}
			parse_step = WAIT_HEAD1;
			break;
		default:
			parse_step = WAIT_HEAD1;
			break;	
	}

	return ret;
}

void ICACHE_FLASH_ATTR protocol_msg_timeout_handle(void)
{
	uint8_t cmd = send_buf[5];
	uint8_t* param = &send_buf[6];
	uint8_t param_len = datalen - 6;

	switch(cmd) {
		case CMD_MCU_UPGRADE_STATUS:
			mcu_boot_start();
			break;
		default:break;
	}
}

uint8_t ICACHE_FLASH_ATTR protocol_msg_handle(void)
{
	uint8_t cmd = recv_data[5];
	uint8_t* param = &recv_data[6];
	uint8_t param_len = data_len - 6;
	recv_sn = (recv_data[1]<<24)|(recv_data[2]<<16)|(recv_data[3]<<8)|(recv_data[4]);

	//os_printf("param_len:%d\n", param_len);

	switch(cmd) {
	case CMD_RECONNECT:
		os_printf("recv cmd:CMD_RECONNECT!\n");
		protocol_send(PROTOCOL_CH_UART, cmd, false, 1);
		//1.重连网络
		//net_abort();
		//2.软件复位
		system_restart();
		//3.看门狗复位
		//while(1);
		break;
	case CMD_TRANS_VERSION:
		os_printf("recv cmd:CMD_TRANS_VERSION!\n");
		os_printf("mcu version:%d\n", param[0]);
		set_mcu_ver(param[0]);
		protocol_send(PROTOCOL_CH_UART, cmd, false, version_get_major());
		break;		
	case CMD_MCU_UPGRADE:
		os_printf("recv cmd:CMD_MCU_UPGRADE!\n");
		protocol_step = STATUS_IDLE;
		set_mcu_need_upgrade();
		set_mcu_in_boot();
		protocol_send(PROTOCOL_CH_UART, CMD_MCU_UPGRADE_READY, false);  
		break;
	case CMD_MCU_UPGRADE_REQUEST:
		os_printf("recv cmd:CMD_MCU_UPGRADE_REQUEST!\n");
		if(param[0] == 2) {
			os_printf("recv force upgrade!\n");
			protocol_send(PROTOCOL_CH_UART, cmd, false, 1);
			set_upgrade_type(UPGRADE_TYPE_MCU_FORCE);
			set_request_latest_version();
		}
		else if(param[0] == 1) {
			os_printf("recv request upgrade!\n");
			exit_smartconfig();
			set_upgrade_type(UPGRADE_TYPE_MCU_REQUEST);
			set_request_latest_version();
//			protocol_send(PROTOCOL_CH_UART, cmd, false, 1);
//			mcu_boot_start();
		}
		break;
	case CMD_MCU_UPGRADE_STATUS:	
		os_printf("recv cmd:CMD_MCU_UPGRADE_STATUS!\n");
		protocol_step = STATUS_IDLE;
		set_mcu_upgrade_success();
		break;
	case CMD_GET_FIRMWARE_VER:
		os_printf("recv cmd:CMD_GET_FIRMWARE_VER!\n");
		protocol_step = STATUS_IDLE;
		os_printf("latest version:%d.%d.%d\n", param[0], param[1], param[2]);
		set_upgrade_get_version(param);
		break;	
	case CMD_REQUEST_UPGRADE_STATUS:
	{
		os_printf("recv cmd:CMD_REQUEST_UPGRADE_STATUS!\n");
		uint8_t state = get_upgrade_state();
		os_printf("state:%d!\n", state);
		static uint8_t percent = 0;
		if(state==UPGRADE_STATE_WAIT_PERCENT1) {
			state = 0;
			percent = 5;
		}
		else if(state==UPGRADE_STATE_CONNECTING || state==UPGRADE_STATE_DOWNLOAD) {
			state = 0;
			percent = 10;
		}
		else if(state==UPGRADE_STATE_WAIT_PERCENT2) {
			state = 1;
			percent = 30;
		}
		else if(state==UPGRADE_STATE_WAIT_PERCENT3) {
			state = 1;
			percent = 80;
		} 
		else if(state==UPGRADE_STATE_IDLE||state==UPGRADE_STATE_UPDATE_END||state==UPGRADE_STATE_WAIT_PERCENT4) {
			state = 2;
			percent = 100;
		}
		if(get_is_mcu_in_boot()) {
			state = 1;
			percent = 50;
		}
		protocol_send(PROTOCOL_CH_NET, cmd, false, 1, percent);
		set_get_percent();
		break;
	}
	default:break;					
	}

	return cmd;
}

void ICACHE_FLASH_ATTR protocol_set_recv(uint8_t* data, uint8_t len)
{
	memcpy(recv_buf, data, len);
	recv_buf_len = len;
}

bool ICACHE_FLASH_ATTR protocol_msg_parse(uint8_t* data, uint16_t len, uint8_t* msg_id)
{
	uint8_t ch;
	for(uint16_t i=0; i<len; i++) {
		if(protocol_parse_char(data[i]))
		{
			// os_printf("get msg\n");
			*msg_id = protocol_msg_handle();
			return true;
		}
	}
	return false;
}

void ICACHE_FLASH_ATTR protocol_update(void)
{
	uint8_t msg;
	if(protocol_msg_parse(recv_buf, recv_buf_len, &msg)) {
		if(protocol_is_need_trans(msg)) {
			uart_trans_send(recv_buf, recv_buf_len);
		}
		recv_buf_len = 0;
	}

	// uint8_t ch;
	// for(uint8_t i=0; i<recv_buf_len; i++) {
	// 	if(protocol_parse_char(recv_buf[i]))
	// 	{
	// 		// os_printf("get msg\n");
	// 		protocol_msg_handle();
	// 	}
	// 	if(i+1 == recv_buf_len) {
	// 		recv_buf_len = 0;
	// 	}
	// }

    if(protocol_step == STATUS_WAIT_RECV)
    {
		if(timer_check(&last_send_time, 1*1000*1000)) {
			if(resend_try < RETRY_CNT)
			{
				protocol_resend();
				resend_try++; 
			} else {
				protocol_msg_timeout_handle();
				protocol_step = STATUS_IDLE;
			}
		}		
    }	
}

void ICACHE_FLASH_ATTR protocol_init(void)
{
    parse_step = WAIT_HEAD1;
}

