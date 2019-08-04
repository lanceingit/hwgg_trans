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

char server_ip[4];
char dns_ip[4];
uint16_t server_port;
char server_domain[100];
bool use_ip;
DnsState dns_state=DNS_STATE_NO_FOUND;

bool is_wifi_connect=false;
uint32_t wifi_check_timer; 
uint32_t dns_check_timer; 
uint32_t server_conn_check_timer; 
static net_conn_cb* user_server_connect_cb;

bool ICACHE_FLASH_ATTR get_is_conncect_server(void) 
{
	return !(server_conn.state==ESPCONN_NONE || server_conn.state==ESPCONN_CLOSE);
}

void ICACHE_FLASH_ATTR net_set_ip(uint8_t* ip) 
{
	memcpy(server_ip, ip, 4);
}

void ICACHE_FLASH_ATTR net_set_port(uint16_t port) 
{
	server_port = port;
}

void ICACHE_FLASH_ATTR net_set_domain(char* domai) 
{
	strncpy(server_domain, domai, 100);
}

void ICACHE_FLASH_ATTR net_set_use_ip(bool use) 
{
	use_ip = use;
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

	if(server_conn.proto.tcp == NULL) {
		server_conn.proto.tcp = (esp_tcp *) os_zalloc(sizeof(esp_tcp));
		if(server_conn.proto.tcp == NULL) {
			os_printf("[WARN]conn alloc fail!\n");
			return;
		}  
	}
	server_conn.type = ESPCONN_TCP; 

	if(use_ip) {
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

	if(server_conn.proto.tcp == NULL) {
		server_conn.proto.tcp = (esp_tcp *) os_zalloc(sizeof(esp_tcp));
		if(server_conn.proto.tcp == NULL) {
			os_printf("[WARN]conn alloc fail!\n");
			return;
		}  
	}
	server_conn.type = ESPCONN_TCP; 

	if(use_ip) {
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
		if(!use_ip) {
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
		os_printf("server_conn.state:%d\n",server_conn.state);
		if((server_conn.state == ESPCONN_NONE || server_conn.state == ESPCONN_CLOSE) 
		    && is_wifi_connect 
			&& get_upgrade_state() == UPGRADE_STATE_IDLE
			&& (use_ip || dns_state==DNS_STATE_FOUND)) 
		{
			server_connect();
		}
		server_conn_check_timer = system_get_time();
	}
}

/*
  0-3: len=4   magic:0xABABFFDC
  4-7: len=4   ip
  8-9: len=2   port
   10: len=1   is_use_ip. =1 use ip; =0 use domain 
11-99: len=89  domain(string)
*/
void ICACHE_FLASH_ATTR net_init(net_conn_cb* cb)
{
    uint8_t buf[100];
    system_param_load(ADDR2SECTOR(NET_CONFIG_ADDR), 0, buf, 100); 	
	if(((uint32_t*)buf)[0] == NET_CONFIG_MAGIC) {
        memcpy(&server_ip, &buf[4], 4);
		server_port = buf[8]<<8 | buf[9];
		use_ip = buf[10];
		memcpy(server_domain, &buf[11], 89);
		server_domain[89] = 0;

		if(use_ip) {
			os_printf("net config ip:"IPSTR"\n", IP2STR(server_ip));
		} else {
			os_printf("net config domain:%s\n", server_domain);		
		}
		os_printf("net config port:%d\n", server_port);		
	} else {
		os_printf("net config corruption!\n");
	}

	server_conn.proto.tcp = NULL;
	user_server_connect_cb = cb;
}
