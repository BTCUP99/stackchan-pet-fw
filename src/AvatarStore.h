#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Avatar sets uploaded by the Mac over USB. Each set lives in /av/<id>/ and holds the
// 16 base frames, an optional happy frame and a small meta.json; images it lacks fall
// back to the built-in ones at the filesystem root.
namespace AvatarStore {

constexpr size_t kMaxIdLength = 20;  // keeps "/av/<id>/base_m0_e0.jpg" inside the face path cache

bool validId(const char* id);
// Active set id from /av/active; empty when the built-in face is used or the set is incomplete.
String loadActive();
bool complete(const char* id);

// One chunk of a file. offset 0 starts the file; the chunk with `final` checks size and
// CRC-32 of the stored bytes before the file replaces any previous copy.
bool put(JsonVariantConst request, String& error);
bool select(const char* id, String& error);
bool remove(const char* id, String& error);
void writeList(JsonObject out);

}  // namespace AvatarStore
