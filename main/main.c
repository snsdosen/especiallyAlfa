//especiallyAlfa,
//ESP32 powered internal bluetooth module
//for Alfa 156/147/GT Blaupunkt radios.
//Siniša Došen 2026.

#include <stdio.h>
#include <string.h>

#include "buscom.h"
#include "textcom.h"
#include "bt_player.h"
#include "bt_app_spp.h"

void app_main(void)
{
    initOTA();
    InitBuscom();
    //InitTextcom();
    InitBluetooth();
}
