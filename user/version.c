#include "ets_sys.h"

#define VERSION_MAJOR    17      
#define VERSION_MINOR    0            


uint8_t version_get_major(void)
{
    return VERSION_MAJOR;
}

uint8_t version_get_minor(void)
{
    return VERSION_MINOR;
}
