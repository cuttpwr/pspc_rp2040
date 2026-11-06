/*============================================================================
 *  boiler_tty.cpp  —  реализация Tee-Print
 *  Каждый байт вывода идёт в Serial (USB CDC) И аккумулируется в _curLine.
 *  На '\n' строка защёлкивается в кольцевой буфер _lines[].
 *==========================================================================*/
#include "boiler_tty.h"
#include <string.h>

void BoilerTTY::begin() {
  _head = 0;
  _count = 0;
  _curLen = 0;
  _curLine[0] = '\0';
  _inputLine[0] = '\0';
}

size_t BoilerTTY::write(uint8_t b) {
  Serial.write(b);   // всегда в USB CDC
  if (b == '\n') {
    _pushLine();
  } else if (b != '\r') {
    if (_curLen < LINE_LEN) {
      _curLine[_curLen++] = (char)b;
      _curLine[_curLen] = '\0';
    }
    // если превысили LINE_LEN — просто копим дальше без записи (трейшим хвост)
  }
  return 1;
}

size_t BoilerTTY::write(const uint8_t* buf, size_t len) {
  // Сразу целиком в Serial (эффективнее, чем побайтово)
  Serial.write(buf, len);
  // И побайтово в аккумулятор строки
  for (size_t i = 0; i < len; i++) {
    uint8_t b = buf[i];
    if (b == '\n') {
      _pushLine();
    } else if (b != '\r') {
      if (_curLen < LINE_LEN) {
        _curLine[_curLen++] = (char)b;
        _curLine[_curLen] = '\0';
      }
    }
  }
  return len;
}

void BoilerTTY::_pushLine() {
  // защёлкнуть _curLine в кольцо
  strncpy(_lines[_head], _curLine, LINE_LEN);
  _lines[_head][LINE_LEN] = '\0';
  _head = (_head + 1) % MAX_LINES;
  if (_count < MAX_LINES) _count++;
  _curLen = 0;
  _curLine[0] = '\0';
}

void BoilerTTY::setInputLine(const char* s) {
  if (!s) { _inputLine[0] = '\0'; return; }
  strncpy(_inputLine, s, LINE_LEN);
  _inputLine[LINE_LEN] = '\0';
}

void BoilerTTY::clearInputLine() {
  _inputLine[0] = '\0';
}

void BoilerTTY::pushLine(const char* s) {
  if (!s) return;
  strncpy(_lines[_head], s, LINE_LEN);
  _lines[_head][LINE_LEN] = '\0';
  _head = (_head + 1) % MAX_LINES;
  if (_count < MAX_LINES) _count++;
}

void BoilerTTY::dropCurrentLine() {
  _curLen = 0;
  _curLine[0] = '\0';
}

int BoilerTTY::lineCount() const {
  return _count;
}

void BoilerTTY::getLine(int idx, char* buf, size_t n) const {
  if (idx < 0 || idx >= _count || n == 0) { if (n > 0) buf[0] = '\0'; return; }
  // idx=0 — самая старая. В кольце: старая = (_head - _count + idx) mod MAX_LINES
  int src = (_head - _count + idx);
  while (src < 0) src += MAX_LINES;
  src %= MAX_LINES;
  strncpy(buf, _lines[src], n - 1);
  buf[n - 1] = '\0';
}

void BoilerTTY::getInputLine(char* buf, size_t n) const {
  strncpy(buf, _inputLine, n - 1);
  buf[n - 1] = '\0';
}

void BoilerTTY::clear() {
  _head = 0;
  _count = 0;
  _curLen = 0;
  _curLine[0] = '\0';
  _inputLine[0] = '\0';   // иначе после SCREEN clear ввод оставался на OLED
}
