/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2016 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on ESPRESSIF SYSTEMS ESP8266 only, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "ets_sys.h"
#include "osapi.h"
#include "mem.h"
#include "user_interface.h"

#include "board.h"
#include "key.h"
#include "smartconfig_api.h"
#include "uart_trans.h"
#include "net.h"



bool is_in_smartconfig = false;
bool last_is_key_long_press = false;
os_timer_t main_timer;

void ICACHE_FLASH_ATTR smartconfig_callback(void* param)
{
	is_in_smartconfig = false;	
}

void ICACHE_FLASH_ATTR key_func(void)
{
	bool is_key_long_press;

	key_update();

	is_key_long_press = key_is_long_press();
	if(is_key_long_press != last_is_key_long_press) {
		if(is_key_long_press) {
			if(is_in_smartconfig) {
				if(smartconfig_end() == true) {
					is_in_smartconfig = false;	
				}
			} else {
				if(smartconfig_begin(smartconfig_callback) == true) {
					is_in_smartconfig = true;
				}
			}
		}	
	}
	last_is_key_long_press = is_key_long_press;
}

void ICACHE_FLASH_ATTR net_func(void)
{
	net_update();
}

void uart_func(void)
{
	uart_trans_update();
}

void ICACHE_FLASH_ATTR main_func(void* arvg)
{
	key_func();
	net_func();
	uart_func();
}

void ICACHE_FLASH_ATTR user_init(void)
{
	uart_trans_init();	
    os_printf("SDK version:%s\n", system_get_sdk_version());

	key_init();

	os_timer_setfn(&main_timer, main_func, NULL);
	os_timer_arm(&main_timer, MAIN_LOOP_MS, 1);	
}
