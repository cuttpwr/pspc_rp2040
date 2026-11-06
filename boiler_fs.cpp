/*============================================================================
 *  boiler_fs.cpp  —  реализация обёртки LittleFS.
 *==========================================================================*/
#include "boiler_fs.h"
#include <LittleFS.h>
#include <string.h>

// Нормализовать имя файла в путь LittleFS: "/<name>" без повторов '/'.
// Раньше 4 из 5 методов слепо prepending '/', что ломало вызовы с ведущим '/'
// (получалось "//name"). Теперь все методы используют один хелпер.
static void makePath(const char* name, char* out, size_t n) {
  if (!name || !*name) { if (n) out[0] = '\0'; return; }
  size_t i = 0;
  if (name[0] != '/' && i + 1 < n) out[i++] = '/';
  for (size_t k = 0; name[k] && i + 1 < n; k++) {
    // не допускаем "//" в середине
    if (name[k] == '/' && i > 0 && out[i - 1] == '/') continue;
    out[i++] = name[k];
  }
  out[i] = '\0';
}

bool BoilerFS::init() {
  // true = format при неудаче begin (первый запуск / повреждена FS)
  return LittleFS.begin(true);
}

int BoilerFS::listFiles(FileList& out) {
  out.count = 0;
  File root = LittleFS.open("/");
  if (!root) return 0;
  File f = root.openNextFile();
  while (f && out.count < FileList::MAX_FILES) {
    const char* nm = f.name();
    // LittleFS может возвращать имя с ведущим '/' — убираем
    if (nm && nm[0] == '/') nm++;
    if (nm && *nm) {
      strncpy(out.names[out.count], nm, FileList::NAME_LEN);
      out.names[out.count][FileList::NAME_LEN] = '\0';
      out.count++;
    }
    f = root.openNextFile();
  }
  return out.count;
}

bool BoilerFS::exists(const char* name) {
  if (!name || !*name) return false;
  char path[32];
  makePath(name, path, sizeof(path));
  return LittleFS.exists(path);
}

int BoilerFS::readFile(const char* name, char* buf, int maxlen) {
  if (!name || !*name || !buf || maxlen <= 0) return -1;
  char path[32];
  makePath(name, path, sizeof(path));

  File f = LittleFS.open(path, "r");
  if (!f) return -1;
  int sz = f.size();
  if (sz >= maxlen) { f.close(); return -2; }   // -2 = слишком большой (≠ -1 "не найден")
  int n = f.readBytes(buf, sz);
  buf[n] = '\0';
  f.close();
  return n;
}

bool BoilerFS::writeFile(const char* name, const char* text) {
  if (!name || !*name || !text) return false;
  char path[32];
  makePath(name, path, sizeof(path));

  File f = LittleFS.open(path, "w");
  if (!f) return false;
  size_t len = strlen(text);
  size_t w = f.write((const uint8_t*)text, len);
  f.close();
  return w == len;
}

bool BoilerFS::deleteFile(const char* name) {
  if (!name || !*name) return false;
  char path[32];
  makePath(name, path, sizeof(path));
  return LittleFS.remove(path);
}

int BoilerFS::fileSize(const char* name) {
  if (!name || !*name) return -1;
  char path[32];
  makePath(name, path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) return -1;
  int sz = f.size();
  f.close();
  return sz;
}

int BoilerFS::totalBytes() {
  // LittleFS на Arduino-Pico даёт totalBytes()/usedBytes() через FSInfo
  return (int)LittleFS.totalBytes();
}

int BoilerFS::usedBytes() {
  return (int)LittleFS.usedBytes();
}
