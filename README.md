# especiallyAlfa
### ESP32 powered internal CD module emulator for Alfa's Blaupunkt head units 

<img width="800" height="600" alt="IMG_0318" src="https://github.com/user-attachments/assets/8d140577-d5e1-46b0-ad30-b797b4ef76bb" />
<br><br>

* Add bluetooth capabilities to your Alfa 147, 156 FL and GT head unit using "off the shelf" ESP32D dev module
* Internal mod, replaces and emulates CD assembly with bluetooth capable ESP32.
* Audio transfer is purely digital via I2S bus using head unit's own DAC.

<img width="800" height="452" alt="esp32 inside" src="https://github.com/user-attachments/assets/1d9666b2-b982-4ca9-bdd0-af2082f313c7" />

## Schematic:
<details>
  <summary>Alfa 156 FL</summary>
<img width="1685" height="820" alt="Alfa 156 schematic" src="https://github.com/user-attachments/assets/92164ef4-8dca-4692-8c34-a732d63e47c6" />
</details>
<details>
  <summary>Alfa 147/GT</summary>
<img width="1685" height="920" alt="Alfa 147 schematic" src="https://github.com/user-attachments/assets/7a1c2bef-ece8-456b-8981-bbdfef3dceea" />
</details>

## Notes:
* Head unit uses 5V TTL logic while ESP32 is a 3.3V device
so level shifter for the UART communication is required.
<br>5V for the shifter is provided on the connector (VUC) and is
available all the time regardless if the ACC is on.

* DAC on the other hand uses 3.3V TTL logic so no shifters are needed
on the fast audio transport lines which greatly simplifies implementation.

* VCC 9V is switched off in standby but stays hot after ACC is off
for a while, enough to put original CD module in standby.

* CPU is downclocked to 80 Mhz (from default 160) to lower power consumption
so we can use 5V provided by the head unit's voltage regulator (on 156 FL).

## What is implemented:
* CD emulator on the data bus, head unit unlocks and uses CD mode
* Bluetooth pairing and audio streaming
* Prev/Next/FF/REW/Play/Pause/etc commands.
* Enter deep sleep on standby to conserve car battery.
* Remember last connected device and auto connect on startup
* Display time sync with phone playing the music.
* Bluetooth firmware update via Android companion app [MDCompanion](https://github.com/snsdosen/MDCompanion)

## Things yet to be implemented:
* Title metadata support. ESP32 receives it and outputs it to a debug console at the moment.

## Requirements:
* Dev board with ESP-WROOM-32 module.
* 2 way level shifter for TX/RX UART lines.
* Dedicated 9V to 5V voltage regulator on Alfa 147, GT.

## How to flash:
* Download full_version-X.X from releases and flash it to 0x0 using [ESP Tool](https://espressif.github.io/esptool-js/)

## How to update:
1. Flash the latest full_version-X.X via USB
2. OTA (Over-The-Air) update via Bluetooth using [MDCompanion](https://github.com/snsdosen/MDCompanion)

Connections:

| Head unit | ESP pin      |
|:--------- | :----------- |
| VCC +9V   | Via resistor voltage divider to GPIO 27 (on 156 FL), Dedicated PSU (on 147/GT)|
| VUC +5V   | 5V for level shifter, Module power (on 156 FL)|
| LRCK      | GPIO 4          |
| DATA      | GPIO 18          |
| BCK       | GPIO 19           |
| TXD       | Via level shifter to GPIO 17 |
| RXD       | Via level shifter to GPIO 16 |

* On 156FL we need to monitor 9V VCC line to react to it's
  state so a resistor voltage divider<br>is needed to drop the
  voltage to ESP's required 3.3V level.<br>I recommend 10K+20K resistor voltage divider
