#pragma once

#define BIN_SECURE_KEY      'H'

uint8_t bin_decrypt(uint8_t data, uint32_t pos);
void bin_decrypt_buf(uint8_t* decy_data, uint8_t* ency_data, uint32_t len);

void crc_updateChecksum(char data);
void crc_startChecksum();
void crc_endChecksum();
int crc_calculateCRC(int* data, int len);
int crc_get();
