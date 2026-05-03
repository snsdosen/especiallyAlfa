# especiallyAlfa
### ESP32 powered internal CD module emulator for Alfa's Blaupunkt head units 

<img width="800" height="600" alt="IMG_0318" src="https://github.com/user-attachments/assets/8d140577-d5e1-46b0-ad30-b797b4ef76bb" />
<br><br>

* Add bluetooth capabilities to your Alfa 156 FL (possibly GT and 147 too) radio using only ESP32 dev module
* Internal mod, replaces and emulates CD assembly with bluetooth capable ESP32.
* Audio transfer is purely digital via I2S bus using head unit's own DAC.

## Connector pinout:
<img width="373" height="317" alt="Alfa blaupunkt pinout" src="https://github.com/user-attachments/assets/5228023b-efcb-4044-aa36-4dfc56efdd82" />

## Notes:
* This is beta software, it still needs polishing to
iron out the quirks and make the experience seamless for the user.

* Head unit uses 5V TTL logic while ESP32 is a 3.3V device
so level shifter for the UART communication is required.
<br>5V for the shifter is provided on the connector (VUC) and is
available all the time regardless if the ACC is on.

* DAC on the other hand uses 3.3V TTL logic so no shifters are needed
on the fast audio transport lines which greatly simplifies implementation.

* VCC 12+ is switched off in standby but stays hot after ACC is off
for a couple of seconds, enough to put original CD module in standby.

## What is implemented:
* CD emulator on the data bus, head unit unlocks and uses CD mode
* Bluetooth pairing and audio streaming
* Prev/Next/FF/REW/Play/Pause/etc commands.

## Things yet to be implemented:
* Enter deep sleep on standby to conserve car battery.
* Remember last connected device and auto connect on startup
* Time sync with phone playing the music. For now module increases and displays arbitrary time.
* Metadata support. ESP32 receives it and outputs it to a debug console only.

## Requirements:
* Dev board with ESP-WROOM-32 module.
* 2 way level shifter for TX/RX UART lines
* VS Code with ESP-IDF to compile the code.

Connections:

| Head unit | ESP pin      |
|:--------- | :----------- |
| VCC +12V  | Via voltage divider to GPIO 19 (power monitor) |
| VUC +5V   | Module power*, 5V for level shifter|
| LRCK      | GPIO 4          |
| DATA      | GPIO 18          |
| BCK       | GPIO 5          |
| TXD       | Via level shifter to GPIO 23 |
| RXD       | Via level shifter to GPIO 22 |

* On the bench I powered the module via USB but in the
  car it will be powered via available 5V VUC.
  <br>Further tests are needed to see if the power draw of ESP32
  is allowed on the 5V VUC.

* We need to monitor 12V VCC line to react to it's
  state so a resistor voltage divider is needed to drop the
  voltage to ESP's required 3.3V level.
