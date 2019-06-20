#include "board.h"
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include <stdarg.h>

#include "protocol.h"
#include "net.h"
#include "uart_trans.h"

#define RETRY_CNT 		3
#define RECV_BUF_ZISE	255

enum parse_status parse_step = WAIT_HEAD1;

uint16_t checksum;
// uint8_t protocol_dir;
uint8_t data_len;
uint8_t recv_data[256];
uint8_t data_cnt;
uint8_t resend_try;
uint8_t send_buf[256];
uint8_t datalen;
uint8_t protocol_buf[256];
uint8_t recv_buf[256];
uint8_t recv_buf_len;
uint32_t recv_sn;

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
	protocol_buf[2] = SEND_MASK;
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
	va_list args;
	va_start(args, need_ack); 
	datalen = 1;
	send_buf[0] = cmd;

	switch(cmd) {
	case CMD_RECONNECT:
		send_buf[1] = va_arg(args, int);
		datalen += 1;
		break;
	default:break;	
	}
	va_end(args);

	protocol_encode(ch, send_buf, datalen);
	// if(need_ack) {
	// 	last_send_time = timer_now();
	// }
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
			if(ch == SEND_MASK){
				parse_step = WAIT_BYTE3;
			} else {
                parse_step = WAIT_HEAD1;
            } 
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
	default:break;					
	}

	return cmd;
}

void ICACHE_FLASH_ATTR protocol_set_recv(uint8_t* data, uint16_t len)
{
	memcpy(recv_buf, data, len);
	recv_buf_len = len;
}

bool protocol_msg_parse(uint8_t* data, uint16_t len, uint8_t* msg_id)
{
	uint8_t ch;
	for(uint8_t i=0; i<len; i++) {
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
	uint8_t ch;
	for(uint8_t i=0; i<recv_buf_len; i++) {
		if(protocol_parse_char(recv_buf[i]))
		{
			// os_printf("get msg\n");
			protocol_msg_handle();
		}
		if(i+1 == recv_buf_len) {
			recv_buf_len = 0;
		}
	}
}

void ICACHE_FLASH_ATTR protocol_init(void)
{
    parse_step = WAIT_HEAD1;
}

