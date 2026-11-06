/*============================================================================
 *  boiler_shell.h  —  командный шелл. Разбирает строки ввода и исполняет.
 *
 *  Команды (регистронезависимые):
 *    LS                      — список pipeline-файлов в flash
 *    NEW <name>              — создать новый pipeline, войти в редактор
 *    EDIT <name>             — редактировать существующий
 *    RUN <name>              — выполнить pipeline, вывести результаты
 *    CAT <name>              — показать содержимое файла
 *    DEL <name>              — удалить файл
 *    VARS                    — переменные последнего прогона
 *    MEM                     — статистика flash (used/total)
 *    SCREEN home|edit|run|term|clear  — управление экраном OLED
 *    HELP                    — справка
 *
 *  Если активен редактор (NEW/EDIT), все строки уходят в него, кроме выхода.
 *==========================================================================*/
#pragma once
#include <Arduino.h>
#include "boiler_pipeline.h"

class BoilerFS;
class Editor;
class BoilerDisplay;

class Shell {
public:
  Shell(BoilerFS& fs, Editor& editor, Print& out);

  // Обработать строку. Если редактор активен — делегирует ему.
  void process(const char* line);

  // доступ к результатам последнего RUN — для дисплея
  const PipelineRunner& runner() const { return _runner; }
  bool hasResults() const { return _hasResults; }

  // колбэк для управления экраном OLED (SCREEN команда)
  typedef void (*ScreenCb)(const char* arg, BoilerDisplay* disp);
  void setDisplay(BoilerDisplay* disp) { _disp = disp; }

private:
  BoilerFS& _fs;
  Editor&   _editor;
  Print&    _out;
  PipelineParser _parser;
  PipelineRunner _runner;
  VarTable  _vars;
  Pipeline  _parsed;
  bool      _hasResults = false;
  BoilerDisplay* _disp = nullptr;

  // разбор одной команды (не редактор)
  void dispatch(const char* line);
  void cmdLs();
  void cmdNew(const char* name);
  void cmdEdit(const char* name);
  void cmdRun(const char* name);
  void cmdCat(const char* name);
  void cmdDel(const char* name);
  void cmdVars();
  void cmdMem();
  void cmdScreen(const char* arg);
  void cmdHelp();

  // утилита: извлечь слово из строки (разделитель — пробел)
  static void word(const char* s, int idx, char* out, size_t n);
};
