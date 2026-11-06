/*============================================================================
 *  boiler_editor.h  —  интерактивный строковый редактор pipeline.
 *
 *  Активируется командами NEW <name> / EDIT <name> в шелле. В режиме
 *  редактора каждая введённая строка дописывается в буфер. Управляющие
 *  команды начинаются с '.':
 *
 *    .         или .s   — сохранить и выйти
 *    .q                 — выйти без сохранения
 *    .l                 — показать текущее содержимое (с номерами строк)
 *    .d N               — удалить строку N (1-индексация)
 *    .h                 — помощь по командам редактора
 *
 *  Пустая строка (просто Enter) дописывается как пустая — она игнорируется
 *  парсером pipeline, но полезна для визуального разделения блоков.
 *
 *  Вместимость: 80 строк × 96 символов. При переполнении — предупреждение.
 *==========================================================================*/
#pragma once
#include <Arduino.h>

class BoilerFS;
class BoilerDisplay;

class Editor {
public:
  Editor(BoilerFS& fs, Print& out);

  // Начать редактирование. isNew=true → пустой буфер; false → загрузить файл.
  // Возвращает true если вошли в режим (файл существует при EDIT, или имя валидно).
  bool enter(const char* name, bool isNew);

  // Обработать строку ввода. Возвращает true если всё ещё в режиме редактора,
  // false — вышли (save/quit). Вызывается главным циклом для каждой строки,
  // пока isActive().
  bool handleLine(const char* line);

  bool isActive() const { return _active; }
  const char* currentName() const { return _name; }
  int  lineCount() const { return _count; }
  // строка по индексу (0-индексация) — для дисплея
  const char* lineAt(int i) const;

  static const int MAX_LINES = 80;
  static const int LINE_LEN  = 96;

private:
  BoilerFS& _fs;
  Print& _out;
  bool   _active = false;
  char   _name[24];
  char   _lines[MAX_LINES][LINE_LEN + 1];
  int    _count = 0;
  bool   _dirty = false;

  void doSave();
  void doList();
  void doDelete(int n);
  void exitEditor();
};
