// Seeed reTerminal Sticky (ESP32-S3R8, 32 MB flash, 3.97" 800x480 e-paper).
// Pin map from Seeed's open-source examples; see docs/development.md for sources.
#pragma once

// Power latch: both must go high at boot or the board switches off on battery.
#define PIN_POWER_HOLD 45
#define PIN_POWER_LOCK 46

// Buttons, active low. OK (the top "AI" button) is also the deep-sleep wake pin.
#define PIN_BTN_OK 4
#define PIN_BTN_UP 5
#define PIN_BTN_DOWN 6

// BQ25616 charger: enable is active low; EXTERNAL_POWER tells USB power.
#define PIN_BAT_CHG_EN 39
#define PIN_EXTERNAL_POWER 9

// Sensor bus (I2C port 1): SHT40 0x44, BQ27220 0x55, PCF8563 0x51, LSM6DS3 0x6A.
// GPIO0 is a strapping pin: leave the bus alone until after boot.
#define PIN_SENSOR_SCL 0
#define PIN_SENSOR_SDA 1
#define ADDR_SHT40 0x44
#define ADDR_BQ27220 0x55
#define ADDR_PCF8563 0x51
#define ADDR_LSM6DS3 0x6A

// E-paper on SPI2, 3-wire half duplex: the panel reads and writes on MOSI.
#define PIN_EPD_MOSI 14
#define PIN_EPD_CLK 13
#define PIN_EPD_MISO 12  // SD card only
#define PIN_EPD_CS 15
#define PIN_EPD_DC 16
#define PIN_EPD_RST 17
#define PIN_EPD_BUSY 18
#define PIN_EPD_EN 47

// MicroSD shares SPI2 with the panel.
#define PIN_SD_CS 8
#define PIN_SD_EN 10
#define PIN_SD_DETECT 11

// GT911 touch (I2C port 0).
#define PIN_TOUCH_SCL 2
#define PIN_TOUCH_SDA 3
#define PIN_TOUCH_EN 42
#define PIN_TOUCH_INT 21
#define PIN_TOUCH_RST 41

// Passive buzzer. There is no speaker on this board.
#define PIN_BUZZER 48

// PDM microphone. GPIO19/20 are the chip's USB pins, reused here: the USB-C
// port goes through a CH343 serial bridge instead. EN drives a load switch.
#define PIN_MIC_CLK 19
#define PIN_MIC_DATA 20
#define PIN_MIC_EN 38
