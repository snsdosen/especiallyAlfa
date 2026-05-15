# especiallyAlfa
### ESP32 powered internal CD module emulator for Alfa's Blaupunkt head units 

<img width="800" height="600" alt="IMG_0318" src="https://github.com/user-attachments/assets/8d140577-d5e1-46b0-ad30-b797b4ef76bb" />
<br><br>

* Add bluetooth capabilities to your Alfa 156 FL (possibly GT and 147 too) radio using only ESP32 dev module
* Internal mod, replaces and emulates CD assembly with bluetooth capable ESP32.
* Audio transfer is purely digital via I2S bus using head unit's own DAC.

<img width="800" height="452" alt="esp32 inside" src="https://github.com/user-attachments/assets/1d9666b2-b982-4ca9-bdd0-af2082f313c7" />

## Schematic:
<img width="1685" height="825" alt="especiallyAlfa diag" src="https://github.com/user-attachments/assets/e5f4328f-8df9-4337-a1a2-37531df65a8c" />
<br><br>

## Notes:
* This is beta software, it still needs polishing to
iron out the quirks and make the experience seamless for the user.

* Head unit uses 5V TTL logic while ESP32 is a 3.3V device
so level shifter for the UART communication is required.
<br>5V for the shifter is provided on the connector (VUC) and is
available all the time regardless if the ACC is on.

* DAC on the other hand uses 3.3V TTL logic so no shifters are needed
on the fast audio transport lines which greatly simplifies implementation.

* VCC 9V is switched off in standby but stays hot after ACC is off
for a couple of seconds, enough to put original CD module in standby.

## What is implemented:
* CD emulator on the data bus, head unit unlocks and uses CD mode
* Bluetooth pairing and audio streaming
* Prev/Next/FF/REW/Play/Pause/etc commands.
* Enter deep sleep on standby to conserve car battery.
* Remember last connected device and auto connect on startup

## Things yet to be implemented:
* Time sync with phone playing the music. For now module increases and displays arbitrary time.
* Metadata support. ESP32 receives it and outputs it to a debug console only.

## Requirements:
* Dev board with ESP-WROOM-32 module.
* 2 way level shifter for TX/RX UART lines
* VS Code with ESP-IDF to compile the code.

Connections:

| Head unit | ESP pin      |
|:--------- | :----------- |
| VCC +9V  | Via resistor voltage divider to GPIO 27 (power monitor) |
| VUC +5V   | Module power, 5V for level shifter|
| LRCK      | GPIO 4          |
| DATA      | GPIO 18          |
| BCK       | GPIO 19           |
| TXD       | Via level shifter to GPIO 17 |
| RXD       | Via level shifter to GPIO 16 |

* We need to monitor 9V VCC line to react to it's
  state so a resistor voltage divider is needed to drop the
  voltage to ESP's required 3.3V level. I recommend 10K+20K resistor voltage divider
