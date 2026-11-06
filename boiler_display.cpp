/*============================================================================
 *  boiler_display.cpp  —  реализация OLED UI v3.
 *==========================================================================*/
#include "boiler_display.h"
#include "boiler_tty.h"
#include "boiler_shell.h"
#include "boiler_fs.h"
#include "boiler_pipeline.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
#define OLED_RESET -1
#define BTN_NEXT 15
#define BTN_PREV 14
#define BTN_DEC  13
#define BTN_INC  12
#define ROWS 6          // HOME: 6 строк + футер y=56 (7-я налезала бы на футер)
#define ROWS_RUN 7      // RUN: 7 строк, футера нет

static Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, OLED_RESET);

// транслит UTF-8 → ASCII (для TERMINAL-экрана)
static const char D0_MAP[64] = {
  'A','B','V','G','D','E','Z','Z','I','J','K','L','M','N','O','P',
  'R','S','T','U','F','H','C','C','S','S','\'','Y','\'','E','U','A',
  'a','b','v','g','d','e','z','z','i','j','k','l','m','n','o','p'
};
static const char D1_MAP[16] = {
  'r','s','t','u','f','h','c','c','s','s','\'','y','\'','e','u','a'
};
static void translit(const char* in, char* out, size_t maxOut) {
  if (!in || !out || maxOut == 0) { if (out && maxOut) out[0] = '\0'; return; }
  size_t oi = 0, ii = 0;
  size_t inLen = strlen(in);
  while (ii < inLen && oi + 1 < maxOut) {
    uint8_t b = (uint8_t)in[ii];
    if (b < 0x80) { out[oi++] = (char)b; ii++; continue; }
    if ((b & 0xE0) == 0xC0 && ii + 1 < inLen) {
      uint8_t b2 = (uint8_t)in[ii + 1];
      char c = '?';
      if (b == 0xD0 && b2 >= 0x90 && b2 <= 0xBF) c = D0_MAP[b2 - 0x90];
      else if (b == 0xD1 && b2 >= 0x80 && b2 <= 0x8F) c = D1_MAP[b2 - 0x80];
      else if (b == 0xD0 && b2 == 0x81) c = 'E';   // Ё (uppercase) — выпадало из D0_MAP
      else if (b == 0xD1 && b2 == 0x91) c = 'e';    // ё (lowercase)
      else if (b == 0xC2 && b2 == 0xB0) c = 'o';    // ° → 'o' (был пробел — терялся)
      else if (b == 0xC2 && b2 == 0xAB) c = '<';
      else if (b == 0xC2 && b2 == 0xBB) c = '>';
      out[oi++] = c; ii += 2; continue;
    }
    if ((b & 0xF0) == 0xE0 && ii + 2 < inLen) {
      uint8_t b2 = (uint8_t)in[ii + 1], b3 = (uint8_t)in[ii + 2];
      char c = '?';
      if (b == 0xE2 && b2 == 0x80 && b3 == 0x94) c = '-';
      else if (b == 0xE2 && b2 == 0x86 && b3 == 0x92) c = '>';
      out[oi++] = c; ii += 3; continue;
    }
    ii++;
  }
  out[oi] = '\0';
}

static void fmtVal(float v, char* buf, size_t n) {
  if (isnan(v) || isinf(v)) { strncpy(buf, "nan", n); return; }
  if (fabsf(v - roundf(v)) < 0.005f) snprintf(buf, n, "%ld", (long)roundf(v));
  else                                snprintf(buf, n, "%.2f", v);
}

//============================================================================
void BoilerDisplay::begin() {
  oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  oled.clearDisplay();
  oled.display();
  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);
  pinMode(BTN_DEC,  INPUT_PULLUP);
  pinMode(BTN_INC,  INPUT_PULLUP);
  _dirty = true;
  _listDirty = true;
}

void BoilerDisplay::setScreenByName(const char* name) {
  if (!name || !*name) return;
  if (!strcasecmp(name, "home"))        setScreen(SCR_HOME);
  else if (!strcasecmp(name, "edit"))   setScreen(SCR_EDIT);
  else if (!strcasecmp(name, "run"))    setScreen(SCR_RUN);
  else if (!strcasecmp(name, "term") || !strcasecmp(name, "terminal")) setScreen(SCR_TERM);
  else if (!strcasecmp(name, "clear"))  { if (_tty) _tty->clear(); markDirty(); }
}

void BoilerDisplay::refreshFileList() {
  _fileCount = 0;
  if (!_fs) return;
  FileList list;
  _fs->listFiles(list);
  for (int i = 0; i < list.count && i < MAX_F; i++) {
    strncpy(_files[i], list.names[i], 23);
    _files[i][23] = '\0';
    _fileCount++;
  }
  if (_selFile >= _fileCount) _selFile = _fileCount - 1;
  if (_selFile < 0) _selFile = 0;
  _listDirty = false;
}

const char* BoilerDisplay::selectedFile() const {
  if (_fileCount <= 0 || _selFile < 0 || _selFile >= _fileCount) return "";
  return _files[_selFile];
}

void BoilerDisplay::cycleScreen() {
  switch (_screen) {
    case SCR_HOME: _screen = SCR_EDIT; break;
    case SCR_EDIT: _screen = SCR_RUN;  break;
    case SCR_RUN:  _screen = SCR_TERM; break;
    case SCR_TERM: _screen = SCR_HOME; _listDirty = true; break;
  }
  _dirty = true;
}

void BoilerDisplay::notifyEditorActive(bool active) {
  if (active && _screen != SCR_EDIT) {
    _prevScreen = _screen;          // запомнили, откуда пришли
    _screen = SCR_EDIT; _dirty = true;
  }
  else if (!active && _screen == SCR_EDIT) {
    _screen = _prevScreen;          // возвращаемся туда, откуда вошли (а не всегда HOME)
    if (_screen == SCR_HOME) _listDirty = true;
    _dirty = true;
  }
}

//============================================================================
//  4 кнопки
//============================================================================
void BoilerDisplay::_pollOne(int pin, BtnState& s, uint32_t now) {
  bool down = (digitalRead(pin) == LOW);
  if (down && !s.wasDown) { s.pressStart = now; s.longFired = false; }
  if (down && !s.longFired && (now - s.pressStart) >= LONG_PRESS_MS) {
    s.longEvent = true; s.longFired = true;
  }
  if (!down && s.wasDown && !s.longFired) s.shortEvent = true;
  s.wasDown = down;
}

void BoilerDisplay::pollButton(uint32_t now) {
  _pollOne(BTN_NEXT, _bNext, now);
  _pollOne(BTN_PREV, _bPrev, now);
  _pollOne(BTN_DEC,  _bDec,  now);
  _pollOne(BTN_INC,  _bInc,  now);

  // GP15/GP14 short → навигация
  if (_bNext.shortEvent) {
    _bNext.shortEvent = false;
    if (_screen == SCR_HOME && _fileCount > 0) {
      _selFile = (_selFile + 1) % _fileCount; _dirty = true;
    } else if (_screen == SCR_RUN) {
      _runPage++; _dirty = true;
    }
  }
  if (_bPrev.shortEvent) {
    _bPrev.shortEvent = false;
    if (_screen == SCR_HOME && _fileCount > 0) {
      _selFile = (_selFile == 0) ? _fileCount - 1 : _selFile - 1; _dirty = true;
    } else if (_screen == SCR_RUN) {
      if (_runPage > 0) _runPage--; _dirty = true;
    }
  }
  // GP14 long → цикл экранов
  if (_bPrev.longEvent) { _bPrev.longEvent = false; cycleScreen(); }
}

bool BoilerDisplay::consumeRunRequest() {
  bool r = _bNext.longEvent; _bNext.longEvent = false; return r;
}
bool BoilerDisplay::consumeEditRequest() {
  bool r = _bDec.longEvent; _bDec.longEvent = false; return r;
}

//============================================================================
void BoilerDisplay::render() {
  if (!_dirty) {
    if (_screen == SCR_TERM && _tty) {
      int lc = _tty->lineCount();
      if (lc != _lastLineCount) { _dirty = true; _lastLineCount = lc; }
    }
    if (_screen == SCR_HOME && _listDirty) _dirty = true;
  }
  if (!_dirty) return;
  switch (_screen) {
    case SCR_HOME:  drawHome();  break;
    case SCR_EDIT:  drawEdit();  break;
    case SCR_RUN:   drawRun();   break;
    case SCR_TERM:  drawTerminal(); break;
  }
  _dirty = false;
}

//============================================================================
void BoilerDisplay::drawHome() {
  if (_listDirty) refreshFileList();
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextWrap(false);

  // заголовок
  oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F(" PIPELINES "));
  if (_fileCount > 0) {
    oled.print(_selFile + 1); oled.print(F("/")); oled.print(_fileCount);
  }
  oled.setTextColor(SSD1306_WHITE);

  if (_fileCount == 0) {
    oled.setCursor(0, 16);
    oled.print(F("Flash empty"));
    oled.setCursor(0, 26);
    oled.print(F("NEW <name>"));
    oled.setCursor(0, 36);
    oled.print(F("on keyboard"));
    oled.display();
    return;
  }

  int page = _selFile / ROWS;
  int start = page * ROWS;
  int y = 10;
  for (int i = 0; i < ROWS && start + i < _fileCount; i++) {
    int idx = start + i;
    bool sel = (idx == _selFile);
    if (sel) oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
    oled.setCursor(0, y);
    oled.print(F(">"));
    oled.setCursor(8, y);
    oled.print(_files[idx]);
    if (sel) oled.setTextColor(SSD1306_WHITE);
    y += 8;
  }
  // подсказка внизу
  oled.setCursor(0, 56);
  oled.print(F("GP15>run GP13>edit"));
  oled.display();
}

//============================================================================
void BoilerDisplay::drawEdit() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextWrap(false);

  oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F(" EDIT "));
  oled.setTextColor(SSD1306_WHITE);

  // без активного редактора — подсказка
  if (!_shell) {
    oled.setCursor(0, 16);
    oled.print(F("No editor"));
    oled.display();
    return;
  }

  // имя и число строк — через shell/editor (упрощённо: читаем tty input)
  oled.setCursor(0, 12);
  if (_tty) {
    char raw[100], asc[40];
    _tty->getInputLine(raw, sizeof raw);
    translit(raw, asc, sizeof asc);
    oled.print(F("> ")); oled.print(asc);
  }
  oled.setCursor(0, 24);
  oled.print(F(".s=save .q=quit"));
  oled.setCursor(0, 34);
  oled.print(F(".l=list .d N=del"));
  oled.setCursor(0, 48);
  oled.print(F("Type lines on"));
  oled.setCursor(0, 56);
  oled.print(F("USB keyboard"));
  oled.display();
}

//============================================================================
void BoilerDisplay::drawRun() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextWrap(false);

  oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  oled.setCursor(0, 0);
  oled.print(F(" RESULTS "));
  oled.setTextColor(SSD1306_WHITE);

  if (!_shell || !_shell->hasResults()) {
    oled.setCursor(0, 16);
    oled.print(F("No results"));
    oled.setCursor(0, 26);
    oled.print(F("RUN <name>"));
    oled.display();
    return;
  }

  const PipelineRunner& r = _shell->runner();
  int total = r.resultCount();
  int pages = (total + ROWS_RUN - 1) / ROWS_RUN;
  if (pages == 0) pages = 1;
  if (_runPage >= pages) _runPage = pages - 1;
  if (_runPage < 0) _runPage = 0;

  oled.setCursor(90, 0);
  oled.print(_runPage + 1); oled.print(F("/")); oled.print(pages);

  int start = _runPage * ROWS_RUN;
  int y = 8;
  for (int i = 0; i < ROWS_RUN && start + i < total; i++) {
    const ResultRow& row = r.result(start + i);
    if (row.isNote) {
      oled.setCursor(0, y);
      char asc[24]; translit(row.label, asc, sizeof asc);
      oled.print(asc);
    } else {
      char vb[12]; fmtVal(row.value, vb, sizeof vb);
      oled.setCursor(0, y);
      oled.print(row.label);
      oled.setCursor(70, y); oled.print(vb);
      if (row.unit[0]) { oled.setCursor(104, y); oled.print(row.unit); }
    }
    y += 8;
  }
  oled.display();
}

//============================================================================
void BoilerDisplay::drawTerminal() {
  if (!_tty) { drawHome(); return; }
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextWrap(false);

  // 7 строк вывода (y=0,8,16,24,32,40,48), ввод — y=56
  const int VIS = 7;
  int lc = _tty->lineCount();
  int startIdx = lc - VIS;
  if (startIdx < 0) startIdx = 0;
  char raw[128], asc[28];
  int y = 0;
  for (int i = startIdx; i < lc; i++) {
    _tty->getLine(i, raw, sizeof raw);
    translit(raw, asc, sizeof asc);
    oled.setCursor(0, y);
    oled.print(asc);
    y += 8;
  }
  // ввод
  oled.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
  oled.setCursor(0, 56);
  oled.print(F("> "));
  _tty->getInputLine(raw, sizeof raw);
  translit(raw, asc, sizeof asc);
  oled.print(asc);
  oled.setTextColor(SSD1306_WHITE);
  oled.display();
}
