#include "ets_sys.h"

#define VERSION_MAJOR    2      
#define VERSION_MINOR    0            

static uint8_t mcu_ver;

void ICACHE_FLASH_ATTR set_mcu_ver(uint8_t ver)
{
    mcu_ver = ver;
}

uint8_t ICACHE_FLASH_ATTR get_mcu_ver(void)
{
    return mcu_ver;
}

uint8_t ICACHE_FLASH_ATTR version_get_major(void)
{
    return VERSION_MAJOR;
}

uint8_t ICACHE_FLASH_ATTR version_get_minor(void)
{
    return VERSION_MINOR;
}
