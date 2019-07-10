#include "ets_sys.h"
#include "osapi.h"
#include "smartconfig.h"
#include "user_interface.h"

#include <stdlib.h>

#include "smartconfig_api.h"
#include "board.h"
#include "net.h"

#include "mem.h"

config_callback* config_cb;


#define BUF_SIZE_MAX   		(32+64)	
#define SSID_SIZE_MAX  		32
#define PASSWORD_SIZE_MAX  	64
#define DOMAIN_SIZE_MAX  	90
#define IP_SIZE_MAX  		15
#define PORT_SIZE_MAX  		5

static char ssid[SSID_SIZE_MAX+1] = {0};
static char password[PASSWORD_SIZE_MAX+1] = {0};
static char domain[DOMAIN_SIZE_MAX+1] = {0};
static char ip[IP_SIZE_MAX+1] = {0};
static char port[PORT_SIZE_MAX] = {0};


/*
{  ssid },{  password  },{  domain   },{ip},{port}
{Netcore},{tcl123698745},{jl.hwei.net},{  },{8960}
*/
bool smartconfig_decode(char* data, char* ssid, char* password, char* domain, char* ip, char* port)
{
	char buf[BUF_SIZE_MAX+1] = {0};
	uint8_t i=0;
	uint8_t cnt=0;
	
	while(data[i] != '{') {
		i++;
		if(i>=BUF_SIZE_MAX) {
			goto decode_err;
		}
	}
	i++;

	//ssid
	while(!(data[i] == '}' && data[i+1]==',' && data[i+2]=='{')) {
		buf[cnt] = data[i];
		i++;
		cnt++;
		if(i>=BUF_SIZE_MAX-3 || cnt >= SSID_SIZE_MAX) {
			goto decode_err;
		}
	}
	strcpy(ssid, buf);
	memset(buf, 0, BUF_SIZE_MAX);

	i+=3;
	cnt=0;
	//password
	while(!(data[i] == '}' && data[i+1]==',' && data[i+2]=='{')) {
		buf[cnt] = data[i];
		i++;
		cnt++;
		if(i>=BUF_SIZE_MAX-3 || cnt >= PASSWORD_SIZE_MAX) {
			goto decode_err;
		}
	}
	strcpy(password, buf);
	memset(buf, 0, BUF_SIZE_MAX);

	i+=3;
	cnt=0;
	//domain
	while(!(data[i] == '}' && data[i+1]==',' && data[i+2]=='{')) {
		buf[cnt] = data[i];
		i++;
		cnt++;
		if(i>=BUF_SIZE_MAX-3 || cnt >= DOMAIN_SIZE_MAX) {
			goto decode_err;
		}
	}	
	strcpy(domain, buf);
	memset(buf, 0, BUF_SIZE_MAX);

	i+=3;
	cnt=0;
	//ip
	while(!(data[i] == '}' && data[i+1]==',' && data[i+2]=='{')) {
		buf[cnt] = data[i];
		i++;
		cnt++;
		if(i>=BUF_SIZE_MAX-3 || cnt >= IP_SIZE_MAX) {
			goto decode_err;
		}
	}	
	strcpy(ip, buf);
	memset(buf, 0, BUF_SIZE_MAX);
	
	i+=3;
	cnt=0;
	//port
	while(data[i] != '}') {
		buf[cnt] = data[i];
		i++;
		cnt++;
		if(i>=BUF_SIZE_MAX || cnt >= PORT_SIZE_MAX) {
			break;
			goto decode_err;
		}
	}	
	strcpy(port, buf);

	os_printf("ssid:%s\n", ssid);
	os_printf("password:%s\n", password);
	os_printf("domain:%s\n", domain);
	os_printf("ip:%s\n", ip);
	os_printf("port:%s\n", port);

	return true;

decode_err:
	os_printf("decode err! index:%d  cnt:%d\n", i, cnt);
    return false;
}

void ip_atoi(char* ip_str, uint8_t* ip_int)
{
	char ip_char[4] = {'\0'};
    uint8_t ip_int_index = 0;
    uint8_t ip_char_index =0;
    for(uint8_t i=0; i<=strlen(ip_str); i++) {
        if (ip_str[i]=='\0' || ip_str[i]=='.') {			
            ip_int[ip_int_index++] = (uint8_t)atoi(ip_char);	
            memset(ip_char, 0, 4);
            ip_char_index=0;
            continue;
        }
        ip_char[ip_char_index++] = ip_str[i];
    }	
}

void ICACHE_FLASH_ATTR smartconfig_done_cb(sc_status status, void *pdata)
{
    switch(status) {
        case SC_STATUS_WAIT:
            os_printf("SC_STATUS_WAIT\n");
            break;
        case SC_STATUS_FIND_CHANNEL:
            os_printf("SC_STATUS_FIND_CHANNEL\n");
            break;
        case SC_STATUS_GETTING_SSID_PSWD:
            os_printf("SC_STATUS_GETTING_SSID_PSWD\n");
			sc_type *type = pdata;
            if (*type == SC_TYPE_ESPTOUCH) {
                os_printf("SC_TYPE:SC_TYPE_ESPTOUCH\n");
            } else {
                os_printf("SC_TYPE:SC_TYPE_AIRKISS\n");
            }
            break;
        case SC_STATUS_LINK:
            os_printf("SC_STATUS_LINK\n");
            struct station_config *sta_conf = pdata;
            // uint8 ssid[32];
            // uint8 password[64];        
            // uint8 bssid_set;
            // uint8 bssid[6];    
            for(int i=0; i<32; i++) {
                os_printf("%02x ", ((char*)pdata)[i]);
            }
            for(int i=0; i<64; i++) {
                os_printf("%02x ", ((char*)pdata)[32+i]);
            }
            os_printf("\n");
            os_printf("bssid_set:%d bssid:", ((struct station_config*)pdata)->bssid_set);
            for(int i=0; i<6; i++) {
                os_printf("%02x ", ((struct station_config*)pdata)->bssid[i]);
            }
            os_printf("\n");

            if(smartconfig_decode(pdata, ssid, password, domain, ip, port)) {
                memcpy(sta_conf->ssid, ssid, 32);
                sta_conf->ssid[31] = 0;
                memcpy(sta_conf->password, password, 64);
                sta_conf->password[63] = 0;

                /*
                0-3:   len=4   magic:0xABABFFDC
                4-7:   len=4   ip
                8-9:   len=2   port
                10:    len=1   is_use_ip. =1 use ip; =0 use domain 
                11-99: len=89  domain(string)
                */
                uint8_t* buf = os_malloc(100);
                if(buf != NULL) {
                    ((uint32_t*)buf)[0] = NET_CONFIG_MAGIC;
                    ip_atoi(ip, &buf[4]);
                    os_printf("ip convert:"IPSTR"\n", IP2STR(&buf[4]));
                    uint16_t port_num = atoi(port);
                    os_printf("port convert:%d\n", port_num);
                    buf[8] = port_num>>8;
                    buf[9] = port_num&0xFF;
                    if(strlen(ip) == 0) {
                        buf[10] = 0;
                    } else {
                        buf[10] = 1;
                    }
                    memcpy(&buf[11], domain, 89);

                    net_set_use_ip(buf[10]);
                    if(buf[10]) {
                        net_set_ip(&buf[4]);
                    } else {
                        net_set_domain(domain);
                    }
                    net_set_port(port_num);

                    if(system_param_save_with_protect(NET_CONFIG_SECTOR, buf, 100) == false) {
                        os_printf("net config save fail!\n");    
                    }   
                    os_free(buf);
                }               
            }
            wifi_station_set_config(sta_conf);
            wifi_station_disconnect();
            wifi_station_connect();
            break;
        case SC_STATUS_LINK_OVER:
            os_printf("SC_STATUS_LINK_OVER\n");
            if (pdata != NULL) {
				//SC_TYPE_ESPTOUCH
                uint8 phone_ip[4] = {0};

                os_memcpy(phone_ip, (uint8*)pdata, 4);
                os_printf("Phone ip: %d.%d.%d.%d\n",phone_ip[0],phone_ip[1],phone_ip[2],phone_ip[3]);
            }
            if(smartconfig_stop() == true) {
                if(config_cb != NULL) {
                    config_cb(NULL);
                }
			}
            break;
    }
	
}

bool ICACHE_FLASH_ATTR smartconfig_begin(config_callback* cb)
{
    config_cb = cb;
    wifi_set_opmode(STATION_MODE);
    smartconfig_set_type(SC_TYPE_ESPTOUCH);
    return smartconfig_start(smartconfig_done_cb, 1);
}

bool ICACHE_FLASH_ATTR smartconfig_end(void)
{
    return smartconfig_stop();
}

