#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Mac bridge network link: when no USB host is talking to the device, join a saved
// Wi-Fi network and open an authenticated wss:// connection to the Mac. The socket
// carries the same SCU1 byte stream as USB, so the protocol handlers stay shared.
namespace NetLink {

void begin();
// allowed: display on and no USB host traffic recently. Drives Wi-Fi and the socket.
// quiet: no recording or playback, so a brief LAN probe cannot disturb audio.
void update(unsigned long now, bool allowed, bool quiet);
void stop(const char* reason);
bool connected();
bool configured();

bool send(const uint8_t* data, size_t length);
size_t available();
int read();

// Accepted only over the USB link; secrets never leave the device again.
bool configure(JsonVariantConst config, String& error);
void clear();
void writeStatus(JsonObject out);
const char* stateName();
// Mac's current LAN address, sent over the authenticated link (DHCP may move it).
void setLanIp(const char* ip);
// Screen indicator: 0 off, 2 LAN, 3 Funnel, 4 connecting, 5 no Wi-Fi, 6 Mac unreachable.
uint8_t indicator();

}  // namespace NetLink
