#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "buscom.h"
#include "esp_rom_sys.h" 
#include "esp_timer.h"

static TaskHandle_t buscom_rx_task_hdl = NULL;
static TaskHandle_t buscom_tick_task_hdl = NULL;

static bool enableTicker = false;           //Status report progress
static bool discInserted = true;            //Emulated media status
static bool insertingSequence = false;      //Media insertion is in progress
static bool inSeek = false;                 //Is song seek in progress
static bool allowTimePush = false;          //Is bt time sync allowed
static bool autoIncrementTime = true;       //Auto increment is not use when device is connected

static uint8_t currentMinute = 0;
static uint8_t currentSecond = 0;
static uint8_t currentTrack = 1;
static uint8_t activePState = PSTATE_PAUSE;

static bool KTKMhappened = false;
static bool radioInitializationDone = false;

QueueHandle_t uart_queue1;
static QueueHandle_t commandQueueHandle = NULL;

void RegisterCommandHandler(QueueHandle_t qHandle){
  commandQueueHandle = qHandle;
  ESP_LOGI(LOG_TAG_BUSCOM, "Got command handle from bluetooth");
}

//Send a command to command queues
void dispatchCMD(char playerCommand){
  if(commandQueueHandle) xQueueSend(commandQueueHandle, &playerCommand, 0);
}

void ResetTime(){
    currentMinute = 0;
    currentSecond = 0;
    radioInitializationDone = false;
}

//Accurate byte read function to reduce latency
static int uart_read_bytes_precise(uint8_t uart_num, uint8_t *buf, size_t len, uint32_t timeout_us){
    int64_t start = esp_timer_get_time();
    size_t total_read = 0;

    while(total_read < len){
        size_t avail = 0;
        uart_get_buffered_data_len(UART_NUM_1, &avail);

        if(avail > 0){
            int r = uart_read_bytes(UART_NUM_1, buf + total_read, len - total_read, 0);
            if(r > 0) total_read += r;
        }

        if(total_read >= len) break;
        if((esp_timer_get_time() - start) > timeout_us) break;
    }

    return total_read;
}

static void ComposeResponse(uint8_t *message, size_t len) {
    ESP_LOG_BUFFER_HEX(LOG_TAG_BUSCOM, message, len);
    esp_rom_delay_us(BUSCOM_RESPONSE_DELAY_US);
    uart_write_bytes(UART_NUM_1, (const char *)message, len);
    uart_wait_tx_done(UART_NUM_1, 20 / portTICK_PERIOD_MS);
}

//Send current time on the bus
void SendTime(){
    ComposeResponse((uint8_t[]){MEDIA_TIME_STATUS, currentSecond, currentMinute, 1, currentTrack, 1}, 6);
}

void restoreAutoUpdate(){
    autoIncrementTime = true;
    //if(activePState == PSTATE_PLAY) enableTicker = true;
}

//Send real track time from bluetooth side
void pushTime(uint32_t value){
    //Do not push for first 3 seconds
    //147 freaks out if the time is not default
    //from the boot
    if (!radioInitializationDone) {

        if (currentMinute > 0 || currentSecond >= 3) {
            radioInitializationDone = true; 
        } else {
            return;
        }
    }

    value /= 1000;
    currentMinute = value / 60;
    currentSecond = value % 60;
    autoIncrementTime = false;
}

/*void pushTime(uint32_t value){
    //Do not push messages on the bus if the module is not playing or in seek
    if(activePState == PSTATE_PAUSE || inSeek) return;

    //Disable auto update timer, we have a real deal now
    enableTicker = false;

    value /= 1000;

    //Clip maximum time to 99 minutes and 59 seconds
    if(value > 5999) value = 5999;

    currentMinute = value / 60;
    currentSecond = value % 60;

    SendTime();
}*/

//Waking up from standby
//Booting warm sends a couple of status messages
//before head unit even asks for anything
void WarmBoot(){

    esp_reset_reason_t reason = esp_reset_reason();

    //Check if ESP was soft reset, no need for warm boot if so
    if(reason == ESP_RST_SW){
        ESP_LOGW("buscom", "From update");
        return;
    }

    if(discInserted) ComposeResponse((uint8_t[]){0x72, 0x5, 0x70}, 3);
    else ComposeResponse((uint8_t[]){0x72, 0x00, 0x60}, 3);
    esp_rom_delay_us(3000);
    //vTaskDelay(80 / portTICK_PERIOD_MS);

    if(!discInserted){
        ComposeResponse((uint8_t[]){0x61, 0x01}, 2);
    }

    if(discInserted) ComposeResponse((uint8_t[]){0x72, 0x5, 0x72}, 3);
    else ComposeResponse((uint8_t[]){0x72, 0x00, 0x61}, 3);
}

//Simulate media insertion into emulated module
void CdInsertSequence(){
    //Insertion started
    ComposeResponse((uint8_t[]){0x72, 0x00, 0x6C}, 3);
    vTaskDelay(20 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x72, 0x00, 0x62}, 3);

    //Simulated disc is inserted
    discInserted = true;

    //Sequence started
    insertingSequence = true;
}

void CdInResponse() {
    ComposeResponse((uint8_t[]){0xE1, 0x70}, 2);

    if(discInserted){
        ComposeResponse((uint8_t[]){0x36, 0x2F, 0x63, 0x38, 0x63, 0x01, 0x00}, 7);
    }
}

void CdEjectResponse() {
    enableTicker = false;
    ResetTime();
    restoreAutoUpdate();
    ComposeResponse((uint8_t[]){CD_STATUS_RES, HU_HEADER_EJECT}, 2);

    if(discInserted){
        vTaskDelay(20 / portTICK_PERIOD_MS);
        ComposeResponse((uint8_t[]){0x72, 0x00, 0x62}, 3);
        vTaskDelay(20 / portTICK_PERIOD_MS);
        ComposeResponse((uint8_t[]){0x72, 0x00, 0x6C}, 3);
        vTaskDelay(20 / portTICK_PERIOD_MS);
        ComposeResponse((uint8_t[]){0x72, 0x00, 0x68}, 3);
        vTaskDelay(20 / portTICK_PERIOD_MS);
        ComposeResponse((uint8_t[]){0x72, 0x00, 0x61}, 3);
    }

    //Simulated disc is ejected
    discInserted = false;
    insertingSequence = false;

    vTaskDelay(50 / portTICK_PERIOD_MS);
    CdInsertSequence();
}

void ModuleActiveResponse(){
    ComposeResponse((uint8_t[]){0xE2, 0x02, 0x09}, 3);
    ComposeResponse((uint8_t[]){0x72, 0x05, 0x12}, 3);

    //Alfa 147
    //ComposeResponse((uint8_t[]){0x72, 0x00, 0x02}, 3);
    //ComposeResponse((uint8_t[]){0x73, 0x3D, 0x03, 0x2}, 4);
}

void ModuleInactiveResponse(){
    enableTicker = false;
    ComposeResponse((uint8_t[]){0xE2, 0x00, 0x09}, 3);
}

void ModuleStandbyResponse(){
    enableTicker = false;
    ComposeResponse((uint8_t[]){0xE2, 0x01, 0x09}, 3);
    if(discInserted) ComposeResponse((uint8_t[]){0x72, 0x05, 0xF2}, 3);
    else ComposeResponse((uint8_t[]){0x72, 0x00, 0xE1}, 3);
}

void PowerOnResponse(){
    ComposeResponse((uint8_t[]){0xE1, 0x60}, 2);
    if(discInserted) {
        if(activePState == PSTATE_PLAY) {
            ComposeResponse((uint8_t[]){0x72, 0x07, 0x32}, 3);
                enableTicker = true;
        }
        else ComposeResponse((uint8_t[]){0x72, 0x05, 0x72}, 3);
    }
    else ComposeResponse((uint8_t[]){0x72, 0x00, 0x61}, 3);
}

void ExModuleStatusResponse(){
    ComposeResponse((uint8_t[]){0xE2, 0x01, 0x51}, 3);
    ComposeResponse((uint8_t[]){0x42, 0x04, 0x12}, 3);

    //Alfa 147
    //ComposeResponse((uint8_t[]){0x42, 0x03, 0x06}, 3);
}

void CdReadyResponse(){
    ComposeResponse((uint8_t[]){0xE2, 0x03, 0x79}, 3);
    SendTime();
}

void KTKMResponse(){
    KTKMhappened = true;
    ComposeResponse((uint8_t[]){0xE1, 0x10}, 2);
    ComposeResponse((uint8_t[]){0x72, 0x05, 0x72}, 3);
}

//Send AVRCP forward or backward on track change
void sync_tracks_with_radio(int current_track, int target_track) {
    if (target_track < 1 || target_track > 99) return;

    //If it's the same track it's repeated
    if (target_track == current_track) {
        dispatchCMD(PLAYER_CMD_REP);
        return;
    }

    int forward_dist, backward_dist;

    //Forward steps
    if (target_track > current_track) {
        forward_dist = target_track - current_track;
    } else {
        forward_dist = (99 - current_track) + target_track;
    }

    //Backward steps
    if (target_track < current_track) {
        backward_dist = current_track - target_track;
    } else {
        backward_dist = current_track + (99 - target_track);
    }

    //Send commands
    if (forward_dist <= backward_dist) {
        ESP_LOGI("BT_PLAYER", "Forward %d steps", forward_dist);
        for (int i = 0; i < forward_dist; i++) {
            dispatchCMD(PLAYER_CMD_NEXT);
        }
    } else {
        ESP_LOGI("BT_PLAYER", "Backward %d steps", backward_dist);
        for (int i = 0; i < backward_dist; i++) {
            dispatchCMD(PLAYER_CMD_PREV);
        }
    }
}

void DestroyResponse(uint8_t cm0, uint8_t cm1, uint8_t cm2){
    ComposeResponse((uint8_t[]){0xE4, cm0, cm1, cm2, HU_HEADER_DESTROY}, 5);
}

void SeekToResponse(uint8_t trackNum){
    uint8_t cTrack = currentTrack;

    inSeek = true;
    allowTimePush = false;
    enableTicker = false;

    currentTrack = trackNum;

    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0xE3, 0x01, trackNum, 0x32}, 4);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x72, 0x07, 0x12}, 3);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x15, 0x00, 0x00, 0x00, trackNum, 0x00}, 6);
    sync_tracks_with_radio(cTrack, trackNum);
    vTaskDelay(200 / portTICK_PERIOD_MS);        //Fake seek delay
    ComposeResponse((uint8_t[]){0x72, 0x07, 0x32}, 3);
    vTaskDelay(10 / portTICK_PERIOD_MS);

    ResetTime();

    vTaskDelay(300 / portTICK_PERIOD_MS);

    inSeek = false;
    enableTicker = true;
}

//Acknowledge play status changes from the head unit
void PStateResponse(uint8_t cm0, uint8_t cm1, uint8_t state){
    ComposeResponse((uint8_t[]){CD_PSTATE_RES, cm0, cm1, state, 0x23}, 5);
    activePState = state;

    if(state == PSTATE_PAUSE) enableTicker = false;

    else if(state == PSTATE_TF){
        esp_rom_delay_us(1000);
        ComposeResponse((uint8_t[]){0x72, 0x07, 0x12}, 3);
    }

    else if(state == PSTATE_PLAY){
        esp_rom_delay_us(1000);
        ComposeResponse((uint8_t[]){0x72, 0x05, 0x32}, 3);
        if(KTKMhappened) enableTicker = true;
    }
}

//RX task for the buscom
static void buscom_rx_task(void *arg){
    uart_event_t event;
    uint8_t data[128];
    int length = 0;

    while(true){
        if(xQueueReceive(uart_queue1, (void *)&event, portMAX_DELAY)){

            switch(event.type){

                case UART_DATA:
                    length = uart_read_bytes(UART_NUM_1, data, 1, 0);

                    if(length <= 0) break;

                    ESP_LOGI(LOG_TAG_BUSCOM, "HU HEADER: %X", data[0]);

                    switch(data[0]){
                        default:        //Unknown command
                            printf("%.2X ", data[0]);

                            length = uart_read_bytes(UART_NUM_1, data, 20, pdMS_TO_TICKS(BUSCOM_PAYLOAD_TIMEOUT_MS));

                            for(int i = 0; i < length; i++){
                                printf("%.2X ", data[i]);
                            }

                            ESP_LOGI(LOG_TAG_BUSCOM, "<- Unknown MSG");
                            break;

                        case HU_MODULE_STATUS:
                            length = uart_read_bytes_precise(UART_NUM_1, data, HU_STATUS_LEN, BUSCOM_PAYLOAD_TIMEOUT_MS * 1000);
                            ExModuleStatusResponse();
                            break;

                        case HU_HEADER_KTKM:
                            KTKMResponse();
                            break;

                        case HU_POWER_ON:
                            PowerOnResponse();
                            break;

                        case HU_HEADER_CD_INSERTED:
                            CdInResponse();
                            break;

                        case HU_READY_REQUEST:
                            if(HU_READY_LEN == uart_read_bytes_precise(UART_NUM_1, data, HU_READY_LEN, BUSCOM_PAYLOAD_TIMEOUT_MS * 1000)){
                                CdReadyResponse();
                            }
                            break;

                        case HU_HEADER_EJECT:
                            dispatchCMD(PLAYER_CMD_EJECT);
                            CdEjectResponse();
                            break;

                        case HU_HEADER_ACTIVITY:
                            length = uart_read_bytes_precise(UART_NUM_1, data, HU_ACTIVITY_LEN, BUSCOM_PAYLOAD_TIMEOUT_MS * 1000);

                            if(length == HU_ACTIVITY_LEN){
                                switch(data[0]){
                                    case MODULE_ACTIVE:
                                        ModuleActiveResponse();
                                        dispatchCMD(PLAYER_CMD_ACTIVE);
                                        break;

                                    case MODULE_STANDBY:
                                        KTKMhappened = false;
                                        ModuleStandbyResponse();
                                        break;

                                    case MODULE_INACTIVE:
                                        ModuleInactiveResponse();
                                        dispatchCMD(PLAYER_CMD_PAUSE);
                                        //activePState = PSTATE_PAUSE;
                                        break;
                                }
                            }
                            break;

                        case HU_HEADER_PSTATE:
                            length = uart_read_bytes_precise(UART_NUM_1, data, HU_PSTATE_LEN, BUSCOM_PAYLOAD_TIMEOUT_MS * 1000);

                            if(length == HU_PSTATE_LEN){
                                switch(data[2]){
                                    case PSTATE_PLAY:
                                        dispatchCMD(PLAYER_CMD_PLAY);
                                        break;

                                    case PSTATE_PAUSE:
                                        dispatchCMD(PLAYER_CMD_PAUSE);
                                        break;

                                    case PSTATE_REW:
                                        dispatchCMD(PLAYER_CMD_REW);
                                        break;

                                    case PSTATE_FF:
                                        dispatchCMD(PLAYER_CMD_FF);
                                        break;
                                }

                                PStateResponse(data[0], data[1], data[2]);
                            }
                            break;

                        case HU_HEADER_DESTROY:
                            length = uart_read_bytes_precise(UART_NUM_1, data, HU_DESTROY_LEN, BUSCOM_PAYLOAD_TIMEOUT_MS * 1000);
                            
                            if(length == HU_DESTROY_LEN){
                                DestroyResponse(data[0], data[1], data[2]);
                            }
                            
                            break;

                        case HU_HEADER_SEEK:
                            length = uart_read_bytes_precise(UART_NUM_1, data, HU_SEEK_LEN, BUSCOM_PAYLOAD_TIMEOUT_MS * 1000);

                            if(length == HU_SEEK_LEN){
                                SeekToResponse(data[1]);
                            }
                            break;
                    }
                    break;

                case UART_FIFO_OVF:
                    ESP_LOGW(LOG_TAG_BUSCOM, "HW FIFO overflow");
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(uart_queue1);
                    break;

                case UART_BUFFER_FULL:
                    ESP_LOGW(LOG_TAG_BUSCOM, "Ring buffer full");
                    uart_flush_input(UART_NUM_1);
                    xQueueReset(uart_queue1);
                    break;

                default:
                    break;
            }
        }
    }
}

//Seconds timer for progress report
static void buscom_tick_task(void *arg){
    for(;;){
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        //if(!allowTimePush && currentSecond > 2) allowTimePush = true;

        if(enableTicker && activePState == PSTATE_PLAY && !inSeek){
            SendTime();

            if(autoIncrementTime){
                if(currentSecond < 59) currentSecond++;
                else{
                    currentSecond = 0;
                    if(currentMinute < 99) currentMinute++;
                    else currentMinute = 0;
                };
            }

            //Auto enable time increment for the next cycle
            //phone will disable it if need be
            //otherwise 147 will freak out
            autoIncrementTime = true;
        }
    }
}

static const char *TAG = "PWR_MON";

void power_on_routine() {
    ESP_LOGW(TAG, "Sending warm CD status");
    //WarmBoot();
}

//Monitor head unit power
void power_monitor_task(void *pvParameters) {
    bool power_is_active = false;
    int low_duration_ms = 0;
    const int check_interval_ms = 10;
    const int required_low_ms = 100;

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << POWER_MONITOR_PIN),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    gpio_config(&io_conf);

    ESP_LOGI(TAG, "Power monitor started on pin %d.", POWER_MONITOR_PIN);

    while (1) {
        int level = gpio_get_level(POWER_MONITOR_PIN);

        if (level == 1) {
            if (!power_is_active) {
                ESP_LOGI(TAG, "ACC Power on");
                power_on_routine();
                power_is_active = true;
            }
            low_duration_ms = 0;
        } 
        else {
            if (power_is_active) {
                low_duration_ms += check_interval_ms;

                if (low_duration_ms >= required_low_ms) {
                    ESP_LOGE(TAG, "ACC Power off", required_low_ms);
                    power_is_active = false;
                    low_duration_ms = 0;
                    enableTicker = false;

                    //Disconnect remote device
                    dispatchCMD(PLAYER_CMD_EJECT);

                    vTaskDelay(pdMS_TO_TICKS(300));

                    //Go to sleep
                    rtc_gpio_init(POWER_MONITOR_PIN);
                    rtc_gpio_set_direction(POWER_MONITOR_PIN, RTC_GPIO_MODE_INPUT_ONLY);
                    esp_sleep_enable_ext0_wakeup(POWER_MONITOR_PIN, 1);
                    esp_deep_sleep_start();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    }
}

//Set up UART for bus communication
void SetUpBuscomPort(){
    const uart_port_t uart_num1 = UART_NUM_1;
    uart_config_t uart_config1 = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };

    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, BUSCOM_TX_PIN, BUSCOM_RX_PIN, -1, -1));
    ESP_ERROR_CHECK(uart_param_config(uart_num1, &uart_config1));

    const int uart_buffer_size = (1024 * 2);
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, uart_buffer_size, uart_buffer_size, 20, &uart_queue1, 0));

    //Interrupt as soon as 1 byte arrives for fast response
    ESP_ERROR_CHECK(uart_set_rx_full_threshold(UART_NUM_1, 1));
    ESP_ERROR_CHECK(uart_set_rx_timeout(UART_NUM_1, 1));
}

//Start bus RX/TX communication module
void InitBuscom(void)
{
    //Configure serial port
    SetUpBuscomPort();

    WarmBoot();

    xTaskCreate(buscom_rx_task, "buscom_rx", 2048, NULL, 5, &buscom_rx_task_hdl);
    xTaskCreate(power_monitor_task, "pwr_monitor", 2048, NULL, 10, NULL);
    xTaskCreate(buscom_tick_task, "buscom_tick", 2048, NULL, 5, &buscom_tick_task_hdl);
    ESP_LOGI(LOG_TAG_BUSCOM, "Buscom initialized");
}
