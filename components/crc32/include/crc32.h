#ifndef _CRC32_H_
#define _CRC32_H_

  #include "freertos/FreeRTOS.h"

  #define CRC32_POLYNOMIAL    0xedb88320
  #define CRC32_SEED          0xffffffff

  void crc32CreateTable();
  uint32_t crc32CalculateChecksum(uint8_t *buffer, int start, int length);

#endif
