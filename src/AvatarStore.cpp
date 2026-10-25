#include "AvatarStore.h"

#include <LittleFS.h>
#include <algorithm>
#include <dirent.h>
#include <vector>
#include <mbedtls/base64.h>

#include "UsbSerialProtocol.h"

namespace AvatarStore {
namespace {

constexpr const char* kRoot = "/av";
constexpr const char* kActivePath = "/av/active";
constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr size_t kMaxChunkBytes = 4096;
constexpr size_t kSpareBytes = 64 * 1024;  // never fill the filesystem to the last block
constexpr const char* kMount = "/littlefs";  // LittleFS.begin()'s default base path
constexpr uint16_t kAllBaseFrames = 0xFFFF;

// Every LittleFS path lookup walks from the root, whose directory is crowded with the
// built-in images, so each costs tens of milliseconds, and so does every File that
// openNextFile() opens. Read folders by name only (readdir), and cache what swipes,
// redraws and listings ask for until a write changes it.
std::vector<String> cycleCache;
bool cycleCached = false;
String activeCache;
bool activeCached = false;
size_t usedCache = 0;
bool usedCached = false;
bool usedExact = false;   // false once an upload has only estimated its growth

void invalidate() {
  cycleCached = false;
  activeCached = false;
}

// Calls fn(name, isDirectory) for each entry of a folder; false if it can't be opened.
template <typename F>
bool eachEntry(const String& dir, F fn) {
  DIR* folder = opendir((String(kMount) + dir).c_str());
  if (folder == nullptr) {
    return false;
  }
  for (dirent* entry = readdir(folder); entry != nullptr; entry = readdir(folder)) {
    fn(entry->d_name, entry->d_type == DT_DIR);
  }
  closedir(folder);
  return true;
}

// Bit m*4+e for base_m{m}_e{e}.jpg, 0 for any other name.
uint16_t baseFrameBit(const char* name) {
  if (strlen(name) != 14 || strncmp(name, "base_m", 6) != 0 || strncmp(name + 7, "_e", 2) != 0 ||
      strcmp(name + 10, ".jpg") != 0 || name[6] < '0' || name[6] > '3' || name[9] < '0' || name[9] > '3') {
    return 0;
  }
  return 1u << ((name[6] - '0') * 4 + (name[9] - '0'));
}

// Both totalBytes() and usedBytes() walk the whole filesystem (about 0.6 s each when full).
size_t totalBytes() {
  static size_t total = LittleFS.totalBytes();  // the partition size never changes
  return total;
}

size_t usedBytes() {
  if (!usedCached) {
    usedCache = LittleFS.usedBytes();
    usedCached = true;
    usedExact = true;
  }
  return usedCache;
}

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
  uint16_t frames = 0;
  eachEntry(dirOf(id), [&](const char* name, bool) { frames |= baseFrameBit(name); });
  return frames == kAllBaseFrames;
}

String loadActive() {
  if (activeCached) {
    return activeCache;
  }
  File file = LittleFS.open(kActivePath, "r");
  String id = file ? file.readString() : String();
  if (file) file.close();
  id.trim();
  activeCache = complete(id.c_str()) ? id : String();
  activeCached = true;
  return activeCache;
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
    if (totalBytes() - usedBytes() < total + kSpareBytes) {
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
  invalidate();  // the set may have just become complete, or lost a frame
  // Count the new file in whole 4 KB blocks without subtracting a replaced copy: an
  // overestimate is safe for the space check, and the next listing measures it again.
  usedCache += (size + 4095) / 4096 * 4096;
  usedExact = false;
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
  activeCache = id;
  activeCached = true;
  return true;
}

namespace {

// The built-in face ("") followed by the complete sets sorted by id.
const std::vector<String>& cycle() {
  if (cycleCached) {
    return cycleCache;
  }
  std::vector<String> ids{String()};
  eachEntry(kRoot, [&](const char* name, bool folder) {
    if (folder && complete(name)) ids.push_back(name);
  });
  std::sort(ids.begin() + 1, ids.end(), [](const String& a, const String& b) { return strcmp(a.c_str(), b.c_str()) < 0; });
  cycleCache = ids;
  cycleCached = true;
  return cycleCache;
}

size_t indexIn(const std::vector<String>& ids, const String& current) {
  for (size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] == current) return i;
  }
  return 0;
}

}  // namespace

String adjacent(const String& current, int step) {
  const std::vector<String>& ids = cycle();
  const int count = static_cast<int>(ids.size());
  return ids[((static_cast<int>(indexIn(ids, current)) + step) % count + count) % count];
}

void position(const String& current, uint8_t& index, uint8_t& count) {
  const std::vector<String>& ids = cycle();
  count = static_cast<uint8_t>(min<size_t>(ids.size(), 255));
  index = static_cast<uint8_t>(min<size_t>(indexIn(ids, current), 254));
}

bool remove(const char* id, String& error) {
  if (!validId(id)) {
    error = "bad_id";
    return false;
  }
  const String dir = dirOf(id);
  std::vector<String> names;
  if (!eachEntry(dir, [&](const char* name, bool folder) {
        if (!folder) names.push_back(dir + "/" + name);
      })) {
    error = "not_found";
    return false;
  }
  for (const String& name : names) {
    LittleFS.remove(name);
  }
  LittleFS.rmdir(dir);
  invalidate();
  usedCached = false;
  if (loadActive().isEmpty()) {
    String ignored;
    select("", ignored);
  }
  return true;
}

void writeList(JsonObject out) {
  out["active"] = loadActive();
  out["fsTotal"] = totalBytes();
  if (!usedExact) usedCached = false;  // measure again after uploads, which only estimate
  out["fsUsed"] = usedBytes();
  JsonArray sets = out["avatars"].to<JsonArray>();
  eachEntry(kRoot, [&](const char* id, bool folder) {
    if (!folder || !validId(id)) {
      return;
    }
    uint8_t files = 0;
    uint16_t frames = 0;
    bool happy = false;
    bool touch = false;
    eachEntry(dirOf(id), [&](const char* name, bool) {
      if (String(name).endsWith(".part")) {
        return;
      }
      ++files;
      frames |= baseFrameBit(name);
      happy = happy || strcmp(name, "happy.jpg") == 0;
      touch = touch || strncmp(name, "giggle_", 7) == 0 || strncmp(name, "laugh_", 6) == 0;
    });
    JsonObject item = sets.add<JsonObject>();
    item["id"] = id;
    item["complete"] = frames == kAllBaseFrames;
    item["files"] = files;
    item["happy"] = happy;
    item["touch"] = touch;
  });
}

}  // namespace AvatarStore
