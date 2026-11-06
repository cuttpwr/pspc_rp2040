/*============================================================================
 *  boiler_tty.h  —  "Tee" Print: пишет в Serial И в кольцевый буфер
 *  последних строк для OLED-экрана TERMINAL.
 *
 *  Зачем: в Term Mode на OLED нужно видеть, что выводит CLI (ответы на
 *  команды, результаты расчёта). Обычный Serial.print уходит только в USB CDC.
 *  BoilerTTY перехватывает весь вывод через интерфейс Print и хранит
 *  последние N строк в кольцевом буфере. Display рисует их на экране TERMINAL.
 *
 *  Буфер хранит сырые UTF-8 байты (кириллицу, греческие β/η/Δ и т.д.).
 *  Транслитерация в ASCII происходит при рендеринге в boiler_display.cpp
 *  (drawTerminal), потому что шрифт Adafruit_GFX — ASCII-only.
 *
 *  Текущая вводимая строка (что пользователь набирает на клавиатуре)
 *  хранится отдельно — её выставляет главный цикл через setInputLine().
 *==========================================================================*/
#pragma once
#include <Arduino.h>

class BoilerTTY : public Print {
public:
  void begin();

  // --- интерфейс Print (вызывается из CLI через out().print/println) ---
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* buf, size_t len) override;

  // --- текущая вводимая строка ---
  void setInputLine(const char* s);
  void clearInputLine();

  // Зашить готовую строку в кольцо scrollback БЕЗ эха в Serial.
  // Нужно, когда главный цикл нажал Enter: он уже аккумулировал input
  // через setInputLine, и теперь хочет увидеть эту строку в истории
  // терминала (выше текущей вводимой). Эха в Serial нет, потому что
  // для Serial-источника Serial Monitor уже показал ввод локально,
  // а для клавиатуры эхо делалось побайтово через Serial.write.
  void pushLine(const char* s);

  // Сбросить аккумулируемую строку _curLine, не выводя её в scrollback.
  // Используется главным циклом перед печатью промпта "> ": иначе промпт
  // копится в _curLine и потом склеивается с первой строкой вывода шелла
  // ("> === RUN: ..." вместо отдельной строки "> " и "=== RUN: ...").
  void dropCurrentLine();

  // --- доступ для OLED-экрана TERMINAL ---
  // idx=0 — самая старая из видимых, idx=lineCount()-1 — самая свежая.
  int  lineCount() const;
  void getLine(int idx, char* buf, size_t n) const;
  void getInputLine(char* buf, size_t n) const;

  // очистка буфера (для команды SCREEN clear)
  void clear();

private:
  static const int MAX_LINES = 16;   // храним последние 16 строк
  static const int LINE_LEN  = 96;   // байт на строку (длинные формулы не режутся)

  char _lines[MAX_LINES][LINE_LEN + 1];
  int  _head  = 0;     // куда пишем следующую строку
  int  _count = 0;     // сколько строк валидно (0..MAX_LINES)

  char _curLine[LINE_LEN + 1];   // аккумулируется до '\n'
  int  _curLen = 0;

  char _inputLine[LINE_LEN + 1]; // что пользователь набирает

  void _pushLine();              // защёлкнуть _curLine в кольцо, сбросить _curLen
};
