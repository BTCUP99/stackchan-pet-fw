#include "AvatarStore.h"

#include <LittleFS.h>
#include <mbedtls/base64.h>

#include "UsbSerialProtocol.h"

namespace AvatarStore {
namespace {

constexpr const char* kRoot = "/av";
constexpr const char* kActivePath = "/av/active";
constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr size_t kMaxChunkBytes = 4096;
constexpr size_t kSpareBytes = 64 * 1024;  // never fill the filesystem to the last block

String dirOf(const char* id) {
  return String(kRoot) + "/" + id;
}

bool validFileName(const char* name) {
  if (strcmp(name, "happy.jpg") == 0 || strcmp(name, "meta.json") == 0) {
    return true;
  }
  // Touch reactions: giggle_1/2.jpg, laugh_1/2.jpg
  if ((strncmp(name, "giggle_", 7) == 0 && (name[7] == '1' || name[7] == '2') && strcmp(name + 8, ".jpg") == 0) ||
      (strncmp(name, "laugh_", 6) == 0 && (name[6] == '1' || name[6] == '2') && strcmp(name + 7, ".jpg") == 0)) {
    return true;
  }
  return strlen(name) == 14 && strncmp(name, "base_m", 6) == 0 && name[6] >= '0' && name[6] <= '3' &&
         strncmp(name + 7, "_e", 2) == 0 && name[9] >= '0' && name[9] <= '3' && strcmp(name + 10, ".jpg") == 0;
}

uint32_t fileCrc(const String& path, size_t& size) {
  File file = LittleFS.open(path, "r");
  size = 0;
  if (!file) {
    return 0;
  }
  uint8_t buffer[512];
  uint32_t crc = 0xFFFFFFFFUL;
  while (file.available()) {
    const size_t n = file.read(buffer, sizeof(buffer));
    if (n == 0) break;
    crc = UsbSerialProtocol::crc32Update(crc, buffer, n);
    size += n;
  }
  file.close();
  return ~crc;
}

}  // namespace

bool validId(const char* id) {
  const size_t length = id != nullptr ? strlen(id) : 0;
  if (length == 0 || length > kMaxIdLength) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
      return false;
    }
  }
  return true;
}

bool complete(const char* id) {
  if (!validId(id)) {
    return false;
  }
  const String dir = dirOf(id);
  char name[16];
  for (uint8_t mouth = 0; mouth < 4; ++mouth) {
    for (uint8_t eye = 0; eye < 4; ++eye) {
      snprintf(name, sizeof(name), "/base_m%u_e%u.jpg", mouth, eye);
      if (!LittleFS.exists(dir + name)) {
        return false;
      }
    }
  }
  return true;
}

String loadActive() {
  File file = LittleFS.open(kActivePath, "r");
  if (!file) {
    return String();
  }
  String id = file.readString();
  file.close();
  id.trim();
  return complete(id.c_str()) ? id : String();
}

bool put(JsonVariantConst request, String& error) {
  const char* id = request["id"] | "";
  const char* name = request["file"] | "";
  const char* data = request["data"] | "";
  const uint32_t offset = request["offset"] | 0UL;
  if (!validId(id)) {
    error = "bad_id";
    return false;
  }
  if (!validFileName(name)) {
    error = "bad_file";
    return false;
  }

  static uint8_t chunk[kMaxChunkBytes];  // off the loop task's stack
  size_t length = 0;
  if (mbedtls_base64_decode(chunk, sizeof(chunk), &length, reinterpret_cast<const unsigned char*>(data),
                            strlen(data)) != 0) {
    error = "bad_data";
    return false;
  }
  const String dir = dirOf(id);
  const String temp = dir + "/" + name + ".part";

  if (offset == 0) {
    const uint32_t total = request["size"] | 0UL;
    if (total == 0 || total > kMaxFileBytes) {
      error = "bad_size";
      return false;
    }
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < total + kSpareBytes) {
      error = "no_space";
      return false;
    }
    if (!LittleFS.exists(kRoot)) LittleFS.mkdir(kRoot);
    if (!LittleFS.exists(dir)) LittleFS.mkdir(dir);
    LittleFS.remove(temp);
  }

  File file = LittleFS.open(temp, offset == 0 ? "w" : "a");
  if (!file) {
    error = "open_failed";
    return false;
  }
  if (file.size() != offset) {
    file.close();
    error = "bad_offset";
    return false;
  }
  if (offset + length > kMaxFileBytes || file.write(chunk, length) != length) {
    file.close();
    LittleFS.remove(temp);
    error = "write_failed";
    return false;
  }
  file.close();

  if (!(request["final"] | false)) {
    return true;
  }
  size_t size = 0;
  const uint32_t crc = fileCrc(temp, size);
  if (size != (request["size"] | 0UL) || crc != (request["crc"] | 0UL)) {
    LittleFS.remove(temp);
    error = "verify_failed";
    return false;
  }
  const String target = dir + "/" + name;
  LittleFS.remove(target);
  if (!LittleFS.rename(temp, target)) {
    error = "rename_failed";
    return false;
  }
  return true;
}

bool select(const char* id, String& error) {
  if (id[0] != '\0' && !complete(id)) {
    error = "incomplete";
    return false;
  }
  if (!LittleFS.exists(kRoot)) LittleFS.mkdir(kRoot);
  File file = LittleFS.open(kActivePath, "w");
  if (!file) {
    error = "write_failed";
    return false;
  }
  file.print(id);
  file.close();
  return true;
}

bool remove(const char* id, String& error) {
  if (!validId(id)) {
    error = "bad_id";
    return false;
  }
  const String dir = dirOf(id);
  File folder = LittleFS.open(dir);
  if (!folder || !folder.isDirectory()) {
    error = "not_found";
    return false;
  }
  String names[24];
  uint8_t count = 0;
  for (File entry = folder.openNextFile(); entry && count < 24; entry = folder.openNextFile()) {
    names[count++] = dir + "/" + entry.name();
    entry.close();
  }
  folder.close();
  for (uint8_t i = 0; i < count; ++i) {
    LittleFS.remove(names[i]);
  }
  LittleFS.rmdir(dir);
  if (loadActive().isEmpty()) {
    String ignored;
    select("", ignored);
  }
  return true;
}

void writeList(JsonObject out) {
  out["active"] = loadActive();
  out["fsTotal"] = LittleFS.totalBytes();
  out["fsUsed"] = LittleFS.usedBytes();
  JsonArray sets = out["avatars"].to<JsonArray>();
  File root = LittleFS.open(kRoot);
  if (!root || !root.isDirectory()) {
    return;
  }
  for (File entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }
    const String id = entry.name();
    JsonObject item = sets.add<JsonObject>();
    item["id"] = id;
    item["complete"] = complete(id.c_str());
    uint32_t bytes = 0;
    uint8_t files = 0;
    bool happy = false;
    bool touch = false;
    for (File child = entry.openNextFile(); child; child = entry.openNextFile()) {
      const String name = child.name();
      if (!name.endsWith(".part")) {
        bytes += child.size();
        ++files;
        happy = happy || name == "happy.jpg";
        touch = touch || name.startsWith("giggle_") || name.startsWith("laugh_");
      }
      child.close();
    }
    item["files"] = files;
    item["bytes"] = bytes;
    item["happy"] = happy;
    item["touch"] = touch;
    entry.close();
  }
  root.close();
}

}  // namespace AvatarStore
