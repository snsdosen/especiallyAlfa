#ifndef _BT_APP_SPP_H_
#define _BT_APP_SPP_H_

  //Event type mask
  #define MD_EVENT_PLAYER_CMD 0x1

  void disableEvents();
  void sppSendEvent(uint8_t eventType, uint8_t eventCode);
  void validImageCheck(uint32_t write_handle, uint8_t *encrypted_cache, uint32_t dataLen);
  void app_spp_send_string(char command, char* text, uint32_t handle);
  void app_spp_send_byte(char command, uint8_t byte, uint32_t handle);
  void initOTA();
  void app_spp_data_handle(uint8_t* data, uint8_t data_len, uint32_t handle);

#endif
