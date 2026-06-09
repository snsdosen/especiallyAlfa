#ifndef _BT_PLAYER_H_
#define _BT_PLAYER_H_

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "bt_app_core_utils.h"
#include "bredr_app_common_utils.h"
#include "a2dp_sink_common_utils.h"
#include "a2dp_utils_tags.h"
#include "a2dp_sink_int_codec_utils.h"
#include "avrcp_utils_tags.h"
#include "avrcp_common_utils.h"
#include "avrcp_metadata_utils.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "esp_bt.h"
#include "esp_wifi.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "buscom.h"
#include "bt_spp.h"

#define COOLDOWN_TIME_US (10000000LL)

#define LOG_TAG_A2DP  "A2DP/AVRCP"

#define PLAYER_DATA_PIN GPIO_NUM_18
#define PLAYER_LRCK_PIN GPIO_NUM_4
#define PLAYER_BCLK_PIN GPIO_NUM_19

#define RINGBUF_SIZE (32 * 1024)

void InitBluetooth(void);

void saveDeviceAddr(esp_bd_addr_t bda);
esp_err_t loadDeviceAddr(esp_bd_addr_t bda);

#endif