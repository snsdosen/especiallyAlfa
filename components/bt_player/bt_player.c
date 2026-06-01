#include "bt_player.h"

//FF/REW activity flags
static bool ffActive = false;
static bool rewActive = true;

static QueueHandle_t commandQueue = NULL;

static i2s_chan_handle_t my_tx_chan = NULL;

//Address of the connected A2DP source
static esp_bd_addr_t connected_bda;

//Debounce ms counter for eject/connect event
static int64_t lastEject = 0;

static RingbufHandle_t s_a2d_ringbuf = NULL;

//Current source play state
int sourcePlayState = 0x2;
bool supportsAutoUpdate = false;

/* device name */
static const char local_device_name[] = CONFIG_EXAMPLE_LOCAL_DEVICE_NAME;

/* event for stack up */
enum {
    BT_APP_EVT_STACK_UP = 0,
};

/********************************
 * STATIC FUNCTION DECLARATIONS
 *******************************/

/* Device callback function */
static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param);

/* GAP callback function */
static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param);

/* callback function for A2DP sink */
static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param);

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len);

/* handler for AVRCP controller events */
static void bt_app_avrc_ct_evt_hdl(uint16_t event, void *param);

/* callback function for AVRCP controller */
static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param);

/* callback function for AVRCP target */
static void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param);

/* handler for bluetooth stack enabled events */
static void bt_av_hdl_stack_evt(uint16_t event, void *p_param);

static void bt_app_dev_cb(esp_bt_dev_cb_event_t event, esp_bt_dev_cb_param_t *param)
{
    bredr_app_dev_evt_def_hdl(event, param);
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    bredr_app_gap_evt_def_hdl(event, param);
}

static void bt_app_a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_PROF_STATE_EVT:
    case ESP_A2D_SNK_PSC_CFG_EVT:
    case ESP_A2D_SNK_SET_DELAY_VALUE_EVT:
    case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
        bt_app_work_dispatch(bt_a2d_evt_def_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;
    }
    case ESP_A2D_CONNECTION_STATE_EVT:
    esp_a2d_cb_param_t *a2d = (esp_a2d_cb_param_t *)(param);
        if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            //Copy address to localy value
            memcpy(connected_bda, a2d->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
            
            ESP_LOGI(BT_AV_TAG, "Device connected: %02x:%02x:%02x:%02x:%02x:%02x",
                    connected_bda[0], connected_bda[1], connected_bda[2],
                    connected_bda[3], connected_bda[4], connected_bda[5]);

            esp_bd_addr_t savedAddress;

            //Save device to storage if new
            if (loadDeviceAddr(savedAddress) != ESP_OK || 
                    memcmp(savedAddress, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN) != 0) {
                    
                    saveDeviceAddr(param->conn_stat.remote_bda);
                    ESP_LOGI("BT_APP", "Device saved to NVS");
                } else {
                    ESP_LOGI("BT_APP", "Device already known");
                }

        }else if (a2d->conn_stat.state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
                //Remove active connected device address
                memset(connected_bda, 0, ESP_BD_ADDR_LEN);
        }

        bt_app_work_dispatch(bt_a2d_evt_int_codec_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;

    case ESP_A2D_AUDIO_STATE_EVT:
    case ESP_A2D_AUDIO_CFG_EVT:
    case ESP_A2D_SEP_REG_STATE_EVT: {
        bt_app_work_dispatch(bt_a2d_evt_int_codec_hdl, event, param, sizeof(esp_a2d_cb_param_t), NULL);
        break;
    }
    default:
        ESP_LOGE(BT_AV_TAG, "Invalid A2DP event: %d", event);
        break;
    }
}

static void bt_app_a2d_data_cb(const uint8_t *data, uint32_t len)
{
    if (s_a2d_ringbuf == NULL) {
        return;
    }

    xRingbufferSend(s_a2d_ringbuf, (void *)data, len, 0);
}

//Request audio metadata from remote device
void requestMetadata(){
    esp_avrc_ct_send_metadata_cmd(bt_avrc_common_alloc_tl(), ESP_AVRC_MD_ATTR_TITLE);
    esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_PLAY_POS_CHANGED, 0);
}

void bt_avrc_md_ct_evt_hdl_int(uint16_t event, void *param)
{
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);

    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
            uint8_t *bda = rc->conn_stat.remote_bda;
            ESP_LOGI(BT_RC_CT_TAG, "AVRC conn_state event: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                    rc->conn_stat.connected, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            break;
        }

        case ESP_AVRC_CT_METADATA_RSP_EVT: {
            if (rc->meta_rsp.attr_text) {
                ESP_LOGI(BT_RC_CT_TAG, "AVRC metadata rsp: attribute id 0x%x, %s", rc->meta_rsp.attr_id, rc->meta_rsp.attr_text);
            } else {
                ESP_LOGE(BT_RC_CT_TAG, "AVRC metadata rsp: attr_text NULL");
            }
            break;
        }

        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
            if (rc->change_ntf.event_id == ESP_AVRC_RN_TRACK_CHANGE) {
                requestMetadata();
            }else if(rc->change_ntf.event_id == ESP_AVRC_RN_PLAY_POS_CHANGED){
                //ESP_LOGI(BT_RC_CT_TAG, "poschange");
            }else if(rc->change_ntf.event_id == ESP_AVRC_RN_PLAY_STATUS_CHANGE){
                sourcePlayState = rc->change_ntf.event_parameter.playback;
                esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
            }
            break;
        }

        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
                requestMetadata();
            break;
        }
    }
}

static void bt_app_avrc_ct_evt_hdl(uint16_t event, void *param)
{
    ESP_LOGD(BT_RC_CT_TAG, "%s event: %d", __func__, event);

    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);

    switch (event) {
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_PROF_STATE_EVT: {
        bt_avrc_common_ct_evt_def_hdl(event, param);
        break;
    }
    case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
        bt_avrc_md_ct_evt_hdl_int(event, param);
        if (rc->conn_stat.connected) {
            /* get remote supported event_ids of peer AVRCP Target */
            bt_avrc_common_ct_get_peer_rn_cap();
        } else {
            supportsAutoUpdate = false;
            sourcePlayState = 0x2;
            bt_avrc_common_ct_set_peer_rn_cap(0);
            restoreAutoUpdate();
        }
        break;
    }
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
        bt_avrc_md_ct_evt_hdl_int(event, param);

        if(rc->change_ntf.event_id == ESP_AVRC_RN_PLAY_POS_CHANGED){

            pushTime(rc->change_ntf.event_parameter.play_pos);
            bt_avrc_common_ct_rn_play_pos_changed();

            break;
        }

        bt_avrc_common_ct_notify_evt_handler(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
        break;
    }
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
        /* set peer notification capability record */
        bt_avrc_common_ct_set_peer_rn_cap(rc->get_rn_caps_rsp.evt_set.bits);
        bt_avrc_common_ct_rn_track_changed();
        bt_avrc_common_ct_rn_play_status_changed();
        bt_avrc_common_ct_rn_play_pos_changed();

        bt_avrc_md_ct_evt_hdl_int(event, param);
        
        //Does source support auto update of the song position (Android) or we have to poll manually (iPhone)
        supportsAutoUpdate = (1 << ESP_AVRC_RN_PLAY_POS_CHANGED) & rc->get_rn_caps_rsp.evt_set.bits;
        
        //Enable play state report
        esp_avrc_ct_send_register_notification_cmd(1, ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);

        break;
    }
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        bt_avrc_md_ct_evt_hdl_int(event, param);
        break;
    }
    default:
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT:
        pushTime(param->play_status_rsp.song_position);
        break;
    case ESP_AVRC_CT_METADATA_RSP_EVT: {
        bt_app_work_dispatch(bt_app_avrc_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t), bt_avrc_common_copy_metadata);
        break;
    }
    case ESP_AVRC_CT_CONNECTION_STATE_EVT:
    case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT:
    case ESP_AVRC_CT_CHANGE_NOTIFY_EVT:
    case ESP_AVRC_CT_REMOTE_FEATURES_EVT:
    case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT:
    case ESP_AVRC_CT_PROF_STATE_EVT:
        bt_app_work_dispatch(bt_app_avrc_ct_evt_hdl, event, param, sizeof(esp_avrc_ct_cb_param_t), NULL);
        break;
    default:
        ESP_LOGE(BT_RC_CT_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

static void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    switch (event) {
    case ESP_AVRC_TG_CONNECTION_STATE_EVT:
    case ESP_AVRC_TG_REMOTE_FEATURES_EVT:
    case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT:
    case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT:
    case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT:
    case ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT:
    case ESP_AVRC_TG_PROF_STATE_EVT:
        bt_app_work_dispatch(bt_avrc_common_tg_evt_def_hdl, event, param, sizeof(esp_avrc_tg_cb_param_t), NULL);
        break;
    default:
        ESP_LOGE(BT_RC_TG_TAG, "Invalid AVRC event: %d", event);
        break;
    }
}

static void bt_av_hdl_stack_evt(uint16_t event, void *p_param)
{
    ESP_LOGD(BT_AV_TAG, "%s event: %d", __func__, event);

    switch (event) {
    /* when do the stack up, this event comes */
    case BT_APP_EVT_STACK_UP: {
        esp_bt_gap_set_device_name(local_device_name);
        esp_bt_dev_register_callback(bt_app_dev_cb);
        esp_bt_gap_register_callback(bt_app_gap_cb);

        esp_avrc_ct_register_callback(bt_app_rc_ct_cb);
        assert(esp_avrc_ct_init() == ESP_OK);
        esp_avrc_tg_register_callback(bt_app_rc_tg_cb);
        assert(esp_avrc_tg_init() == ESP_OK);

        esp_a2d_register_callback(&bt_app_a2d_cb);
        assert(esp_a2d_sink_init() == ESP_OK);

        esp_a2d_sink_register_data_callback(bt_app_a2d_data_cb);

        /* Get the default value of the delay value */
        esp_a2d_sink_get_delay_value();
        /* Get local device name */
        esp_bt_gap_get_device_name();

        /* set discoverable and connectable mode, wait to be connected */
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        break;
    }
    /* others */
    default:
        ESP_LOGE(BT_AV_TAG, "%s unhandled event: %d", __func__, event);
        break;
    }
}

//Internal codec uses I2S but in wrong mode for our purposes 
//so we will use another available I2S channel and direct data to it
void init_i2s1_for_32bit_dac() {
    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_1,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 12,
        .dma_frame_num = 480,
        .auto_clear = true,
    };

    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &my_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(44100),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PLAYER_BCLK_PIN,
            .ws   = PLAYER_LRCK_PIN,
            .dout = PLAYER_DATA_PIN,
            .din  = I2S_GPIO_UNUSED,
        },
    };

    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.ws_width = I2S_SLOT_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.bit_shift = false;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(my_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(my_tx_chan));
}

//Save device address to NVS
void saveDeviceAddr(esp_bd_addr_t bda) {
    nvs_handle_t my_handle;
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_blob(my_handle, "last_bda", bda, ESP_BD_ADDR_LEN);
        nvs_commit(my_handle);
        nvs_close(my_handle);
    }
}

//Load device from NVS storage
esp_err_t loadDeviceAddr(esp_bd_addr_t bda) {
    nvs_handle_t my_handle;
    size_t size = ESP_BD_ADDR_LEN;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        err = nvs_get_blob(my_handle, "last_bda", bda, &size);
        nvs_close(my_handle);
    }
    return err;
}

//Return status of the connected device
bool isRemoteConnected(){
    esp_bd_addr_t empty_bda = {0};
    
    return (memcmp(connected_bda, empty_bda, ESP_BD_ADDR_LEN) != 0);
}

//Disconnect connected source device
void disconnectSource() {
    if(isRemoteConnected()) {
        esp_a2d_sink_disconnect(connected_bda);
        lastEject = esp_timer_get_time();
    }
}

void send_avrc_cmd(uint8_t cmd_id) {
    if(!isRemoteConnected()) return;

    //Button pressed
    esp_avrc_ct_send_passthrough_cmd(0, cmd_id, ESP_AVRC_PT_CMD_STATE_PRESSED);

    //Button released
    esp_avrc_ct_send_passthrough_cmd(0, cmd_id, ESP_AVRC_PT_CMD_STATE_RELEASED);
}

void startFF(){
    if(!isRemoteConnected()) return;
    ffActive = true;
    esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_FAST_FORWARD, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

void startREW(){
    if(!isRemoteConnected()) return;
    rewActive = true;
    esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_REWIND, ESP_AVRC_PT_CMD_STATE_PRESSED);
}

static void command_task(void *pvParameters){
  char message;

  for(;;){
        if(xQueueReceive(commandQueue, &message, portMAX_DELAY)){
            switch(message){
                case PLAYER_CMD_ACTIVE:
                    if(!isRemoteConnected()){

                        esp_bd_addr_t savedAddress;
                        int64_t currentTime = esp_timer_get_time();

                        if (loadDeviceAddr(savedAddress) == ESP_OK && !(currentTime - lastEject < COOLDOWN_TIME_US)) esp_a2d_sink_connect(savedAddress);
                    }
                    break;

                case PLAYER_CMD_PLAY:
                    //Depress ff/rew buttons
                    if(ffActive){
                        ffActive = false;
                        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_FAST_FORWARD, ESP_AVRC_PT_CMD_STATE_RELEASED);
                    }

                    if(rewActive){
                        ffActive = false;
                        esp_avrc_ct_send_passthrough_cmd(0, ESP_AVRC_PT_CMD_REWIND, ESP_AVRC_PT_CMD_STATE_RELEASED);
                    }

                    send_avrc_cmd(ESP_AVRC_PT_CMD_PLAY);
                    break;

                case PLAYER_CMD_PAUSE:
                    send_avrc_cmd(ESP_AVRC_PT_CMD_PAUSE);
                    break;

                case PLAYER_CMD_REP:
                    send_avrc_cmd(ESP_AVRC_PT_CMD_BACKWARD);
                    break;

                case PLAYER_CMD_EJECT:
                    disconnectSource();
                    break;

                case PLAYER_CMD_PREV:
                    send_avrc_cmd(ESP_AVRC_PT_CMD_BACKWARD);
                    break;

                case PLAYER_CMD_NEXT:
                    send_avrc_cmd(ESP_AVRC_PT_CMD_FORWARD);
                    break;

                case PLAYER_CMD_FF:
                    startFF();
                    break;

                case PLAYER_CMD_REW:
                    startREW();
                    break;
            }
        }
  }
}

//Output PCM data from the ringbuffer to the I2S port
static void i2s_audio_tx_task(void *pvParameters)
{
    size_t item_size;
    size_t bytes_written;
    
    uint32_t *local_buffer32 = malloc(4096 * sizeof(uint32_t));
    if (local_buffer32 == NULL) {
        ESP_LOGE("I2S_TASK", "Not enough memory for buffer.");
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        uint8_t *data = (uint8_t *)xRingbufferReceive(s_a2d_ringbuf, &item_size, pdMS_TO_TICKS(50));

        if (data != NULL) {
            if (my_tx_chan != NULL) {
                int16_t *samples16 = (int16_t *)data;
                uint32_t num_samples = item_size / 2;

                if (num_samples > 4096) {
                    num_samples = 4096;
                }

                for (int i = 0; i < num_samples; i++) {
                    uint32_t sample32 = (uint32_t)((uint16_t)samples16[i]);
                    local_buffer32[i] = ~sample32; 
                }

                i2s_channel_write(my_tx_chan, local_buffer32, num_samples * 4, &bytes_written, portMAX_DELAY);
            }

            vRingbufferReturnItem(s_a2d_ringbuf, (void *)data);
        } else {

        }
    }

    free(local_buffer32);
    vTaskDelete(NULL);
}

//Seconds timer for track position pull request
static void bt_player_tick_task(void *arg){
    for(;;){

        //Poll position only for playing and non auto reporting devices
        if(isRemoteConnected() && sourcePlayState == 0x01 && !supportsAutoUpdate){
            esp_avrc_ct_send_get_play_status_cmd(5);
        }

        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

//Auto connect saved device
void autoConnectLastDevice(){
    esp_bd_addr_t savedAddress;
    if (loadDeviceAddr(savedAddress) == ESP_OK) esp_a2d_sink_connect(savedAddress);
}

void InitBTPlayer(void)
{
    //Disable Wi-Fi to lower power comsumption
    esp_wifi_stop();
    esp_wifi_deinit();

    s_a2d_ringbuf = xRingbufferCreate(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (s_a2d_ringbuf == NULL) {
        ESP_LOGE("APP", "Failed to init ringbuffer.");
        return;
    }

    commandQueue = xQueueCreate(100, sizeof(char));
    if(!commandQueue){
        ESP_LOGE(LOG_TAG_A2DP, "Failed to create command queue");
        return;
    }

    RegisterCommandHandler(commandQueue);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(command_task, "cmd_handler_task", 2048, NULL, 10, NULL);
    ESP_LOGI(LOG_TAG_A2DP, "Command task registered");

    ESP_ERROR_CHECK(bredr_app_common_init());

    bt_app_task_start_up();
    init_i2s1_for_32bit_dac();

    xTaskCreate(i2s_audio_tx_task, "i2s_tx_task", 2048, NULL, configMAX_PRIORITIES - 3, NULL);
    ESP_LOGI(LOG_TAG_A2DP, "I2S task registered");

    bt_app_work_dispatch(bt_av_hdl_stack_evt, BT_APP_EVT_STACK_UP, NULL, 0, NULL);

    //Set lower TX/RX power
    esp_err_t err = esp_bredr_tx_power_set(ESP_PWR_LVL_N12, ESP_PWR_LVL_N3);
    if (err == ESP_OK) {
        ESP_LOGI("BT_POWER", "Lowered BT power.");
    } else {
        ESP_LOGE("BT_POWER", "Error: %d", err);
    }

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    
    //Auto connect to last used device
    autoConnectLastDevice();

    //Start position polling task
    xTaskCreate(bt_player_tick_task, "bt_player_tick", 2048, NULL, 5, NULL);
}