/*============================================================================
 *  boiler_pico.ino  —  точка входа v3 (pipeline engine + LittleFS + editor).
 *
 *  Архитектура:
 *    - LittleFS хранит pipeline-файлы (формулы + статика) в flash.
 *    - Shell разбирает команды (LS/NEW/EDIT/RUN/CAT/DEL/VARS/MEM/SCREEN/HELP).
 *    - Editor — интерактивный строковый редактор (.s/.q/.l/.d N).
 *    - Pipeline parser + ExprEval — исполняют pipeline, выводят результаты.
 *    - BoilerTTY тиражирует вывод CLI в Serial И в кольцевой буфер для OLED.
 *    - OLED: 4 экрана (HOME/EDIT/RUN/TERM), 4 кнопки навигации.
 *    - USB-клавиатура (опц.) — ввод наравне с Serial.
 *
 *  Аппаратные подключения (Raspberry Pi Pico):
 *    USB CDC (Serial)     — терминал 115200
 *    SSD1306 128×64 I²C   — SDA=GP4, SCL=GP5, VCC=3V3, GND=GND
 *    4 кнопки → GND       — GP15 NEXT, GP14 PREV, GP13 DEC, GP12 INC
 *    USB-A клавиатура     — D+=GP0, D-=GP1 (опц., см. boiler_keyboard.h)
 *
 *  Flash: при прошивке выберите Tools → Flash Size → "2MB (Sketch: 1MB, FS: 1MB)"
 *  (или другой размер с FS). LittleFS резервируется автоматически.
 *==========================================================================*/
#include <Arduino.h>
#include "boiler_fs.h"
#include "boiler_expr.h"
#include "boiler_pipeline.h"
#include "boiler_editor.h"
#include "boiler_shell.h"
#include "boiler_display.h"
#include "boiler_keyboard.h"
#include "boiler_tty.h"

static BoilerFS      fs;
static BoilerTTY     tty;
static Editor*       editor = nullptr;
static Shell*        shell  = nullptr;
static BoilerDisplay disp;
static KeyboardInput keyboard;
static String        inputBuf;

static void handleInputChar(char c, bool fromKeyboard);
static void executeLine(const String& line);
static void seedDemoPipelines();
static void updateKbMode();

/*==========================================================================*/
void setup() {
  Serial.begin(115200);
  delay(50);

  tty.begin();
  fs.init();
  keyboard.begin();

  // editor и shell создаются после fs/tty (нужны ссылки)
  editor = new Editor(fs, tty);
  shell  = new Shell(fs, *editor, tty);

  disp.begin();
  disp.setTTY(&tty);
  disp.setShell(shell);
  disp.setFS(&fs);
  shell->setDisplay(&disp);
  disp.setScreen(BoilerDisplay::SCR_HOME);

  // баннер
  tty.println();
  tty.println(F("============================================"));
  tty.println(F("  PicoStarPowerCalc (PSPC)"));
  tty.println(F("  Pi Pico + LittleFS + ExprEval + Editor"));
  tty.println(F("  Формулы в flash, исполнение на устройстве"));
  tty.println(F("============================================"));
  tty.print(F("Flash: ")); tty.print(fs.usedBytes());
  tty.print(F(" / ")); tty.print(fs.totalBytes()); tty.println(F(" bytes"));

  // сидинг демо-pipeline при пустой flash
  FileList list;
  if (fs.listFiles(list) == 0) {
    tty.println(F("Flash пуста — сею демо-pipeline..."));
    seedDemoPipelines();
    disp.markDirty();
  }

  tty.println(F("Команды: LS NEW EDIT RUN CAT DEL VARS MEM HELP"));
  tty.println(F("SCREEN home|edit|run|term|clear"));
  tty.println(F("Кнопки: GP15 next/run, GP14 prev/screen, GP13 edit"));
  tty.println();
  tty.print(F("> "));

  disp.render();
}

/*==========================================================================*/
void loop() {
  uint32_t now = millis();

  updateKbMode();

  // кнопки: RUN / EDIT выбранного файла
  if (disp.consumeRunRequest()) {
    const char* fn = disp.selectedFile();
    if (fn && *fn) {
      tty.println();
      tty.print(F("[BTN: RUN ")); tty.print(fn); tty.println(F("]"));
      String cmd = F("RUN "); cmd += fn;
      shell->process(cmd.c_str());
      disp.setScreen(BoilerDisplay::SCR_RUN);
      tty.print(F("> "));
    } else {
      tty.println(F("[BTN: нет файла для RUN]"));
      tty.print(F("> "));
    }
  }
  if (disp.consumeEditRequest()) {
    const char* fn = disp.selectedFile();
    if (fn && *fn) {
      tty.println();
      tty.print(F("[BTN: EDIT ")); tty.print(fn); tty.println(F("]"));
      String cmd = F("EDIT "); cmd += fn;
      shell->process(cmd.c_str());
      tty.print(F("> "));
    }
  }

  // отслеживание editor-режима → автопереключение экрана
  static bool editorWasActive = false;
  bool edActive = editor->isActive();
  if (edActive != editorWasActive) {
    editorWasActive = edActive;
    disp.notifyEditorActive(edActive);
  }

  // ввод из Serial
  while (Serial.available()) {
    char c = (char)Serial.read();
    handleInputChar(c, false);
  }

  // ввод с клавиатуры
  while (keyboard.available()) {
    int ch = keyboard.read();
    if (ch >= 0) handleInputChar((char)ch, true);
  }

  // кнопки навигации + рендер
  disp.pollButton(now);
  disp.render();
}

/*==========================================================================*/
static bool kbWasMounted = false;
static void updateKbMode() {
  bool now = keyboard.mounted();
  if (now != kbWasMounted) {
    kbWasMounted = now;
    if (now) {
      tty.println(F("[Keyboard mounted]"));
      disp.setScreen(BoilerDisplay::SCR_TERM);
    } else {
      tty.println(F("[Keyboard unmounted]"));
      disp.setScreen(BoilerDisplay::SCR_HOME);
    }
    tty.print(F("> "));
  }
}

/*==========================================================================*/
static void handleInputChar(char c, bool fromKeyboard) {
  if (c == '\r') return;
  if (c == '\n') {
    if (fromKeyboard) tty.println();
    inputBuf.trim();
    if (inputBuf.length() == 0) {
      // пустой Enter: в редакторе = разделитель, в шелле = noop
      if (editor->isActive()) {
        editor->handleLine("");
      }
      inputBuf = String();
      tty.setInputLine("");
      disp.markDirty();
      tty.print(F("> "));
      return;
    }
    tty.pushLine(inputBuf.c_str());
    executeLine(inputBuf);
    inputBuf = String();
    tty.setInputLine("");
    disp.markDirty();
    tty.print(F("> "));
    return;
  }
  // Backspace
  if (c == 0x08 || c == 0x7F) {
    if (inputBuf.length() > 0) {
      inputBuf.remove(inputBuf.length() - 1);
      tty.setInputLine(inputBuf.c_str());
      disp.markDirty();
      if (fromKeyboard) Serial.write(c);
    }
    return;
  }
  if (inputBuf.length() < 120) {
    inputBuf += c;
    tty.setInputLine(inputBuf.c_str());
    disp.markDirty();
    if (fromKeyboard) Serial.write(c);
  }
}

/*==========================================================================*/
static void executeLine(const String& line) {
  shell->process(line.c_str());
}

/*==========================================================================
 *  seedDemoPipelines — записывает примеры в flash при первом старте.
 *==========================================================================*/
static void seedDemoPipelines() {
  // --- Водогрейная котельная ---
  const char* vod =
    "# Водогрейная котельная (вариант 15)\n"
    "# --- статические данные ---\n"
    "Qov = 34\n"
    "Qgv = 10\n"
    "t1 = 110\n"
    "t2 = 50\n"
    "eta = 0.98\n"
    "Kts = 2\n"
    "c = 4.19\n"
    "# --- формулы ---\n"
    "Q = Qov + Qgv\n"
    "G = Q * 1000 / (c * (t1 - t2))\n"
    "Gpod = G / Kts\n"
    "dG = abs(G - Gpod) / G * 100\n"
    "# --- вывод ---\n"
    "print \"=== Vodogreyny v15 ===\"\n"
    "print Q \" MW\"\n"
    "print G \" t/h\"\n"
    "print Gpod \" t/h\"\n"
    "print dG \" pct\"\n"
    "print eta\n";
  if (fs.writeFile("vodogreyny", vod))
    tty.println(F("  created: vodogreyny"));
  else
    tty.println(F("  FAIL: vodogreyny"));

  // --- Паровая котельная ---
  const char* par =
    "# Паровая котельная\n"
    "# --- статика ---\n"
    "Q = 31\n"
    "Dt = 10\n"
    "hrou = 2833\n"
    "hpv = 437\n"
    "eta = 0.98\n"
    "Ksn = 4\n"
    "# --- формулы ---\n"
    "Dk = Q * 1000 / (hrou - hpv)\n"
    "D = Dk * Ksn\n"
    "# --- вывод ---\n"
    "print \"=== Parovoy ===\"\n"
    "print Dk \" t/h\"\n"
    "print D \" t/h\"\n"
    "print eta\n";
  if (fs.writeFile("parovoy", par))
    tty.println(F("  created: parovoy"));
  else
    tty.println(F("  FAIL: parovoy"));

  // --- Демо условной логики ---
  const char* cond =
    "# Демо условной логики (тернарный оператор)\n"
    "scheme = 1\n"
    "G = 100\n"
    "Kts = 2\n"
    "Gpod = (scheme == 1) ? G / Kts : G\n"
    "print \"=== Conditional demo ===\"\n"
    "print Gpod \" t/h\"\n"
    "print \"scheme=1 -> Gpod=G/Kts\"\n";
  if (fs.writeFile("demo_cond", cond))
    tty.println(F("  created: demo_cond"));
  else
    tty.println(F("  FAIL: demo_cond"));
}
