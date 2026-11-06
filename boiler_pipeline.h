/*============================================================================
 *  boiler_pipeline.h  —  формат pipeline и его исполнитель.
 *
 *  PIPELINE — текстовый файл в LittleFS. Формат построчный:
 *
 *    # comment                          — комментарий, игнорируется
 *    var = number                       — статическое данное (константа)
 *    var = expression                   — формула (RHS вычисляется,写到 var)
 *    print expr                         — вывод: "expr = value"
 *    print expr "unit"                  — вывод: "expr = value unit"
 *    print "text"                       — вывод литерального текста
 *
 *  Пример:
 *    # vodogreyny v15
 *    Qov = 34
 *    Qgv = 10
 *    t1 = 110
 *    t2 = 50
 *    eta = 0.98
 *    Q = Qov + Qgv
 *    G = Q * 1000 / (4.19 * (t1 - t2))
 *    print Q " МВт"
 *    print G " т/ч"
 *    print eta
 *
 *  Исполнитель выполняет операторы по порядку, поддерживая таблицу
 *  переменных VarTable. Ошибка в любом операторе останавливает выполнение.
 *==========================================================================*/
#pragma once
#include <Arduino.h>
#include "boiler_expr.h"

enum StmtType : uint8_t {
  ST_ASSIGN,     // var = expr
  ST_PRINT_EXPR, // print expr ["unit"]
  ST_PRINT_TEXT  // "literal text"
};

struct Stmt {
  StmtType type;
  char varName[VarTable::NAME_LEN + 1];  // LHS для ASSIGN
  char expr[96];                          // выражение для ASSIGN / PRINT_EXPR
  char text[48];                          // литерал для PRINT_TEXT / unit для PRINT_EXPR
  int  lineNo;                            // номер строки источника (для ошибок)
};

struct Pipeline {
  static const int MAX_STMTS = 80;
  Stmt stmts[MAX_STMTS];
  int  count = 0;
};

// Строка результата для дисплея
struct ResultRow {
  char  label[24];
  float value;
  char  unit[12];
  bool  isNote;     // true → только текст (label), value/unit не важны
};

class PipelineParser {
public:
  // Разобрать текст в pipeline. false — синтаксическая ошибка.
  bool parse(const char* text, Pipeline& out);
  const char* err() const { return _err; }
  int  errLine() const { return _errLine; }
private:
  const char* _err = nullptr;
  int  _errLine = 0;
};

class PipelineRunner {
public:
  PipelineRunner(Print& out);
  // Выполнить pipeline. false — ошибка выполнения (см. err/errLine).
  bool run(const Pipeline& p, VarTable& vars);
  const char* err() const { return _err; }
  int  errLine() const { return _errLine; }

  // Результаты последнего прогона — для дисплея
  static const int MAX_RESULTS = 32;
  int  resultCount() const { return _rc; }
  const ResultRow& result(int i) const { return _results[i]; }
  void clearResults() { _rc = 0; }
private:
  Print& _out;
  ResultRow _results[MAX_RESULTS];
  int  _rc = 0;
  const char* _err = nullptr;
  int  _errLine = 0;
};
