#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "crc32.h"

uint32_t *crcTable = NULL;

//Create a table for checksum calculation
void crc32CreateTable(){

  //Check if table is already available
  if(crcTable){
    ESP_LOGE("CRC32", "Table is already created");
    return;
  }

  crcTable = malloc(256 * sizeof(uint32_t));

  if(!crcTable){
    ESP_LOGE("CRC32", "Not enough memory to create table");
    return;
  }

    uint32_t entry = 0;

    for(uint32_t i = 0; i < 256; i++){
  		entry = i;

  		for (uint32_t j = 0; j < 8; j++){
  			if ((entry & 1) == 1)
  				entry = (entry >> 1) ^ CRC32_POLYNOMIAL;
  			else
  				entry = entry >> 1;
  		}

  		crcTable[i] = entry;
  }

  ESP_LOGI("CRC32", "Table is sucessfully created");
}


uint32_t crc32CalculateChecksum(uint8_t *buffer, int start, int length){
  uint32_t CRC = CRC32_SEED;

	for(int i = start; i < length; i++){
		CRC = (CRC >> 8) ^ crcTable[buffer[i] ^ (CRC & 0xff)];
	}

  return CRC ^ CRC32_SEED;
}
