/*============================================================================
 *  boiler_expr.cpp  —  реализация вычислителя выражений.
 *==========================================================================*/
#include "boiler_expr.h"
#include <math.h>
#include <string.h>
#include <ctype.h>

//--- VarTable ---
bool VarTable::set(const char* name, float val) {
  if (!name || !name[0]) return false;
  // обновить существующую
  for (int i = 0; i < count; i++) {
    if (strncmp(names[i], name, NAME_LEN) == 0) { values[i] = val; return true; }
  }
  if (count >= MAX_VARS) return false;
  strncpy(names[count], name, NAME_LEN);
  names[count][NAME_LEN] = '\0';
  values[count] = val;
  count++;
  return true;
}

bool VarTable::get(const char* name, float& out) const {
  if (!name || !name[0]) return false;
  for (int i = 0; i < count; i++) {
    if (strncmp(names[i], name, NAME_LEN) == 0) { out = values[i]; return true; }
  }
  return false;
}

//--- ExprEval ---
ExprEval::ExprEval(const char* src, const VarTable& vars)
  : _p(src), _vars(vars), _err(nullptr) {}

void ExprEval::skipSpaces() {
  while (*_p && isspace((unsigned char)*_p)) _p++;
}

bool ExprEval::peek(char c) const {
  return *_p == c;
}

bool ExprEval::peekStr(const char* s) const {
  return strncmp(_p, s, strlen(s)) == 0;
}

bool ExprEval::match(char c) {
  skipSpaces();
  if (*_p == c) { _p++; return true; }
  return false;
}

//--- parseNumber: целое/дробное, опц. экспонента ---
float ExprEval::parseNumber() {
  skipSpaces();
  const char* start = _p;
  if (*_p == '+' || *_p == '-') _p++;   // знак внутри числа (редко)
  while (isdigit((unsigned char)*_p)) _p++;
  if (*_p == '.') { _p++; while (isdigit((unsigned char)*_p)) _p++; }
  if (*_p == 'e' || *_p == 'E') {
    _p++;
    if (*_p == '+' || *_p == '-') _p++;
    while (isdigit((unsigned char)*_p)) _p++;
  }
  if (_p == start) { _err = "ожидалось число"; return 0; }
  return strtof(start, nullptr);
}

//--- parseIdent: переменная или функция ---
float ExprEval::parseIdent() {
  skipSpaces();
  char name[VarTable::NAME_LEN + 1];
  int n = 0;
  if (!(isalpha((unsigned char)*_p) || *_p == '_')) {
    _err = "ожидалось имя"; return 0;
  }
  while ((isalnum((unsigned char)*_p) || *_p == '_') && n < VarTable::NAME_LEN) {
    name[n++] = *_p++;
  }
  name[n] = '\0';
  // пропустить остаток имени, если слишком длинное (ошибка)
  while (isalnum((unsigned char)*_p) || *_p == '_') _p++;

  skipSpaces();
  // вызов функции?
  if (*_p == '(') {
    _p++;   // съели '('
    // 1-й аргумент
    float a = parseExpr();
    if (_err) return 0;
    float b = 0;
    skipSpaces();
    if (*_p == ',') {
      _p++;
      b = parseExpr();
      if (_err) return 0;
    }
    skipSpaces();
    if (*_p != ')') { _err = "ожидалась ')'"; return 0; }
    _p++;
    // диспета функций
    if      (strcmp(name, "sqrt")  == 0) return sqrtf(a);
    else if (strcmp(name, "abs")   == 0) return fabsf(a);
    else if (strcmp(name, "floor") == 0) return floorf(a);
    else if (strcmp(name, "ceil")  == 0) return ceilf(a);
    else if (strcmp(name, "sin")   == 0) return sinf(a);
    else if (strcmp(name, "cos")   == 0) return cosf(a);
    else if (strcmp(name, "tan")   == 0) return tanf(a);
    else if (strcmp(name, "exp")   == 0) return expf(a);
    else if (strcmp(name, "log")   == 0) return logf(a);
    else if (strcmp(name, "min")   == 0) return fminf(a, b);
    else if (strcmp(name, "max")   == 0) return fmaxf(a, b);
    else if (strcmp(name, "pow")   == 0) return powf(a, b);
    else if (strcmp(name, "mod")   == 0) { if (b == 0) { _err = "mod by 0"; return 0; } return fmodf(a, b); }
    else { _err = "неизвестная функция"; return 0; }
  }
  // переменная
  float v;
  if (_vars.get(name, v)) return v;
  _err = "неизвестная переменная";
  return 0;
}

//--- parseFactor: унарный минус/плюс, число, идент, (expr) ---
float ExprEval::parseFactor() {
  skipSpaces();
  if (*_p == '-') { _p++; return -parseFactor(); }
  if (*_p == '+') { _p++; return parseFactor(); }
  if (*_p == '(') {
    _p++;
    float r = parseExpr();
    if (_err) return 0;
    skipSpaces();
    if (*_p != ')') { _err = "ожидалась ')'"; return 0; }
    _p++;
    return r;
  }
  if (isdigit((unsigned char)*_p) || *_p == '.') return parseNumber();
  if (isalpha((unsigned char)*_p) || *_p == '_') return parseIdent();
  _err = "неожиданный символ";
  return 0;
}

//--- parseMulDiv: * / ---
float ExprEval::parseMulDiv() {
  float r = parseFactor();
  if (_err) return 0;
  for (;;) {
    skipSpaces();
    if (*_p == '*') { _p++; r *= parseFactor(); if (_err) return 0; }
    else if (*_p == '/') {
      _p++;
      float d = parseFactor();
      if (_err) return 0;
      if (d == 0) { _err = "деление на 0"; return 0; }
      r /= d;
    }
    else break;
  }
  return r;
}

//--- parseAddSub: + - ---
float ExprEval::parseAddSub() {
  float r = parseMulDiv();
  if (_err) return 0;
  for (;;) {
    skipSpaces();
    if (*_p == '+') { _p++; r += parseMulDiv(); if (_err) return 0; }
    else if (*_p == '-') { _p++; r -= parseMulDiv(); if (_err) return 0; }
    else break;
  }
  return r;
}

//--- parseCompare: == != < > <= >= ---
float ExprEval::parseCompare() {
  float a = parseAddSub();
  if (_err) return 0;
  skipSpaces();
  // двухсимвольные операторы сначала
  if (peekStr("==")) { _p += 2; float b = parseAddSub(); if (_err) return 0; return fabsf(a - b) < 1e-9f ? 1 : 0; }
  if (peekStr("!=")) { _p += 2; float b = parseAddSub(); if (_err) return 0; return fabsf(a - b) >= 1e-9f ? 1 : 0; }
  if (peekStr("<=")) { _p += 2; float b = parseAddSub(); if (_err) return 0; return a <= b ? 1 : 0; }
  if (peekStr(">=")) { _p += 2; float b = parseAddSub(); if (_err) return 0; return a >= b ? 1 : 0; }
  if (peekStr("<"))  { _p += 1; float b = parseAddSub(); if (_err) return 0; return a < b ? 1 : 0; }
  if (peekStr(">"))  { _p += 1; float b = parseAddSub(); if (_err) return 0; return a > b ? 1 : 0; }
  return a;
}

//--- parseTernary: cond ? a : b ---
float ExprEval::parseTernary() {
  float cond = parseCompare();
  if (_err) return 0;
  skipSpaces();
  if (*_p == '?') {
    _p++;
    float a = parseTernary();
    if (_err) return 0;
    skipSpaces();
    if (*_p != ':') { _err = "ожидалось ':'"; return 0; }
    _p++;
    float b = parseTernary();
    if (_err) return 0;
    return cond != 0 ? a : b;
  }
  return cond;
}

float ExprEval::parseExpr() {
  return parseTernary();
}

//--- главный метод ---
float ExprEval::eval() {
  _err = nullptr;
  if (!_p) { _err = "пустое выражение"; return 0; }
  float r = parseExpr();
  if (_err) return 0;
  skipSpaces();
  if (*_p != '\0') { _err = "лишние символы в конце"; return 0; }
  return r;
}
