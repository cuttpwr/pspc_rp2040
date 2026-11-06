/*============================================================================
 *  boiler_shell.cpp  —  реализация командного шелла.
 *==========================================================================*/
#include "boiler_shell.h"
#include "boiler_fs.h"
#include "boiler_editor.h"
#include "boiler_display.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

Shell::Shell(BoilerFS& fs, Editor& editor, Print& out)
  : _fs(fs), _editor(editor), _out(out), _runner(out) {}

// извлечь слово idx (0-индексация) из строки, разделённой пробелами
void Shell::word(const char* s, int idx, char* out, size_t n) {
  if (!s || !out || n == 0) { if (out && n) out[0] = '\0'; return; }
  out[0] = '\0';
  int cur = 0;
  const char* p = s;
  while (*p) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    const char* start = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (cur == idx) {
      size_t len = p - start;
      if (len >= n) len = n - 1;
      memcpy(out, start, len);
      out[len] = '\0';
      return;
    }
    cur++;
  }
}

static bool eqi(const char* a, const char* b) {
  return a && b && strcasecmp(a, b) == 0;
}

// Валидация имени файла для LittleFS: только буквы/цифры/_/-, до 23 символов.
// Защищает от "..", "/", "\\0", слишком длинных имён.
static bool validFileName(const char* s) {
  if (!s || !*s) return false;
  size_t len = strlen(s);
  if (len > 23) return false;
  for (const char* p = s; *p; p++) {
    unsigned char c = (unsigned char)*p;
    if (!(isalnum(c) || c == '_' || c == '-')) return false;
  }
  return true;
}

void Shell::process(const char* line) {
  if (!line) return;
  // редактор перехватывает весь ввод
  if (_editor.isActive()) {
    _editor.handleLine(line);
    return;
  }
  dispatch(line);
}

void Shell::dispatch(const char* line) {
  char cmd[24];
  word(line, 0, cmd, sizeof(cmd));
  if (cmd[0] == '\0') return;   // пустая строка

  if (eqi(cmd, "LS") || eqi(cmd, "DIR")) { cmdLs(); return; }
  if (eqi(cmd, "NEW")) { char n[24]; word(line, 1, n, sizeof n); cmdNew(n); return; }
  if (eqi(cmd, "EDIT") || eqi(cmd, "ED")) { char n[24]; word(line, 1, n, sizeof n); cmdEdit(n); return; }
  if (eqi(cmd, "RUN")) { char n[24]; word(line, 1, n, sizeof n); cmdRun(n); return; }
  if (eqi(cmd, "CAT") || eqi(cmd, "TYPE")) { char n[24]; word(line, 1, n, sizeof n); cmdCat(n); return; }
  if (eqi(cmd, "DEL") || eqi(cmd, "RM")) { char n[24]; word(line, 1, n, sizeof n); cmdDel(n); return; }
  if (eqi(cmd, "VARS")) { cmdVars(); return; }
  if (eqi(cmd, "MEM") || eqi(cmd, "DF")) { cmdMem(); return; }
  if (eqi(cmd, "SCREEN")) { char a[24]; word(line, 1, a, sizeof a); cmdScreen(a); return; }
  if (eqi(cmd, "HELP") || eqi(cmd, "?")) { cmdHelp(); return; }

  _out.print(F("Неизвестная команда: ")); _out.println(cmd);
  _out.println(F("Введите HELP."));
}

void Shell::cmdLs() {
  FileList list;
  int n = _fs.listFiles(list);
  if (n == 0) {
    _out.println(F("Flash пуста. NEW <name> — создать pipeline."));
    return;
  }
  _out.print(F("Pipeline-файлов: ")); _out.println(n);
  for (int i = 0; i < n; i++) {
    int sz = _fs.fileSize(list.names[i]);
    _out.print(F("  ")); _out.print(list.names[i]);
    _out.print(F("  (")); _out.print(sz); _out.println(F(" байт)"));
  }
}

void Shell::cmdNew(const char* name) {
  if (!name || !*name) { _out.println(F("Usage: NEW <name>")); return; }
  if (!validFileName(name)) {
    _out.println(F("Неверное имя: только A-Z a-z 0-9 _ -, до 23 символов"));
    return;
  }
  if (_fs.exists(name)) {
    _out.print(F("Файл уже существует: ")); _out.println(name);
    _out.println(F("Используйте EDIT для редактирования."));
    return;
  }
  _editor.enter(name, true);
}

void Shell::cmdEdit(const char* name) {
  if (!name || !*name) { _out.println(F("Usage: EDIT <name>")); return; }
  if (!validFileName(name)) {
    _out.println(F("Неверное имя: только A-Z a-z 0-9 _ -, до 23 символов"));
    return;
  }
  if (!_fs.exists(name)) {
    _out.print(F("Файл не найден: ")); _out.println(name);
    _out.println(F("Используйте NEW для создания."));
    return;
  }
  _editor.enter(name, false);
}

void Shell::cmdRun(const char* name) {
  if (!name || !*name) { _out.println(F("Usage: RUN <name>")); return; }
  // Сбрасываем результаты прошлых прогонов — иначе при ошибке RUN
  // (нет файла / ошибка парсера) экран RUN продолжал бы показывать
  // устаревшие значения (баг "stale results").
  _hasResults = false;
  _runner.clearResults();
  static char buf[8192];
  int n = _fs.readFile(name, buf, sizeof(buf) - 1);
  if (n == -1) {
    _out.print(F("Файл не найден: ")); _out.println(name);
    return;
  }
  if (n == -2) {
    _out.print(F("Файл слишком большой (max ")); _out.print((int)sizeof(buf) - 1);
    _out.println(F(" байт)")); _out.println(name);
    return;
  }
  buf[n] = '\0';
  _out.print(F("=== RUN: ")); _out.print(name); _out.println(F(" ==="));
  if (!_parser.parse(buf, _parsed)) {
    _out.print(F("ОШИБКА парсера, строка ")); _out.print(_parser.errLine());
    _out.print(F(": ")); _out.println(_parser.err());
    return;
  }
  _vars.clear();
  bool ok = _runner.run(_parsed, _vars);
  _hasResults = true;
  if (ok) {
    _out.print(F("=== OK ("));
    _out.print(_runner.resultCount());
    _out.println(F(" результатов) ==="));
  } else {
    _out.print(F("=== ОШИБКА выполнения, строка "));
    _out.print(_runner.errLine());
    _out.print(F(": ")); _out.println(_runner.err());
    _out.println(F("=== ==="));
  }
  // Если сидим на экране RUN — заставляем его перерисоваться
  // с новыми результатами (или с "No results", если был fail).
  if (_disp) _disp->markDirty();
}

void Shell::cmdCat(const char* name) {
  if (!name || !*name) { _out.println(F("Usage: CAT <name>")); return; }
  static char buf[8192];
  int n = _fs.readFile(name, buf, sizeof(buf) - 1);
  if (n == -1) {
    _out.print(F("Файл не найден: ")); _out.println(name);
    return;
  }
  if (n == -2) {
    _out.print(F("Файл слишком большой для CAT (max ")); _out.print((int)sizeof(buf) - 1);
    _out.println(F(" байт)")); _out.println(name);
    return;
  }
  buf[n] = '\0';
  _out.print(F("=== ")); _out.print(name); _out.print(F(" ("));
  _out.print(n); _out.println(F(" байт) ==="));
  _out.print(buf);
  _out.println(F("=== ==="));
}

void Shell::cmdDel(const char* name) {
  if (!name || !*name) { _out.println(F("Usage: DEL <name>")); return; }
  if (_fs.deleteFile(name)) {
    _out.print(F("Удалён: ")); _out.println(name);
    // HOME хранит кэш списка файлов — обновляем и перерисовываем.
    if (_disp) {
      _disp->refreshFileList();
      _disp->markDirty();
    }
  } else {
    _out.print(F("Не удалось удалить: ")); _out.println(name);
  }
}

void Shell::cmdVars() {
  if (_vars.count == 0) {
    _out.println(F("Нет переменных. Выполните RUN."));
    return;
  }
  _out.print(F("Переменные (")); _out.print(_vars.count); _out.println(F("):"));
  for (int i = 0; i < _vars.count; i++) {
    _out.print(F("  ")); _out.print(_vars.names[i]);
    _out.print(F(" = "));
    if (fabsf(_vars.values[i] - roundf(_vars.values[i])) < 0.005f)
      _out.println((long)roundf(_vars.values[i]));
    else
      _out.println(_vars.values[i], 4);
  }
}

void Shell::cmdMem() {
  _out.print(F("Flash: ")); _out.print(_fs.usedBytes());
  _out.print(F(" / ")); _out.print(_fs.totalBytes()); _out.println(F(" байт"));
  FileList list;
  int n = _fs.listFiles(list);
  _out.print(F("Файлов: ")); _out.println(n);
}

void Shell::cmdScreen(const char* arg) {
  if (!arg || !*arg) {
    _out.println(F("Usage: SCREEN home|edit|run|term|clear"));
    return;
  }
  // дисплей обрабатывает сам — через _disp
  if (_disp) {
    // BoilerDisplay имеет setScreen/setTermMode/clear — вызываем через метод
    // (передаём arg, дисплей разбирает)
    _disp->setScreenByName(arg);
  } else {
    _out.println(F("Дисплей не подключен."));
  }
}

void Shell::cmdHelp() {
  _out.println(F("=== Команды ==="));
  _out.println(F("LS              — список pipeline-файлов"));
  _out.println(F("NEW <name>      — создать pipeline, войти в редактор"));
  _out.println(F("EDIT <name>     — редактировать существующий"));
  _out.println(F("RUN <name>      — выполнить pipeline"));
  _out.println(F("CAT <name>      — показать содержимое"));
  _out.println(F("DEL <name>      — удалить файл"));
  _out.println(F("VARS            — переменные последнего RUN"));
  _out.println(F("MEM             — статистика flash"));
  _out.println(F("SCREEN home|edit|run|term|clear"));
  _out.println(F("HELP            — эта справка"));
  _out.println(F(""));
  _out.println(F("=== Формат pipeline ==="));
  _out.println(F("# комментарий"));
  _out.println(F("var = число | выражение"));
  _out.println(F("print expr \"unit\""));
  _out.println(F("print \"текст\""));
  _out.println(F("Операторы: + - * / ( ) == != < > <= >= ?:"));
  _out.println(F("Функции: sqrt abs min max pow sin cos exp log floor ceil"));
  _out.println(F(""));
  _out.println(F("=== Редактор (в режиме EDIT) ==="));
  _out.println(F(". или .s  — сохранить и выйти"));
  _out.println(F(".q        — выйти без сохранения"));
  _out.println(F(".l        — список строк"));
  _out.println(F(".d N      — удалить строку N"));
}
