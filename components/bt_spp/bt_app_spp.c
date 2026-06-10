#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_avrc_api.h"
#include "bt_app_spp.h"
#include "esp_spp_api.h"
#include "esp_flash.h"
#include "spi_flash_mmap.h"
#include "crc32.h"
#include "esp_flash_encrypt.h"
#include "bt_player.h"

//If this is a debug build it will contain debug flag in firmware signature
#define MD_RELEASE_SIGNATURE  0x0
#define MD_DEBUG_SIGNATURE    0x1

//Set a build signature
#if CONFIG_DEBUG_SIGNATURE
  #define MD_BUILD_SIGNATURE  MD_DEBUG_SIGNATURE
#else
  #define MD_BUILD_SIGNATURE  MD_RELEASE_SIGNATURE
#endif

#define MD_BUILD_NUMBER CONFIG_MD_BUILD_NUMBER

#define MD_REQUEST      "XMD"
#define MD_REPLY        "RMD"
#define MD_IDENTIFIER   "MediaDev"
#define MD_MODEL        CONFIG_MD_MODEL
#define MD_REVISION     CONFIG_MD_HARDWARE_REVISION
#define MD_FIRMWARE     CONFIG_MD_FIRMWARE_VERSION
#define MD_PROTOCOL     0x12

#define MD_RESPONSE_ERROR           "E"   //Undefined error
#define MD_RESPONSE_OK              "K"   //Operation was successful

#define MD_WRITE_WRONG_FORMAT       "W"   //Binary is not meant for this device
#define MD_WRITE_WRONG_CHECKSUM     "X"   //Wrong checksum, resend data

#define MD_CMD_IDENTIFIER   'I'
#define MD_CMD_MODEL        'M'
#define MD_CMD_REVISION     'H'
#define MD_CMD_FIRMWARE     'V'
#define MD_CMD_PROTOCOL     'P'
#define MD_CMD_WRITE        'W'
#define MD_CMD_VERIFY       'F'
#define MD_CMD_REBOOT       'R'
#define MD_CMD_PARTITION    'U'
#define MD_CMD_NOTIFICATION 'N'     //Added in protocol 1.1
#define MD_CMD_REGISTER_EVT 'J'     //Added in protocol 1.1
#define MD_CMD_EVENT        'L'     //Added in protocol 1.1

uint8_t activeEvents = 0x0;

//Output text buffer, for SPP
uint8_t out_text[32];

const esp_partition_t *update_partition = NULL;   //Pointer to partition table
esp_partition_t upd_partition;                    //Copy of the updating table, for raw OTA writes
bool reboot_unlock = false;                       //Do not allow reboots by default, only after successful update

uint8_t partition_index = 0;      //Index of the update partition, for update client

uint32_t sppHandle = 0;

void disableEvents(){
  activeEvents = 0;
}

//Send a registered event to the companion app
void sppSendEvent(uint8_t eventType, uint8_t eventCode){
  //Check if the event type is registered by the app
  if((eventType & activeEvents) == 0) return;

  int text_len = 8;

  memset(out_text, 0, 32);
  memcpy(out_text, MD_REPLY, 3);

  out_text[3] = MD_CMD_EVENT;
  out_text[4] = eventType;
  out_text[5] = eventCode;

  //Add delimiter
  out_text[6] = '\r';
  out_text[7] = '\n';

  if(sppHandle > 0) esp_spp_write(sppHandle, text_len, out_text);

}

//Check if the supplied header is of a real image
void validImageCheck(uint32_t write_handle, uint8_t *encrypted_cache, uint32_t dataLen){
  esp_err_t err;

  //Always erase first sector
  err = esp_partition_erase_range(update_partition, 0, SPI_FLASH_SEC_SIZE);
  if (err != ESP_OK) {
    app_spp_send_string(MD_CMD_WRITE, MD_RESPONSE_ERROR, write_handle);
    return;
  }

  //Regardless of the enabled encryption, we write data as is
  upd_partition.encrypted = false;
  err = esp_partition_write(&upd_partition, 0, (void *) encrypted_cache, dataLen);
  if (err != ESP_OK) {
    app_spp_send_string(MD_CMD_WRITE, MD_RESPONSE_ERROR, write_handle);
    return;
  }else{
    uint8_t decrypted_cache[96] = {0};

    //Decrypted bytes are needed so decrypt if necessary
    //upd_partition.encrypted = esp_flash_encryption_enabled();
    err = esp_partition_read(update_partition, 0, (void *) &decrypted_cache[0], dataLen);
    if (err != ESP_OK) {
      app_spp_send_string(MD_CMD_WRITE, MD_RESPONSE_ERROR, write_handle);
      return;
    }else if(decrypted_cache[0] != 0xE9){
      app_spp_send_string(MD_CMD_WRITE, MD_WRITE_WRONG_FORMAT, write_handle);
      return;
    }
  }

  //Disable A2DP sink so it doesn't interfere with the flashing process
  disconnectTarget();

  //Write went OK
  app_spp_send_string(MD_CMD_WRITE, MD_RESPONSE_OK, write_handle);
}


//Check if updating partition is properly erased
void checkFlashErased(uint32_t eraseSize){
  esp_err_t err;
  uint8_t flash_buffer[16] = {0};

  //We need raw flash reads
  upd_partition.encrypted = false;
  err = esp_partition_read(&upd_partition, 0, (void *) flash_buffer, 16);
  if (err != ESP_OK) {
    ESP_LOGI("OTA", "Failed to read flash");
  }else{
    //Erase entire partition if not empty
    if(flash_buffer[0] != 0xFF){
      ESP_LOGI("OTA", "Flash not empty, erasing");
      err = esp_partition_erase_range(update_partition, 0, eraseSize);
      if (err != ESP_OK) {
        ESP_LOGI("OTA", "Failed to erase partition");
      }else{
        ESP_LOGI("OTA", "Partition was erased");
      }
    }
  }
}

//Initialize the update system
void initOTA(){
  const esp_partition_t *configured = esp_ota_get_boot_partition();
  const esp_partition_t *running = esp_ota_get_running_partition();

  ESP_LOGI("OTA", "Configured: 0x%08x, Running: 0x%08x", configured->address, running->address);

  update_partition = esp_ota_get_next_update_partition(NULL);
  ESP_LOGI("OTA", "Update partition: 0x%08x, Size: 0x%08x, Enc:%d", update_partition->address, update_partition->size, update_partition->encrypted);

  //Set a table index, for client
  if(update_partition->address == 0x10000) partition_index = 0;
  else partition_index = 1;

  //Create copy of the update partition
  memcpy(&upd_partition, update_partition, sizeof(esp_partition_t));

  //Check if flash is clean, and erase if necessary
  checkFlashErased(update_partition->size);

  ESP_LOGI("OTA", "esp_ota_begin succeeded, firmware %x", MD_FIRMWARE);
}

void app_spp_send_string(char command, char* text, uint32_t handle){
  int text_len = 4 + strlen(text) + 2;

  memset(out_text, 0, 32);
  memcpy(out_text, MD_REPLY, 3);

  out_text[3] = command;

  memcpy(&out_text[4], text, strlen(text));

  //Add delimiter
  out_text[text_len - 2] = '\r';
  out_text[text_len - 1] = '\n';

  esp_spp_write(handle, text_len, out_text);
}

//Send array of bytes on the line
void app_spp_send_array(char command, uint8_t *array, int length, uint32_t handle){
  int data_len = 6 + length;

  memset(out_text, 0, 32);
  memcpy(out_text, MD_REPLY, 3);

  out_text[3] = command;

  //Send out entire buffer
  for(int i = 0; i < length; i++){
      out_text[4 + i] = array[i];
  }

  //Add delimiter
  out_text[data_len - 2] = '\r';
  out_text[data_len - 1] = '\n';

  esp_spp_write(handle, data_len, out_text);
}


//Send a single byte on the line
void app_spp_send_byte(char command, uint8_t byte, uint32_t handle){
  int text_len = 7;

  memset(out_text, 0, 32);
  memcpy(out_text, MD_REPLY, 3);

  out_text[3] = command;
  out_text[4] = byte;

  //Add delimiter
  out_text[5] = '\r';
  out_text[6] = '\n';

  esp_spp_write(handle, text_len, out_text);
}

//Receive data from the companion app
void app_spp_data_handle(uint8_t* data, uint8_t data_len, uint32_t handle){
  uint32_t dataPos = 0;
  uint32_t dataLen = 0;
  uint32_t recvChecksum = 0;
  uint32_t checksumValue = 0;
  esp_err_t err;
  uint8_t firmware_cmd[4] = {MD_FIRMWARE, MD_BUILD_SIGNATURE, MD_BUILD_NUMBER & 0xFF, MD_BUILD_NUMBER >> 8 };

  //Packet contains inadequate header, ignore data
  if(data_len < 4) return;

  //Fetch handle for data
  sppHandle = handle;

  //Check what command was received
  if(data[0] == 'X' && data[1] == 'M' && data[2] == 'D'){
    switch(data[3]){
      case MD_CMD_IDENTIFIER:
        app_spp_send_string(MD_CMD_IDENTIFIER, MD_IDENTIFIER, handle);
        break;

      case MD_CMD_MODEL:
        app_spp_send_string(MD_CMD_MODEL, MD_MODEL, handle);
        break;

      case MD_CMD_REVISION:
        app_spp_send_byte(MD_CMD_REVISION, MD_REVISION, handle);
        break;

      case MD_CMD_FIRMWARE:
        app_spp_send_array(MD_CMD_FIRMWARE, firmware_cmd, 4, handle);
        break;

      case MD_CMD_PROTOCOL:
        app_spp_send_byte(MD_CMD_PROTOCOL, MD_PROTOCOL, handle);
        break;

      case MD_CMD_PARTITION:
        app_spp_send_byte(MD_CMD_PARTITION, partition_index, handle);
        break;

      case MD_CMD_WRITE:   //XMDW - Write data to flash (update)
        dataPos = data[4] | data[5] << 8 | data[6] << 16 | data[7] << 24;
        dataLen = data[8] | data[9] << 8 | data[10] << 16 | data[11] << 24;
        recvChecksum = data[dataLen + 12] | data[dataLen + 13] << 8 | data[dataLen + 14] << 16 | data[dataLen + 15] << 24;

        //Calculate checksum for received packet, sans checksum
        checksumValue = crc32CalculateChecksum(data, 0, dataLen + 12);

        //Check if checksum was valid
        if(checksumValue == recvChecksum){

          if(dataPos == 0){
            validImageCheck(handle, &data[12], dataLen);
          }else{
            //Write data as received, without further encryption
            upd_partition.encrypted = false;
            err = esp_partition_write(&upd_partition, dataPos, (void *) &data[12], dataLen);
            if (err != ESP_OK) {
              app_spp_send_string(MD_CMD_WRITE, MD_RESPONSE_ERROR, handle);
            }else{
              app_spp_send_string(MD_CMD_WRITE, MD_RESPONSE_OK, handle);
            }
          }

        }else{
          app_spp_send_string(MD_CMD_WRITE, MD_WRITE_WRONG_CHECKSUM, handle);
        }

        break;

      case MD_CMD_VERIFY:   //Verify written image
        //Set new boot partition
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
          app_spp_send_string(MD_CMD_VERIFY, MD_RESPONSE_ERROR, handle);
          break;
        }

        //Everything went well, notify client
        reboot_unlock = true; //Allow reboots
        app_spp_send_string(MD_CMD_VERIFY, MD_RESPONSE_OK, handle);
        break;

      case MD_CMD_REBOOT:   //Warm reboot, keep active mode if currenty active
        if(reboot_unlock)esp_restart();
        else ESP_LOGI("OTA", "Reboot is not allowed");
        break;

      case MD_CMD_NOTIFICATION:
          //Send notification through the proper mechanisms
          ///pushNotification((char *) &data[6], data[5]);

          //Reply to the companion app that we got the notification
          app_spp_send_string(MD_CMD_NOTIFICATION, MD_RESPONSE_OK, handle);
        break;

      case MD_CMD_REGISTER_EVT:
        activeEvents = data[4];

        ESP_LOGI("SPP", "Registered SPP event with mask %x", activeEvents);

        //Reply on the successful event registration
        app_spp_send_string(MD_CMD_REGISTER_EVT, MD_RESPONSE_OK, handle);
        break;

      default:
        ESP_LOGI("SPP", "Unknown client command");
        break;
    }
  }else{
    ESP_LOGI("SPP", "Wrong command signature");
  }
}
