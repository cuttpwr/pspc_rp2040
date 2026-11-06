/*============================================================================
 *  boiler_display.h  —  OLED UI v3: 4 экрана + 4 кнопки.
 *
 *  Экраны:
 *    SCR_HOME  — список pipeline-файлов в flash, курсор выбирает.
 *                GP15/GP14 — навигация, GP15 long — RUN выбранного,
 *                GP13 long — EDIT выбранного.
 *    SCR_EDIT  — состояние редактора: имя файла, число строк, последняя
 *                введённая строка.
 *    SCR_RUN   — результаты последнего прогона (постранично, 7/стр).
 *    SCR_TERM  — 7 строк вывода CLI + текущий ввод внизу.
 *
 *  Кнопки (все INPUT_PULLUP, active-low):
 *    GP15 NEXT — short: след. файл/страница | long: RUN выбранного
 *    GP14 PREV — short: пред. файл/страница | long: цикл экранов
 *    GP13 DEC  — short: (no-op / scroll)    | long: EDIT выбранного
 *    GP12 INC  — short: (no-op / scroll)    | long: (no-op)
 *==========================================================================*/
#pragma once
#include <Arduino.h>

class BoilerTTY;
class Shell;
class BoilerFS;

class BoilerDisplay {
public:
  enum Screen : uint8_t { SCR_HOME, SCR_EDIT, SCR_RUN, SCR_TERM };

  void begin();
  void setTTY(BoilerTTY* t) { _tty = t; }
  void setShell(Shell* s)   { _shell = s; }
  void setFS(BoilerFS* fs)  { _fs = fs; }

  void setScreen(Screen s) { _screen = s; _dirty = true; if (s == SCR_HOME) _listDirty = true; }
  void setScreenByName(const char* name);
  void markDirty() { _dirty = true; }
  void render();

  Screen currentScreen() const { return _screen; }

  // кнопки
  void pollButton(uint32_t now);
  // запросы для главного цикла
  bool consumeRunRequest();    // GP15 long → RUN выбранного файла
  bool consumeEditRequest();   // GP13 long → EDIT выбранного файла
  // имя выбранного файла (для RUN/EDIT)
  const char* selectedFile() const;

  // редактор активен → автопереключение на EDIT экран
  void notifyEditorActive(bool active);

  // обновить кэш списка файлов (после DEL/NEW из шелла).
  // Вызывается Shell'ом — поэтому public.
  void refreshFileList();

private:
  void drawHome();
  void drawEdit();
  void drawRun();
  void drawTerminal();
  void cycleScreen();

  BoilerTTY* _tty = nullptr;
  Shell*     _shell = nullptr;
  BoilerFS*  _fs = nullptr;

  Screen _screen = SCR_HOME;
  bool   _dirty = true;
  bool   _listDirty = true;
  int    _selFile = 0;
  int    _runPage = 0;
  int    _lastLineCount = -1;
  Screen _prevScreen = SCR_HOME;   // экран до входа в EDIT — туда вернёмся при выходе

  // кэш списка файлов
  static const int MAX_F = 16;
  char   _files[MAX_F][24];
  int    _fileCount = 0;

  // 4 кнопки
  static const uint32_t LONG_PRESS_MS = 1200;
  struct BtnState {
    bool wasDown = false;
    uint32_t pressStart = 0;
    bool longFired = false;
    bool shortEvent = false;
    bool longEvent = false;
  };
  BtnState _bNext, _bPrev, _bDec, _bInc;
  static void _pollOne(int pin, BtnState& s, uint32_t now);
};
