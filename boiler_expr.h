/*============================================================================
 *  boiler_expr.h  —  вычислитель арифметических выражений (recursive descent).
 *
 *  Грамматика (приоритет от низкого к высокому):
 *    expr     → ternary
 *    ternary  → compare ('?' ternary ':' ternary)?
 *    compare  → addsub (('==' | '!=' | '<' | '>' | '<=' | '>=') addsub)?
 *    addsub   → muldiv (('+' | '-') muldiv)*
 *    muldiv   → factor (('*' | '/') factor)*
 *    factor   → ('-' | '+') factor | number | ident | '(' expr ')' | func '(' args ')'
 *
 *  Функции: sqrt abs min max pow sin cos tan exp log floor ceil
 *  min/max/pow принимают 2 аргумента через запятую: pow(2, 3), max(a, b).
 *
 *  Переменные: буквенно-цифровые + '_', начинаются с буквы/'_'. До 16 символов.
 *
 *  Использование:
 *    VarTable vars;
 *    vars.set("Qov", 34);
 *    ExprEval ev("Q = Qov * 2", vars);   // src — "Qov * 2"
 *    float r = ev.eval();
 *    if (!ev.ok()) { Serial.println(ev.err()); }
 *
 *  Чистый C++ (нет Arduino-зависимостей кроме fabsf/sqrtf из <math.h>),
 *  поэтому тестируется на хосте.
 *==========================================================================*/
#pragma once

struct VarTable {
  static const int MAX_VARS = 96;
  static const int NAME_LEN = 16;

  char  names[MAX_VARS][NAME_LEN + 1];
  float values[MAX_VARS];
  int   count = 0;

  void clear() { count = 0; }
  // добавить или обновить. false — переполнение.
  bool set(const char* name, float val);
  // получить. false — нет такой переменной.
  bool get(const char* name, float& out) const;
};

class ExprEval {
public:
  ExprEval(const char* src, const VarTable& vars);
  // Вычислить. При ошибке возвращает 0 и err() непустой.
  float eval();
  bool  ok() const { return _err == nullptr; }
  const char* err() const { return _err ? _err : ""; }

private:
  const char* _p;
  const VarTable& _vars;
  const char* _err;

  void skipSpaces();
  bool match(char c);              // съесть символ, если совпал
  bool peek(char c) const;         // проверить, не съедая
  bool peekStr(const char* s) const;

  float parseExpr();
  float parseTernary();
  float parseCompare();
  float parseAddSub();
  float parseMulDiv();
  float parseFactor();
  float parseNumber();
  float parseIdent();              // переменная или вызов функции
};
