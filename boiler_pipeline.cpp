/*============================================================================
 *  boiler_pipeline.cpp  —  парсер + исполнитель pipeline.
 *==========================================================================*/
#include "boiler_pipeline.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

//--- утилиты: trim вх/вых пробелов, вернуть указатель на первое непустое ---
static char* trim(char* s) {
  while (*s && isspace((unsigned char)*s)) s++;        // leading
  if (*s == '\0') return s;
  char* end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end)) { *end = '\0'; end--; }
  return s;
}

// проверка, что строка — валидный идентификатор (буква/_ + буквы/цифры/_)
static bool validIdent(const char* s) {
  if (!s || !*s) return false;
  if (!(isalpha((unsigned char)*s) || *s == '_')) return false;
  for (const char* p = s + 1; *p; p++) {
    if (!(isalnum((unsigned char)*p) || *p == '_')) return false;
  }
  return true;
}

//--- PipelineParser ---
bool PipelineParser::parse(const char* text, Pipeline& out) {
  _err = nullptr;
  _errLine = 0;
  out.count = 0;

  // Копируем текст в рабочий буфер (strtok модифицирует)
  static char buf[4096];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  int lineNo = 0;
  char* saveptr = nullptr;
  char* line = strtok_r(buf, "\r\n", &saveptr);
  while (line) {
    lineNo++;
    char work[128];
    strncpy(work, line, sizeof(work) - 1);
    work[sizeof(work) - 1] = '\0';
    char* t = trim(work);
    if (*t == '\0' || *t == '#') { line = strtok_r(nullptr, "\r\n", &saveptr); continue; }

    if (out.count >= Pipeline::MAX_STMTS) {
      _err = "слишком много операторов (max 80)"; _errLine = lineNo; return false;
    }
    Stmt& st = out.stmts[out.count];
    st.lineNo = lineNo;
    memset(&st, 0, sizeof(st));
    st.lineNo = lineNo;

    // print ?
    if (strncmp(t, "print", 5) == 0 && (t[5] == ' ' || t[5] == '\t' || t[5] == '\0')) {
      char* rest = trim(t + 5);
      if (*rest == '"') {
        // print "text" — литерал
        st.type = ST_PRINT_TEXT;
        rest++;   // съели открывающую "
        char* endq = strchr(rest, '"');
        if (!endq) { _err = "незакрытая кавычка"; _errLine = lineNo; return false; }
        size_t len = endq - rest;
        if (len >= sizeof(st.text)) len = sizeof(st.text) - 1;
        memcpy(st.text, rest, len);
        st.text[len] = '\0';
      } else {
        // print expr ["unit"]
        st.type = ST_PRINT_EXPR;
        // ищем опциональную "unit" в конце
        char* q = strchr(rest, '"');
        if (q) {
          // выражение — всё до кавычки
          size_t elen = q - rest;
          if (elen >= sizeof(st.expr)) elen = sizeof(st.expr) - 1;
          memcpy(st.expr, rest, elen);
          st.expr[elen] = '\0';
          // trim хвоста выражения
          char* e = st.expr + strlen(st.expr) - 1;
          while (e >= st.expr && isspace((unsigned char)*e)) { *e = '\0'; e--; }
          // unit
          char* u = q + 1;
          char* uend = strchr(u, '"');
          if (!uend) { _err = "незакрытая кавычка unit"; _errLine = lineNo; return false; }
          size_t ulen = uend - u;
          if (ulen >= sizeof(st.text)) ulen = sizeof(st.text) - 1;
          memcpy(st.text, u, ulen);
          st.text[ulen] = '\0';
        } else {
          // только выражение, без unit
          strncpy(st.expr, rest, sizeof(st.expr) - 1);
          st.expr[sizeof(st.expr) - 1] = '\0';
          st.text[0] = '\0';
        }
        if (st.expr[0] == '\0') { _err = "пустое выражение в print"; _errLine = lineNo; return false; }
      }
      out.count++;
    } else {
      // assign: var = expr
      char* eq = strchr(t, '=');
      if (!eq) { _err = "ожидалось '=' или 'print'"; _errLine = lineNo; return false; }
      // проверим, что это не == (сравнение вне print — ошибка синтаксиса)
      if (eq[1] == '=') { _err = "неожиданный '==' (только в выражениях print)"; _errLine = lineNo; return false; }
      *eq = '\0';
      char* name = trim(t);
      char* rhs  = trim(eq + 1);
      if (!validIdent(name)) { _err = "невалидное имя переменной"; _errLine = lineNo; return false; }
      if (*rhs == '\0') { _err = "пустое выражение после '='"; _errLine = lineNo; return false; }
      st.type = ST_ASSIGN;
      strncpy(st.varName, name, sizeof(st.varName) - 1);
      st.varName[sizeof(st.varName) - 1] = '\0';
      strncpy(st.expr, rhs, sizeof(st.expr) - 1);
      st.expr[sizeof(st.expr) - 1] = '\0';
      out.count++;
    }
    line = strtok_r(nullptr, "\r\n", &saveptr);
  }
  return true;
}

//--- PipelineRunner ---
PipelineRunner::PipelineRunner(Print& out) : _out(out) {}

static void fmtNum(float v, char* buf, size_t n) {
  if (fabsf(v - roundf(v)) < 0.005f) snprintf(buf, n, "%ld", (long)roundf(v));
  else                                snprintf(buf, n, "%.2f", v);
}

bool PipelineRunner::run(const Pipeline& p, VarTable& vars) {
  _err = nullptr;
  _errLine = 0;
  _rc = 0;

  for (int i = 0; i < p.count; i++) {
    const Stmt& st = p.stmts[i];
    if (st.type == ST_ASSIGN) {
      ExprEval ev(st.expr, vars);
      float v = ev.eval();
      if (!ev.ok()) {
        _err = ev.err();
        _errLine = st.lineNo;
        _out.print(F("ERR line ")); _out.print(st.lineNo);
        _out.print(F(": ")); _out.println(_err);
        return false;
      }
      vars.set(st.varName, v);
    } else if (st.type == ST_PRINT_EXPR) {
      ExprEval ev(st.expr, vars);
      float v = ev.eval();
      if (!ev.ok()) {
        _err = ev.err();
        _errLine = st.lineNo;
        _out.print(F("ERR line ")); _out.print(st.lineNo);
        _out.print(F(": ")); _out.println(_err);
        return false;
      }
      // вывод в Print: "expr = value unit"
      _out.print(st.expr);
      _out.print(F(" = "));
      char b[16]; fmtNum(v, b, sizeof b);
      _out.print(b);
      if (st.text[0]) { _out.print(st.text); }
      _out.println();
      // в результаты для дисплея
      if (_rc < MAX_RESULTS) {
        ResultRow& r = _results[_rc++];
        strncpy(r.label, st.expr, sizeof(r.label) - 1);
        r.label[sizeof(r.label) - 1] = '\0';
        r.value = v;
        strncpy(r.unit, st.text, sizeof(r.unit) - 1);
        r.unit[sizeof(r.unit) - 1] = '\0';
        r.isNote = false;
      }
    } else if (st.type == ST_PRINT_TEXT) {
      _out.println(st.text);
      if (_rc < MAX_RESULTS) {
        ResultRow& r = _results[_rc++];
        strncpy(r.label, st.text, sizeof(r.label) - 1);
        r.label[sizeof(r.label) - 1] = '\0';
        r.value = 0;
        r.unit[0] = '\0';
        r.isNote = true;
      }
    }
  }
  return true;
}
