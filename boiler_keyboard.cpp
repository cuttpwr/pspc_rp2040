/*============================================================================
 *  boiler_keyboard.cpp  —  реализация USB HID-клавиатуры
 *
 *  Паттерн повторяет официальный пример Pico-PIO-USB:
 *    examples/host_hid_to_device_cdc/host_hid_to_device_cdc.c
 *  Core 0: Arduino loop (native USB CDC — Serial).
 *  Core 1: tuh_task() — host-стек на PIO USB (GP0/GP1).
 *
 *  Сборка с KB_ENABLED требует:
 *    1. Установленная библиотека Pico-PIO-USB
 *    2. Build flags: -DCFG_TUH_ENABLED=1 -DCFG_TUH_HID=4
 *    3. arduino-pico core версии >= 3.x (с поддержкой Pico-PIO-USB интеграции)
 *==========================================================================*/
#include "boiler_keyboard.h"

#ifdef KB_ENABLED
/*---------------------------------------------------------------------------
 *  Полная реализация с Pico-PIO-USB
 *-------------------------------------------------------------------------*/
#include <pico/multicore.h>

// C-библиотеки TinyUSB и Pico-PIO-USB нужно включать через extern "C",
// иначе C++ их не слинкует (колбэки tuh_hid_* имеют C-линковку).
extern "C" {
#include "pio_usb.h"
#include "tusb.h"
}

// Таблица keycode → ASCII из hid.h TinyUSB (HID_KEYCODE_TO_ASCII — макрос
// инициализатора 128×2). Объявляем как static const — будет жить в flash.
static uint8_t const keycode2ascii[128][2] = { HID_KEYCODE_TO_ASCII };

// --- кольцевой буфер (single producer = core1 callback, single consumer = core0 loop) ---
#define KB_BUF_SIZE 128
static volatile char     kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0;
static volatile uint32_t kb_tail = 0;
static volatile bool     kb_mounted = false;

static inline bool kb_find_in_report(hid_keyboard_report_t const *r, uint8_t kc) {
  for (uint8_t i = 0; i < 6; i++) if (r->keycode[i] == kc) return true;
  return false;
}

// Пуш символа в буфер (вызывается из core1, в колбэке).
// Single-producer — атомарная запись head'а без блокировок.
static void kb_push(char c) {
  uint32_t next = (kb_head + 1) % KB_BUF_SIZE;
  if (next == kb_tail) return;   // буфер полон — теряем символ (редкий случай)
  kb_buf[kb_head] = c;
  kb_head = next;
}

// Core 1: бесконечный цикл обработки host-событий.
static void core1_main() {
  sleep_ms(10);
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;  // D+ = GP0, D- = GP1
  tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);
  tuh_init(1);          // root hub port 1 = PIO USB (host)
  while (true) {
    tuh_task();
    tight_loop_contents();
  }
}

// === TinyUSB host-колбэки (C-линковка) ===
extern "C" {

// Клавиатура подключена — запрашиваем первый репорт.
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
  (void)desc_report; (void)desc_len;
  uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    kb_mounted = true;
    // Запросить первый interrupt-IN репорт; следующие будут идти автоматически
    // через tuh_hid_report_received_cb → tuh_hid_receive_report (цепочка).
    tuh_hid_receive_report(dev_addr, instance);
  }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  (void)dev_addr; (void)instance;
  kb_mounted = false;
}

// Пришёл interrupt-IN репорт от клавиатуры.
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
  (void)len;
  uint8_t itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
    hid_keyboard_report_t const *kbd = (hid_keyboard_report_t const*)report;
    static hid_keyboard_report_t prev_report = {0};
    bool const shift = kbd->modifier &
                       (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
    // 6-key rollover: смотрим только НОВЫЕ нажатия (отсутствовавшие в prev)
    for (uint8_t i = 0; i < 6; i++) {
      uint8_t kc = kbd->keycode[i];
      if (kc && !kb_find_in_report(&prev_report, kc)) {
        uint8_t ch = keycode2ascii[kc][shift ? 1 : 0];
        if (ch) {
          // Enter приводим к \n (главный парсер ждёт \n)
          if (ch == '\r') kb_push('\n');
          else            kb_push((char)ch);
        }
      }
    }
    prev_report = *kbd;
  }
  // Запросить следующий репорт (цепочка)
  tuh_hid_receive_report(dev_addr, instance);
}

}  // extern "C"

// === Public API ===

void KeyboardInput::begin() {
  multicore_launch_core1(core1_main);
}

bool KeyboardInput::available() {
  return kb_head != kb_tail;
}

int KeyboardInput::read() {
  if (kb_head == kb_tail) return -1;
  char c = kb_buf[kb_tail];
  kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
  return (int)(uint8_t)c;
}

bool KeyboardInput::mounted() {
  return kb_mounted;
}

#else
/*---------------------------------------------------------------------------
 *  Заглушка — клавиатура выключена (KB_ENABLED не определён).
 *  Код компилируется и работает без Pico-PIO-USB библиотеки и build flags.
 *-------------------------------------------------------------------------*/
void KeyboardInput::begin()                 {}
bool KeyboardInput::available()             { return false; }
int  KeyboardInput::read()                  { return -1; }
bool KeyboardInput::mounted()               { return false; }
#endif
