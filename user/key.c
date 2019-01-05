#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "gpio.h"

#include "board.h"

uint32_t key_press_time;
uint32_t key_release_time;
static bool key_short_press=false;
static bool key_long_press=false;

bool ICACHE_FLASH_ATTR key_is_short_press(void)
{
    return key_short_press;
}

bool ICACHE_FLASH_ATTR key_is_long_press(void)
{
    return key_long_press;
}

bool ICACHE_FLASH_ATTR key_is_press(void)
{
    return (GPIO_INPUT_GET(KEY_GPIO) == KEY_PRESS_LEVEL);
}

void ICACHE_FLASH_ATTR key_update(void)
{
    if(key_is_press()) {
        key_release_time = system_get_time();	
    } else {
        key_press_time = system_get_time();		
    }

	if((system_get_time() - key_press_time) > (KEY_ACTION_TIME_MS*1000))
	{
		if(key_short_press == false) {
			os_printf("key press\n");
		}
		key_short_press = true;
	}

	if((system_get_time() - key_release_time) > (KEY_ACTION_TIME_MS*1000))
	{
		if(key_short_press == true) {
			os_printf("key release\n");
		}
		key_long_press = false;
		key_short_press = false;
	}    

	if((system_get_time() - key_press_time) > (KEY_LONG_PRESS_MS*1000)) {
		if(key_long_press == false) {
			os_printf("key long press\n");
		}
		key_long_press = true;
	}	
}

void ICACHE_FLASH_ATTR key_init(void)
{
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, 3);
    GPIO_DIS_OUTPUT(KEY_GPIO);
}



