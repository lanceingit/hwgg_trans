/**
 *
 * timer.c
 *
 * simple timer, delay and time block function 
 */

#include "board.h"
#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include "timer.h"

#ifdef USE_RTC
#include "rtc.h"
#endif

static volatile times_t timer_cnt = 0;
static os_timer_t work_timer;

times_t ICACHE_FLASH_ATTR timer_now(void)
{
    return timer_cnt*US_PER_TICK;
}

times_t ICACHE_FLASH_ATTR timer_new(uint32_t us)
{
    return timer_now()+us;
}

bool ICACHE_FLASH_ATTR timer_is_timeout(volatile times_t* t)
{
    if(*t >= timer_now())
    {
        return false;
    }
    else
    {
        return true;
    }
}

static times_t ICACHE_FLASH_ATTR timer_passed(times_t* since)
{
    times_t now = timer_now();

    if(now >= *since) {
        return now-*since;
    }

    return now+(TIME_MAX-*since+1);
}

times_t ICACHE_FLASH_ATTR timer_elapsed(times_t* t)
{
	return timer_passed(t);
}

bool ICACHE_FLASH_ATTR timer_check(times_t* t, times_t us)
{
    if(timer_passed(t) > us) {
        *t = timer_now();
        return true;
    }
    return false;    
}

float ICACHE_FLASH_ATTR timer_get_dt(times_t* t, float max, float min)
{
	float dt = (*t > 0) ? (timer_passed(t) / 1000000.0f) : min;
	*t = timer_now();

	if (dt > max) {
		dt = max;
	}
	if (dt < min) {
		dt = min;
	}
    return dt;
}

void ICACHE_FLASH_ATTR delay(float s)
{
    volatile times_t wait;

    wait = timer_new((uint32_t)(s*1000*1000));
    while (!timer_is_timeout((times_t*)&wait));
}

void ICACHE_FLASH_ATTR delay_ms(uint32_t ms)
{
    volatile times_t wait;

    wait = timer_new(ms*1000);
    while (!timer_is_timeout((times_t*)&wait));
}

void ICACHE_FLASH_ATTR delay_us(uint32_t us)
{
    volatile times_t wait;

    wait = timer_new(us);
    while (!timer_is_timeout((times_t*)&wait));
}
  
void ICACHE_FLASH_ATTR sleep(float s)
{
    volatile times_t wait;

    wait = timer_new((uint32_t)(s*1000*1000));
    while (!timer_is_timeout(&wait));
}

void ICACHE_FLASH_ATTR timer_disable(void)
{
    os_timer_disarm(&work_timer);
}

void ICACHE_FLASH_ATTR timer_irs(void* arvg)
{    
    timer_cnt++;

#ifdef USE_RTC
    if(timer_cnt%(1000*1000/US_PER_TICK) == 0) {
        rtc_second_inc();
    }
#endif    
}

void ICACHE_FLASH_ATTR timer_init()
{   
	os_timer_setfn(&work_timer, timer_irs, NULL);
	os_timer_arm(&work_timer, US_PER_TICK/1000, 1);    
}

