#include "user_interface.h"
#include "osapi.h"
#include "espconn.h"
#include "ip_addr.h"
#include "mem.h"

#include "board.h"
#include "uart_trans.h"
#include "net.h"
#include "utils.h"
#include "protocol.h"
#include "http_func.h"
#include "mcu_boot.h"
#include "upgrade_func.h"

struct espconn server_conn;

const char server_ip[4] = SERVER_IP;
char dns_ip[4];
const uint16_t server_port=SERVER_PORT;
const char server_domain[]=SERVER_DOMAIN;
DnsState dns_state=DNS_STATE_NO_FOUND;

bool is_wifi_connect=false;
uint32_t wifi_check_timer; 
uint32_t dns_check_timer; 
uint32_t server_conn_check_timer; 
static net_conn_cb* user_server_connect_cb;
static esp_tcp tcp_buf;

bool ICACHE_FLASH_ATTR get_is_conncect_server(void) 
{
	return !(server_conn.state==ESPCONN_NONE || server_conn.state==ESPCONN_CLOSE);
}

void ICACHE_FLASH_ATTR net_dns_found_cb(const char *name, ip_addr_t* ipaddr, void* arg)
{
	struct espconn* pespconn = (struct espconn*)arg;

	if(ipaddr != NULL) {
		os_printf("dns ip:"IPSTR"\n", IP2STR(&ipaddr->addr));
		memcpy(dns_ip, &ipaddr->addr, 4);
		dns_state = DNS_STATE_FOUND;
	} else {
		os_printf("dns ip err!\n");
		dns_state = DNS_STATE_NO_FOUND;
	}
}

void ICACHE_FLASH_ATTR net_send(uint8_t* pdata, uint16_t len)
{
	//if(server_conn.state==ESPCONN_NONE || server_conn.state==ESPCONN_CLOSE) return; 

	os_printf("tcp send:");
	for(uint16_t i=0; i<len; i++) {
		os_printf("%02x ", pdata[i]);
		// uart_trans_send(pdata[i]);
	}
	os_printf("\n");	

	for(uint16_t i=0; i<len; i++) {
		os_printf("%c", pdata[i]);
	}
	os_printf("\n");		
	espconn_sent(&server_conn, pdata, len);	
}

void tcp_recv_cb(void *arg, char *pdata, unsigned short len) 
{
// 	os_printf("tcp recv:");
// 	for(uint16_t i=0; i<len; i++) {
// 		os_printf("%02x ", pdata[i]);
// //		uart_trans_send(pdata[i]);
// 	}
// 	os_printf("\n");
	http_handle(pdata, len);

	//uart_trans_send(pdata, len);
	protocol_set_recv(pdata, len);
}

void ICACHE_FLASH_ATTR tcp_send_cb(void *arg)  
{
	os_printf("tcp send success!\n");
}

void ICACHE_FLASH_ATTR tcp_disconnect_cb(void *arg)  
{
	os_printf("tcp disconnect success!\n");
}

void ICACHE_FLASH_ATTR server_connect_cb(void *arg)  
{
	struct espconn *conn = arg;
	espconn_regist_recvcb(conn, tcp_recv_cb); 
	espconn_regist_sentcb(conn, tcp_send_cb);  
	espconn_regist_disconcb(conn, tcp_disconnect_cb);

	if(user_server_connect_cb != NULL) {
		user_server_connect_cb(NULL);
	}

#if 1 //for test
	if(get_is_mcu_in_upgrade()) {
		uint8_t mac[6];
		wifi_get_macaddr(STATION_IF, mac);
		os_printf("%d macaddr:%x %x %x %x %x %x\n"
										,mac[0]
										,mac[1]
										,mac[2]
										,mac[3]
										,mac[4]
										,mac[5]
										);

		uint8_t buf[]={0x46,0xB9,0x68,0x00,0x0C,0x00,0xF0,0x00,0x00,0x0E,0x01,0x84,0xf3,0xeb,0x74,0x02,0x79,0x04,0xc5,0x16};
		for(uint8_t i=0; i<6; i++)
		{
			buf[11+i] = mac[i];
		}
		uint16_t sum=0;
		for(uint8_t i=0; i<15; i++)
		{
			sum += buf[2+i];
		}
		buf[17] = (uint8_t)(sum>>8);
		buf[18] = (uint8_t)(sum&0xFF);
		os_printf("send:");
		for(uint8_t i=0; i<sizeof(buf); i++)
		{
			os_printf("%02x ", buf[i]);
		}
		os_printf("\n");
		espconn_sent(conn, buf, sizeof(buf));
	}
#endif	
	
	os_printf("server connect success!\n");

	//http_get_test();
}

void ICACHE_FLASH_ATTR tcp_reconnect_cb(void *arg, sint8 err) 
{
	os_printf("connect err:%d!\n", err);
	espconn_connect((struct espconn *) arg);
}

void ICACHE_FLASH_ATTR server_abort(void) 
{
	espconn_abort(&server_conn);
}

void ICACHE_FLASH_ATTR server_connect(void) 
{
	struct ip_info local_ip;

	server_conn.type = ESPCONN_TCP; 

	if(USE_IP) {
		memcpy(server_conn.proto.tcp->remote_ip, server_ip, 4);
	} else {
		memcpy(server_conn.proto.tcp->remote_ip, dns_ip, 4);
	}
	server_conn.proto.tcp->remote_port = server_port;  

	wifi_get_ip_info(STATION_IF, &local_ip);
	memcpy(server_conn.proto.tcp->local_ip, &local_ip, 4);
	server_conn.proto.tcp->local_port = espconn_port();  

	espconn_regist_connectcb(&server_conn, server_connect_cb);
	espconn_regist_reconcb(&server_conn, tcp_reconnect_cb);

	espconn_connect(&server_conn);
}

void ICACHE_FLASH_ATTR server_connect_port(uint16_t port, espconn_connect_callback connect_cb) 
{
	struct ip_info local_ip;

	server_conn.type = ESPCONN_TCP; 

	if(USE_IP) {
		memcpy(server_conn.proto.tcp->remote_ip, server_ip, 4);
	} else {
		memcpy(server_conn.proto.tcp->remote_ip, dns_ip, 4);
	}
	server_conn.proto.tcp->remote_port = port;  

	wifi_get_ip_info(STATION_IF, &local_ip);
	memcpy(server_conn.proto.tcp->local_ip, &local_ip, 4);
	server_conn.proto.tcp->local_port = espconn_port();  

	espconn_regist_connectcb(&server_conn, connect_cb);
	espconn_regist_reconcb(&server_conn, tcp_reconnect_cb);

	espconn_connect(&server_conn);
}

void ICACHE_FLASH_ATTR net_update(void)
{
	if(system_get_time() - wifi_check_timer > (1000*WIFI_CHECK_MS)) {
		if(wifi_station_get_connect_status() == STATION_GOT_IP) {
			if(!is_wifi_connect) {
				os_printf("wifi connect ok!\n");
			}
			is_wifi_connect = true;
		} else {
			is_wifi_connect = false;
		}
		wifi_check_timer = system_get_time();
	}

	if(system_get_time() - dns_check_timer > (1*1000)) {
		if(!USE_IP) {
			if(is_wifi_connect && dns_state==DNS_STATE_NO_FOUND) {
				ip_addr_t esp_server_ip;
				struct espconn	conn;
				dns_state = DNS_STATE_FOUNDING;
				espconn_gethostbyname(&conn, server_domain, &esp_server_ip, net_dns_found_cb);
			}
		}
		dns_check_timer = system_get_time();
	}

	if(system_get_time() - server_conn_check_timer > (SERVER_CONN_CHECK_MS*1000)) {
		os_printf("port:%d server_conn.state:%d\n",server_conn.proto.tcp->remote_port, server_conn.state);
		if((server_conn.state == ESPCONN_NONE || server_conn.state == ESPCONN_CLOSE) 
		    && is_wifi_connect 
			&& get_upgrade_state() == UPGRADE_STATE_IDLE
			&& (USE_IP || dns_state==DNS_STATE_FOUND)) 
		{
			server_connect();
		}
		server_conn_check_timer = system_get_time();
	}
}

void ICACHE_FLASH_ATTR net_init(net_conn_cb* cb)
{
	server_conn.proto.tcp = NULL;
	user_server_connect_cb = cb;
	server_conn.proto.tcp = &tcp_buf;
}
