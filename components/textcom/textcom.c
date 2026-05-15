#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "textcom.h"

#define GPIO_SCLK 22  // D2 (Crvena linija)
#define GPIO_MOSI 04  // D1 (Narančasta linija)
#define GPIO_CS_FAKE 15 // Pin koji ćemo softverski kontrolirati kao CS

#define BUFFER_SIZE 128
WORD_ALIGNED_ATTR uint8_t rx_buffer[BUFFER_SIZE];

volatile bool bus_ready = false;

// Interrupt koji čeka prvi takt clocka nakon pauze
static void IRAM_ATTR sclk_isr_handler(void* arg) {
    // Čim detektiramo prvi impuls, "spuštamo" CS na nulu da SPI Slave krene
    gpio_set_level(GPIO_CS_FAKE, 0); 
    bus_ready = true;
}

void spi_task(void *pv) {
    // 1. Konfiguracija FAKE CS pina
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << GPIO_CS_FAKE),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
    };
    gpio_config(&cs_cfg);
    gpio_set_level(GPIO_CS_FAKE, 1); // Počni u High (SPI Slave spava)

    // 2. Konfiguracija SPI Slave
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .sclk_io_num = GPIO_SCLK,
        .miso_io_num = -1,
    };
    spi_slave_interface_config_t slvcfg = {
        .mode = 1, // Mode 0 (Clock idle LOW)
        .spics_io_num = GPIO_CS_FAKE,
        .queue_size = 1,
    };
    spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_CH_AUTO);

    // 3. Konfiguracija Interrupta na SCLK za detekciju početka
    gpio_install_isr_service(0);
    gpio_set_intr_type(GPIO_SCLK, GPIO_INTR_POSEDGE);
    gpio_isr_handler_add(GPIO_SCLK, sclk_isr_handler, NULL);

    while(1) {
        if (bus_ready) {
            spi_slave_transaction_t t = {
                .length = BUFFER_SIZE * 8,
                .rx_buffer = rx_buffer,
            };

            if (spi_slave_transmit(SPI2_HOST, &t, pdMS_TO_TICKS(50)) == ESP_OK) {
                // Ispis onoga što smo ulovili
                if (rx_buffer[0] != 0) {
                    printf("Ulovljeno: ");
                    for(int i=0; i<32; i++) printf("%02X ", rx_buffer[i]);
                    printf("\n");
                }
            }

            // Resetiraj za idući paket
            gpio_set_level(GPIO_CS_FAKE, 1);
            bus_ready = false;
            vTaskDelay(pdMS_TO_TICKS(20)); // Čekaj da prođe ostatak paketa i nastupi Idle
        }
        vTaskDelay(1);
    }
}

void InitTextcom(void){
    xTaskCreate(spi_task, "clk_mon", 4096, NULL, 5, NULL);
}