/*============================================================================
 *  boiler_keyboard.h  —  USB HID-клавиатура через Pico-PIO-USB (host on GP0/GP1)
 *
 *  Аппаратные подключения (опционально, для автономной работы):
 *    USB-A разъём:
 *      pin 1 (VBUS, +5V) ← Pico VBUS (pin 40, 5V от USB-порта Pico)
 *      pin 2 (D-)        → Pico GP1 + 15kΩ pulldown на GND
 *      pin 3 (D+)        → Pico GP0 + 15kΩ pulldown на GND
 *      pin 4 (GND)       → Pico GND
 *    Дополнительно: 22Ω последовательно на D+/D- (опционально, для надёжности).
 *
 *  Библиотеки:
 *    Pico-PIO-USB (Library Manager: "Pico-PIO-USB", или git clone
 *      https://github.com/sekigon-gonnoc/Pico-PIO-USB )
 *    Adafruit TinyUSB Library (обычно уже идёт с arduino-pico)
 *
 *  Build flags (для включения USB host в TinyUSB):
 *    -DCFG_TUH_ENABLED=1 -DCFG_TUH_HID=4
 *
 *  АРХИТЕКТУРА:
 *    Core 0 (Arduino loop): читает из кольцевого буфера клавиш, как из Serial.
 *    Core 1 (запускается в begin()): бесконечный цикл tuh_task() — обрабатывает
 *      USB-host события. Колбэки tuh_hid_report_received_cb() пушат ASCII-символы
 *      в кольцевый буфер.
 *
 *  ЧТОБЫ ВКЛЮЧИТЬ клавиатуру: раскомментируйте #define KB_ENABLED ниже.
 *  Без этого клавиатура — заглушка (begin/available/read возвращают пусто),
 *  остальной код работает как раньше.
 *==========================================================================*/
#pragma once
#include <Arduino.h>

// === Раскомментируй, чтобы включить USB-клавиатуру ===
// #define KB_ENABLED

class KeyboardInput {
public:
  void begin();              // запуск core1 (host task loop)
  bool available();          // есть ли символ в буфере
  int  read();               // прочитать один символ (-1 если пусто)
  bool mounted();            // подключена ли клавиатура
};
