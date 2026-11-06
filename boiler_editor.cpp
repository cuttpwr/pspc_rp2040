/*============================================================================
 *  boiler_editor.cpp  —  реализация строкового редактора.
 *==========================================================================*/
#include "boiler_editor.h"
#include "boiler_fs.h"
#include <string.h>
#include <stdio.h>

Editor::Editor(BoilerFS& fs, Print& out) : _fs(fs), _out(out) {
  _name[0] = '\0';
  _lastInput[0] = '\0';
}

bool Editor::enter(const char* name, bool isNew) {
  if (!name || !*name) return false;
  strncpy(_name, name, sizeof(_name) - 1);
  _name[sizeof(_name) - 1] = '\0';
  _count = 0;
  _dirty = false;
  _lastInput[0] = '\0';

  if (!isNew) {
    // загрузить существующий файл
    static char buf[8192];
    int n = _fs.readFile(_name, buf, sizeof(buf) - 1);
    if (n < 0) {
      _out.print(F("Файл не найден: ")); _out.println(_name);
      return false;
    }
    buf[n] = '\0';
    // разбить на строки
    char* saveptr = nullptr;
    char* line = strtok_r(buf, "\r\n", &saveptr);
    while (line && _count < MAX_LINES) {
      strncpy(_lines[_count], line, LINE_LEN);
      _lines[_count][LINE_LEN] = '\0';
      _count++;
      line = strtok_r(nullptr, "\r\n", &saveptr);
    }
  }

  _active = true;
  _out.print(F("=== Редактор: "));
  _out.print(_name);
  _out.print(F(" ("));
  _out.print(isNew ? F("новый") : F("загружен"));
  _out.println(F(") ==="));
  _out.println(F(".s=сохранить .q=выйти .l=список .d N=удалить .h=помощь"));
  if (_count > 0) {
    _out.print(F("Загружено строк: ")); _out.println(_count);
  }
  _out.println(F("Вводите строки. Пустая = разделитель."));
  return true;
}

const char* Editor::lineAt(int i) const {
  if (i < 0 || i >= _count) return "";
  return _lines[i];
}

bool Editor::handleLine(const char* line) {
  if (!_active) return false;
  // запомнить для дисплея
  strncpy(_lastInput, line ? line : "", LINE_LEN);
  _lastInput[LINE_LEN] = '\0';

  // команды начинаются с '.'
  if (line && line[0] == '.') {
    char cmd = line[1];
    if (cmd == '\0' || cmd == 's' || cmd == 'S') {
      doSave();
      exitEditor();
      return false;
    }
    if (cmd == 'q' || cmd == 'Q') {
      if (_dirty) _out.println(F("Несохранённые изменения! .q ещё раз для выхода без сохранения."));
      else { _out.println(F("Выход без сохранения.")); exitEditor(); return false; }
      // помечаем что подтвердили выход — упрощённо: следующий .q выходит
      _dirty = false;
      return true;
    }
    if (cmd == 'l' || cmd == 'L') { doList(); return true; }
    if (cmd == 'h' || cmd == 'H') {
      _out.println(F(". / .s — сохранить и выйти"));
      _out.println(F(".q — выйти без сохранения"));
      _out.println(F(".l — показать строки с номерами"));
      _out.println(F(".d N — удалить строку N"));
      return true;
    }
    if (cmd == 'd' || cmd == 'D') {
      int n = atoi(line + 2);
      doDelete(n);
      return true;
    }
    _out.print(F("Неизвестная команда: ")); _out.println(line);
    return true;
  }

  // обычная строка — дописать в буфер
  if (_count >= MAX_LINES) {
    _out.println(F("Буфер полон (80 строк). Сохраните (.s) и продолжите в новом файле."));
    return true;
  }
  strncpy(_lines[_count], line ? line : "", LINE_LEN);
  _lines[_count][LINE_LEN] = '\0';
  _count++;
  _dirty = true;
  _out.print(_count); _out.print(F("> "));
  _out.println(_lines[_count - 1]);
  return true;
}

void Editor::doList() {
  _out.println(F("--- содержимое ---"));
  for (int i = 0; i < _count; i++) {
    _out.print(i + 1); _out.print(F(": ")); _out.println(_lines[i]);
  }
  _out.println(F("--- конец ---"));
}

void Editor::doDelete(int n) {
  if (n < 1 || n > _count) {
    _out.println(F("Неверный номер строки."));
    return;
  }
  for (int i = n - 1; i < _count - 1; i++) {
    strcpy(_lines[i], _lines[i + 1]);
  }
  _count--;
  _dirty = true;
  _out.print(F("Удалена строка ")); _out.println(n);
}

void Editor::doSave() {
  // собрать текст из строк
  static char buf[8192];
  int pos = 0;
  for (int i = 0; i < _count; i++) {
    int len = strlen(_lines[i]);
    if (pos + len + 1 >= (int)sizeof(buf) - 1) break;
    memcpy(buf + pos, _lines[i], len);
    pos += len;
    buf[pos++] = '\n';
  }
  buf[pos] = '\0';
  if (_fs.writeFile(_name, buf)) {
    _out.print(F("Сохранено: ")); _out.print(_name);
    _out.print(F(" (")); _out.print(_count); _out.println(F(" строк)"));
  } else {
    _out.println(F("ОШИБКА записи в flash!"));
  }
}

void Editor::exitEditor() {
  _active = false;
  _out.println(F("=== выход из редактора ==="));
}
