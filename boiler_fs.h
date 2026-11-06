/*============================================================================
 *  boiler_fs.h  —  обёртка над LittleFS для хранения pipeline-файлов.
 *
 *  LittleFS (BSD-licensed, GPL-совместима) входит в ядро Arduino-Pico
 *  (earlephilhower). Flash под LittleFS резервируется при прошивке
 *  (Tools → Flash Size → выберите размер FS, напр. 1MB).
 *
 *  Pipeline-файлы хранятся в корне FS без расширения:
 *    /vodogreyny, /parovoy, /kombi — имена = то, что вводит пользователь.
 *
 *  Обёртка изолирует остальной код от деталей File API и даёт простые
 *  операции: list / exists / read / write / delete.
 *==========================================================================*/
#pragma once
#include <Arduino.h>

struct FileList {
  static const int MAX_FILES = 32;
  static const int NAME_LEN  = 24;
  char names[MAX_FILES][NAME_LEN + 1];
  int  count = 0;
};

class BoilerFS {
public:
  // Инициализация LittleFS. true — успех. При первом запуске FS может быть
  // пустой — это нормально, файлы создаются через редактор.
  bool init();

  // Список pipeline-файлов. Возвращает число найденных.
  int  listFiles(FileList& out);

  bool exists(const char* name);

  // Прочитать файл целиком в buf. Возвращает длину или -1 если нет файла
  // или не влезло в maxlen.
  int  readFile(const char* name, char* buf, int maxlen);

  // Записать text в файл (перезапись). true — успех.
  bool writeFile(const char* name, const char* text);

  bool deleteFile(const char* name);

  // Размер файла в байтах, или -1.
  int  fileSize(const char* name);

  // Сколько свободно (грубо, в байтах) — для статистики.
  int  totalBytes();
  int  usedBytes();
};
