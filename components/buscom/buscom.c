#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "buscom.h"

//static TaskHandle_t buscom_task_hdl = NULL;
static TaskHandle_t buscom_rx_task_hdl = NULL;
static TaskHandle_t buscom_tx_task_hdl = NULL;
static TaskHandle_t buscom_tick_task_hdl = NULL;

static bool enableTicker = false;       //Status report progress
static bool discInserted = true;        //Emulated media status
static bool insertingSequence = false;  //Media insertion is in progress

static uint8_t currentMinute = 0;
static uint8_t currentSecond = 0;
static uint8_t currentTrack = 1;
static uint8_t activePState = PSTATE_PAUSE;

//Ringbuffer for data transmission
RingbufHandle_t dataRingbuf;

void ResetTime(){
    currentMinute = 0;
    currentSecond = 0;
}

static void ComposeResponse(uint8_t *message, size_t len) {
    ESP_LOG_BUFFER_HEX(LOG_TAG_BUSCOM, message, len);
    xRingbufferSend(dataRingbuf, message, len, pdMS_TO_TICKS(100));
}

//Send current time on the bus
void SendTime(){
    ComposeResponse((uint8_t[]){MEDIA_TIME_STATUS, currentSecond, currentMinute, 1, currentTrack, 1}, 6);
}

//Waking up from standby
//Booting warm sends a couple of status messages
//before head unit even asks for anything
void WarmBoot(){
    if(discInserted) ComposeResponse((uint8_t[]){0x72, 0x5, 0x70}, 3);
    else ComposeResponse((uint8_t[]){0x72, 0x00, 0x60}, 3);
    vTaskDelay(80 / portTICK_PERIOD_MS);

    if(!discInserted){
        ComposeResponse((uint8_t[]){0x61, 0x01}, 2);
        vTaskDelay(10 / portTICK_PERIOD_MS);
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
        vTaskDelay(10 / portTICK_PERIOD_MS);
        ComposeResponse((uint8_t[]){0x36, 0x2F, 0x0D, 0x38, 0x0D, 0x01, 0x00}, 7);
    }
}

void CdEjectResponse() {
    ComposeResponse((uint8_t[]){CD_STATUS_RES, HU_HEADER_EJECT}, 2);

    if(discInserted){
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
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x72, 0x05, 0x12}, 3);
}

void ModuleInactiveResponse(){
    enableTicker = false;
    ComposeResponse((uint8_t[]){0xE2, 0x00, 0x09}, 3);
}

void ModuleStandbyResponse(){
    enableTicker = false;
    ComposeResponse((uint8_t[]){0xE2, 0x01, 0x09}, 3);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    if(discInserted) ComposeResponse((uint8_t[]){0x72, 0x05, 0xF2}, 3);
    else ComposeResponse((uint8_t[]){0x72, 0x00, 0xE1}, 3);
}

void PowerOnResponse(){
    ComposeResponse((uint8_t[]){0xE1, 0x60}, 2);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    if(discInserted) ComposeResponse((uint8_t[]){0x72, 0x05, 0x72}, 3);
    else ComposeResponse((uint8_t[]){0x72, 0x00, 0x61}, 3);
}

void ExModuleStatusResponse(){
    ComposeResponse((uint8_t[]){0xE2, 0x01, 0x51}, 3);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x42, 0x04, 0x12}, 3);
}

void CdReadyResponse(){
    ComposeResponse((uint8_t[]){0xE2, 0x03, 0x79}, 3);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x15, 0x03, 0x00, 0x01, 0x01, 0x01}, 6);
    //ComposeResponse((uint8_t[]){0x15, 0x13, 0x00, 0x01, 0x03, 0x01}, 6);
}

void SeekToResponse(uint8_t trackNum){
    enableTicker = false;
    currentTrack = trackNum;
    ResetTime();
    ComposeResponse((uint8_t[]){0xE3, 0x01, trackNum, 0x32}, 4);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x72, 0x07, 0x12}, 3);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x15, 0x00, 0x00, 0x00, trackNum, 0x00}, 6);
    vTaskDelay(20 / portTICK_PERIOD_MS);        //Fake seek delay
    ComposeResponse((uint8_t[]){0x72, 0x07, 0x32}, 3);
    vTaskDelay(10 / portTICK_PERIOD_MS);
    ComposeResponse((uint8_t[]){0x15, 0x00, 0x00, 0x01, trackNum, 0x01}, 6);
    enableTicker = true;
}

//Acknowledge play status changes from the head unit
void PStateResponse(uint8_t state){
    ComposeResponse((uint8_t[]){CD_PSTATE_RES, 8, 0, state, 0x23}, 5);
    activePState = state;

    if(state == PSTATE_PAUSE) enableTicker = false;

    if(state == PSTATE_PLAY){
        ComposeResponse((uint8_t[]){0x72, 0x05, 0x32}, 3);
        vTaskDelay(20 / portTICK_PERIOD_MS);    //Fake seek time
        ComposeResponse((uint8_t[]){0x72, 0x07, 0x32}, 3);
        enableTicker = true;
    }
}

//RX task for the buscom
static void buscom_rx_task(void *arg){
    size_t inBuffer = 0;
    uint8_t data[128];
    int length = 0;

    //Do the deed
    while(true){

        //Data from Headunit
        uart_get_buffered_data_len(UART_NUM_1, &inBuffer);
        if(inBuffer > 0){

            //Read only header of the message
            length = uart_read_bytes(UART_NUM_1, data, 1, 3 / portTICK_PERIOD_MS);

            ESP_LOGI(LOG_TAG_BUSCOM, "HU HEADER: %X", data[0]);

            //Decode a message
            switch(data[0]){
                default:        //Unknown command
                    
                    //Print header+message on the screen for debugging
                    printf("%.2X ", data[0]);

                    length = uart_read_bytes(UART_NUM_1, data, 20, 3 / portTICK_PERIOD_MS);

                    for(int i = 0; i < length; i++){
                        printf("%.2X ", data[i]);
                    }

                    ESP_LOGI(LOG_TAG_BUSCOM, "<- Unknown MSG");
                    
                    //uart_flush(UART_NUM_2);     //Discard current input data since data is unknown
                    break;

                case HU_MODULE_STATUS:
                    length = uart_read_bytes(UART_NUM_1, data, 1, 3 / portTICK_PERIOD_MS);
                    ESP_LOGI(LOG_TAG_BUSCOM, "MODULE STATUS");
                    ExModuleStatusResponse();
                    break;

                case HU_POWER_ON:
                    ESP_LOGI(LOG_TAG_BUSCOM, "POWER ON");
                    PowerOnResponse();
                    break;

                case HU_HEADER_CD_INSERTED:
                    ESP_LOGI(LOG_TAG_BUSCOM, "IS CD IN?");
                    CdInResponse();
                    break;

                case HU_READY_REQUEST:
                    if(HU_READY_LEN == uart_read_bytes(UART_NUM_1, data, HU_ACTIVITY_LEN, 3 / portTICK_PERIOD_MS)){
                        ESP_LOGI(LOG_TAG_BUSCOM, "ARE YOU READY?");
                        CdReadyResponse();
                    }
                    break;

                case HU_HEADER_EJECT:
                    ESP_LOGI(LOG_TAG_BUSCOM, "EJECT");
                    CdEjectResponse();
                    break;

                case HU_HEADER_ACTIVITY:
                    length = uart_read_bytes(UART_NUM_1, data, HU_ACTIVITY_LEN, 3 / portTICK_PERIOD_MS);

                    if(length == HU_ACTIVITY_LEN){
                        switch(data[0]){
                            case MODULE_ACTIVE:
                                ESP_LOGI(LOG_TAG_BUSCOM, "MODULE ACTIVE");
                                break;

                            case MODULE_STANDBY:
                                ESP_LOGI(LOG_TAG_BUSCOM, "MODULE STANDBY");
                                ModuleStandbyResponse();
                                break;

                            case MODULE_INACTIVE:
                                ESP_LOGI(LOG_TAG_BUSCOM, "MODULE INACTIVE");
                                ModuleInactiveResponse();
                                break;
                            }
                    }
                    break;

                case HU_HEADER_PSTATE:
                    length = uart_read_bytes(UART_NUM_1, data, HU_PSTATE_LEN, 3 / portTICK_PERIOD_MS);

                    if(length == HU_PSTATE_LEN){
                        switch(data[2]){
                            case PSTATE_PLAY:
                                ESP_LOGI(LOG_TAG_BUSCOM, "PSTATE: PLAY");
                                break;

                            case PSTATE_PAUSE:
                                ESP_LOGI(LOG_TAG_BUSCOM, "PSTATE: PAUSE");
                                break;

                            case PSTATE_REW:
                                ESP_LOGI(LOG_TAG_BUSCOM, "PSTATE: REWIND");
                                break;

                            case PSTATE_FF:
                                ESP_LOGI(LOG_TAG_BUSCOM, "PSTATE: FAST FORWARD");
                                break;
                        }

                        //Send ack response to head unit
                        PStateResponse(data[2]);
                    }

                    break;

                case HU_HEADER_SEEK:
                    length = uart_read_bytes(UART_NUM_1, data, HU_SEEK_LEN, 3 / portTICK_PERIOD_MS);

                    if(length == HU_SEEK_LEN){
                        ESP_LOGI(LOG_TAG_BUSCOM, "SEEK TO: %d", data[1]);
                        SeekToResponse(data[1]);
                    }
                    break;
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

//Wait for output data from ringbuffer and send it to UART
static void buscom_tx_task(void *arg){
    while (1) {
            size_t item_size;
            uint8_t *item = (uint8_t *)xRingbufferReceive(dataRingbuf, &item_size, portMAX_DELAY);
            
            if (item != NULL) {
                uart_write_bytes(UART_NUM_1, (const char *)item, item_size);
                vRingbufferReturnItem(dataRingbuf, (void *)item);
            }
        }
}

//Logging task for the bus debugging
static void buscom_logger_task(void *arg){

    //Configure UART 1 for RX
    const uart_port_t uart_num1 = UART_NUM_2;
    uart_config_t uart_config1 = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };

    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, -1, BUSCOM_RX_SNIFF, -1, -1));

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num1, &uart_config1));

    // Setup UART buffered IO with event queue
    const int uart_buffer_size = (1024 * 2);
    QueueHandle_t uart_queue1;

    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_2, uart_buffer_size, uart_buffer_size, 10, &uart_queue1, 0));

    size_t inBuffer1 = 0;
    uint8_t data[128];
    int length = 0;

    //Do the task
    for(;;){

        //Data from CD drive
        uart_get_buffered_data_len(UART_NUM_2, &inBuffer1);
        if(inBuffer1 > 0){
            length = uart_read_bytes(UART_NUM_2, data, 20, 3 / portTICK_PERIOD_MS);

            for(int i = 0; i < length; i++){
                printf("%.2X ", data[i]);
            }

            ESP_LOGE(LOG_TAG_BUSCOM, "<- CD RESPONSE");
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

//Seconds timer for progress report
static void buscom_tick_task(void *arg){
    for(;;){
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        if(enableTicker){
            SendTime();
            if(currentSecond < 59) currentSecond++;
            else{
                currentSecond = 0;
                if(currentMinute < 59) currentMinute++;
                else currentMinute = 0;
            };
        }
    }
}

static const char *TAG = "PWR_MON";

void power_on_routine() {
    ESP_LOGW(TAG, "Sending warm CD status");
    WarmBoot();
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
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
    }
}

//Set up UART for bus communication
void SetUpBuscomPort(){
    //Configure UART 1 for TX/RX
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

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(uart_num1, &uart_config1));

    // Setup UART buffered IO with event queue
    const int uart_buffer_size = (1024 * 2);
    QueueHandle_t uart_queue1;

    // Install UART driver using an event queue here
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, uart_buffer_size, uart_buffer_size, 10, &uart_queue1, 0));
}

//Start bus RX/TX communication module
void InitBuscom(void)
{
    //Ringbuffer for output data transfer
    dataRingbuf = xRingbufferCreate(256, RINGBUF_TYPE_NOSPLIT);

    if (dataRingbuf == NULL) {
        printf("Can't create data ringbuffer\n");
        return;
    }

    //Configure serial port
    SetUpBuscomPort();

    //xTaskCreate(buscom_logger_task, "buscom", 2048, NULL, 5, &buscom_task_hdl);
    xTaskCreate(buscom_rx_task, "buscom_rx", 2048, NULL, 5, &buscom_rx_task_hdl);
    xTaskCreate(buscom_tx_task, "buscom_tx", 2048, NULL, 5, &buscom_tx_task_hdl);
    xTaskCreate(power_monitor_task, "pwr_monitor", 2048, NULL, 10, NULL);
    xTaskCreate(buscom_tick_task, "buscom_tick", 2048, NULL, 5, &buscom_tick_task_hdl);
    ESP_LOGI(LOG_TAG_BUSCOM, "Buscom initialized");
}
