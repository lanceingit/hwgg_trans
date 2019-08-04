#include "user_interface.h"
#include "osapi.h"
#include "user_interface.h"
#include "espconn.h"

#include "secure.h"

static int crcValue;

void ICACHE_FLASH_ATTR crc_updateChecksum(char data) 
{
    data = data & 0xff; //cast because we want an unsigned type
    short tmp = data ^ (crcValue & 0xff);
    tmp = (tmp ^ (tmp << 4)) & 0xff;
    crcValue = ((crcValue >> 8) & 0xff) ^ (tmp << 8) ^ (tmp << 3) ^ ((tmp >> 4) & 0xf);
}

void ICACHE_FLASH_ATTR crc_startChecksum() 
{
    crcValue = 0xffff;
}

void ICACHE_FLASH_ATTR crc_endChecksum() 
{
    crcValue ^= 0xffff;
}

int ICACHE_FLASH_ATTR crc_calculateCRC(int* data, int len) 
{
    crc_startChecksum();
    for (int i = 0; i < len; i++) {
        crc_updateChecksum(data[i]);
    }
    crc_endChecksum();
    return crcValue;
}

int ICACHE_FLASH_ATTR crc_get()
{
    return crcValue;
}

uint8_t ICACHE_FLASH_ATTR bin_decrypt(uint8_t data, uint32_t pos)
{
    uint8_t bit_pos;
    uint8_t out;

    bit_pos = pos & 0x07; 
    out = data ^ (1 << bit_pos);
    out ^= BIN_SECURE_KEY;

    return out;    
}

void ICACHE_FLASH_ATTR bin_decrypt_buf(uint8_t* decy_data, uint8_t* ency_data, uint32_t len)
{
    for(uint32_t i=0; i<len; i++) {
        decy_data[i] = bin_decrypt(ency_data[i], i+1);
    }
}
