#include "ets_sys.h"
#include "osapi.h"
#include "smartconfig.h"
#include "user_interface.h"

#include "smartconfig_api.h"

config_callback* config_cb;

void ICACHE_FLASH_ATTR smartconfig_done(sc_status status, void *pdata)
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
    return smartconfig_start(smartconfig_done, 1);
}

bool ICACHE_FLASH_ATTR smartconfig_end(void)
{
    return smartconfig_stop();
}

