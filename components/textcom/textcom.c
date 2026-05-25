#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "textcom.h"

static const char *TAG = "ALFA_HARDWARE_SPI";

#define GPIO_MOSI    22
#define GPIO_CLK     23
#define GPIO_FAKE_CS 21

#define BUFFER_SIZE  64
WORD_ALIGNED_ATTR uint8_t rx_buffer[BUFFER_SIZE];

void alfa_spi_hardware_task(void *pvParameters) {
    esp_err_t ret;

    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << GPIO_FAKE_CS),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&cs_cfg);
    gpio_set_level(GPIO_FAKE_CS, 0);

    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = GPIO_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BUFFER_SIZE
    };

    spi_slave_interface_config_t slvcfg = {
        .mode = 1,
        .spics_io_num = GPIO_FAKE_CS,
        .queue_size = 4,
        .flags = 0
    };

    ret = spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    spi_slave_transaction_t t;

    while (1) {
        memset(rx_buffer, 0, BUFFER_SIZE);
        memset(&t, 0, sizeof(t));

        t.length = BUFFER_SIZE * 8; 
        t.rx_buffer = rx_buffer;

        ret = spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);

        if (ret == ESP_OK) {
            printf("Alfa HARDWARE RX: ");
            for (int i = 0; i < 16; i++) {
                printf("0x%02X ", rx_buffer[i]);
            }
            printf("\n");
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void InitTextcom(void){
    xTaskCreatePinnedToCore(alfa_spi_hardware_task, "alfa_spi_hw_task", 4096, NULL, 15, NULL, 1);
}
