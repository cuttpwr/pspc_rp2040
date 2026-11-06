/*============================================================================
 *  boiler_fs.cpp  —  реализация обёртки LittleFS.
 *==========================================================================*/
#include "boiler_fs.h"
#include <LittleFS.h>
#include <string.h>

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
  if (name[0] == '/') {
    strncpy(path, name, sizeof(path) - 1);
  } else {
    path[0] = '/';
    strncpy(path + 1, name, sizeof(path) - 2);
  }
  path[sizeof(path) - 1] = '\0';
  return LittleFS.exists(path);
}

int BoilerFS::readFile(const char* name, char* buf, int maxlen) {
  if (!name || !*name || !buf || maxlen <= 0) return -1;
  char path[32];
  path[0] = '/';
  strncpy(path + 1, name, sizeof(path) - 2);
  path[sizeof(path) - 1] = '\0';

  File f = LittleFS.open(path, "r");
  if (!f) return -1;
  int sz = f.size();
  if (sz >= maxlen) { f.close(); return -1; }
  int n = f.readBytes(buf, sz);
  buf[n] = '\0';
  f.close();
  return n;
}

bool BoilerFS::writeFile(const char* name, const char* text) {
  if (!name || !*name || !text) return false;
  char path[32];
  path[0] = '/';
  strncpy(path + 1, name, sizeof(path) - 2);
  path[sizeof(path) - 1] = '\0';

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
  path[0] = '/';
  strncpy(path + 1, name, sizeof(path) - 2);
  path[sizeof(path) - 1] = '\0';
  return LittleFS.remove(path);
}

int BoilerFS::fileSize(const char* name) {
  if (!name || !*name) return -1;
  char path[32];
  path[0] = '/';
  strncpy(path + 1, name, sizeof(path) - 2);
  path[sizeof(path) - 1] = '\0';
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
