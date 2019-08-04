#pragma once

#define ADDR2SECTOR(a)   ((a)>>12)
#define BIG32(a)         (((uint8_t*)(a))[0]<<24| \
                          ((uint8_t*)(a))[1]<<16| \
                          ((uint8_t*)(a))[2]<<8| \
                          ((uint8_t*)(a))[3])

#define BIG16(a)          (((uint8_t*)(a))[0]<<8|((uint8_t*)(a))[1])

