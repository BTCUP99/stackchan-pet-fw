#include "NetLink.h"

#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/message_buffer.h>
#include <freertos/semphr.h>

#include "NetLinkRootCa.h"
#include "esp_websocket_client.h"
#include "mbedtls/platform.h"

namespace NetLink {
namespace {

constexpr const char* kPrefsNamespace = "netlink";
constexpr uint8_t kMaxWifi = 4;
constexpr size_t kRxCapacity = 48 * 1024;
constexpr size_t kTxCapacity = 32 * 1024;
constexpr size_t kTxFrameMax = 8 * 1024 + 32;
constexpr unsigned long kJoinTimeoutMs = 15000;
constexpr unsigned long kOpenTimeoutMs = 20000;
constexpr unsigned long kLanOpenTimeoutMs = 6000;
constexpr unsigned long kLanProbeIntervalMs = 120000;
constexpr int32_t kLanProbeTimeoutMs = 600;
constexpr unsigned long kFirstBackoffMs = 2000;
constexpr unsigned long kMaxBackoffMs = 60000;

enum class State : uint8_t { Off, Joining, Opening, Online, Backoff };

struct Config {
  String ssid[kMaxWifi];
  String password[kMaxWifi];
  uint8_t wifiCount = 0;
  String url;
  String deviceId;
  String token;
  String lanIp;
  uint16_t lanPort = 0;
  String lanCert;  // the Mac's self-signed LAN certificate, pinned
};

Preferences prefs;
Config config;
State state = State::Off;
unsigned long stateSinceMs = 0;
unsigned long retryAtMs = 0;
unsigned long backoffMs = kFirstBackoffMs;
uint8_t wifiIndex = 0;
uint8_t wifiTried = 0;
esp_websocket_client_handle_t client = nullptr;
String headers;  // must outlive the client
volatile bool socketUp = false;
volatile bool socketDown = false;
uint8_t* rxBuffer = nullptr;
size_t rxHead = 0;
size_t rxTail = 0;
portMUX_TYPE rxLock = portMUX_INITIALIZER_UNLOCKED;
uint32_t rxDropped = 0;
uint32_t txDropped = 0;
MessageBufferHandle_t txQueue = nullptr;
StaticMessageBuffer_t txQueueControl;
SemaphoreHandle_t clientLock = nullptr;
uint32_t sessions = 0;
const char* lastError = "";
bool viaLan = false;
String socketUri;  // must outlive the client
unsigned long lastLanProbeMs = 0;
uint8_t failure = 0;  // sticky indicator while retrying: 5 no Wi-Fi, 6 Mac unreachable

void setState(State next) {
  state = next;
  stateSinceMs = millis();
}

void push(const uint8_t* data, size_t length) {
  if (rxBuffer == nullptr) return;
  portENTER_CRITICAL(&rxLock);
  const size_t used = (rxHead + kRxCapacity - rxTail) % kRxCapacity;
  const size_t room = kRxCapacity - 1 - used;
  if (length > room) {
    rxDropped += length - room;
    length = room;
  }
  const size_t first = min(length, kRxCapacity - rxHead);
  memcpy(rxBuffer + rxHead, data, first);
  memcpy(rxBuffer, data + first, length - first);
  rxHead = (rxHead + length) % kRxCapacity;
  portEXIT_CRITICAL(&rxLock);
}

void clearRx() {
  portENTER_CRITICAL(&rxLock);
  rxHead = rxTail = 0;
  portEXIT_CRITICAL(&rxLock);
}

// Runs on the websocket task: only flags and the receive ring are touched here.
void onEvent(void*, esp_event_base_t, int32_t id, void* eventData) {
  const auto* data = static_cast<esp_websocket_event_data_t*>(eventData);
  switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
      socketUp = true;
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_CLOSED:
    case WEBSOCKET_EVENT_ERROR:
      socketDown = true;
      break;
    case WEBSOCKET_EVENT_DATA:
      // Binary frames and their continuations carry SCU1 bytes; the parser reframes them.
      if ((data->op_code == 0x02 || data->op_code == 0x00) && data->data_len > 0) {
        push(reinterpret_cast<const uint8_t*>(data->data_ptr), static_cast<size_t>(data->data_len));
      }
      break;
    default:
      break;
  }
}

// Frames leave from this task so a slow uplink queues instead of stalling the main
// loop; the socket is only aborted when a single write stalls for seconds.
void senderTask(void*) {
  uint8_t* frame = static_cast<uint8_t*>(heap_caps_malloc(kTxFrameMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  for (;;) {
    const size_t length = xMessageBufferReceive(txQueue, frame, kTxFrameMax, portMAX_DELAY);
    if (length == 0) continue;
    xSemaphoreTake(clientLock, portMAX_DELAY);
    if (client != nullptr && socketUp && !socketDown) {
      esp_websocket_client_send_bin(client, reinterpret_cast<const char*>(frame), static_cast<int>(length),
                                    pdMS_TO_TICKS(5000));
    }
    xSemaphoreGive(clientLock);
  }
}

void closeSocket() {
  xSemaphoreTake(clientLock, portMAX_DELAY);
  if (client != nullptr) {
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    client = nullptr;
  }
  xSemaphoreGive(clientLock);
  socketUp = socketDown = false;
  clearRx();
}

bool lanConfigured() {
  IPAddress ip;
  return config.lanPort != 0 && config.lanCert.length() > 0 && ip.fromString(config.lanIp);
}

// A short TCP probe decides whether the Mac is on this network before any TLS attempt,
// so a hotspot never waits on an unreachable home address.
bool lanReachable() {
  IPAddress ip;
  if (!ip.fromString(config.lanIp)) return false;
  WiFiClient probe;
  const bool reachable = probe.connect(ip, config.lanPort, kLanProbeTimeoutMs);
  probe.stop();
  return reachable;
}

bool openSocket(bool lan) {
  closeSocket();
  headers = "Authorization: Bearer " + config.token + "\r\nX-StopWatch-Device: " + config.deviceId + "\r\n";
  socketUri = lan ? "wss://" + config.lanIp + ":" + String(config.lanPort) + "/device" : config.url;
  esp_websocket_client_config_t options = {};
  options.uri = socketUri.c_str();
  if (lan) {
    // Exactly the Mac's pinned certificate; it names no IP, so the name check is moot.
    options.cert_pem = config.lanCert.c_str();
    options.skip_cert_common_name_check = true;
  } else {
    options.cert_pem = kNetLinkRootCa;  // Let's Encrypt chain and hostname are verified
  }
  options.headers = headers.c_str();
  options.disable_auto_reconnect = true;
  options.buffer_size = 4096;
  options.task_stack = 6144;
  options.ping_interval_sec = 15;
  options.pingpong_timeout_sec = 45;
  options.keep_alive_enable = true;
  options.keep_alive_idle = 10;
  options.keep_alive_interval = 5;
  options.keep_alive_count = 3;
  client = esp_websocket_client_init(&options);
  if (client == nullptr) return false;
  esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, onEvent, nullptr);
  if (esp_websocket_client_start(client) != ESP_OK) {
    closeSocket();
    return false;
  }
  return true;
}

void fail(const char* reason) {
  lastError = reason;
  failure = strcmp(reason, "wifi_unavailable") == 0 ? 5 : strcmp(reason, "mac_unreachable") == 0 ? 6 : 0;
  closeSocket();
  retryAtMs = millis() + backoffMs;
  Serial.printf("[netlink] %s, retry in %lus\n", reason, backoffMs / 1000);
  backoffMs = min(backoffMs * 2, kMaxBackoffMs);
  setState(State::Backoff);
}

void join(uint8_t index) {
  wifiIndex = index % config.wifiCount;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true);
  WiFi.disconnect(false, false);
  WiFi.begin(config.ssid[wifiIndex].c_str(), config.password[wifiIndex].c_str());
  Serial.printf("[netlink] joining wifi %s\n", config.ssid[wifiIndex].c_str());
  setState(State::Joining);
}

void startOpening() {
  // The Mac on this network first; otherwise the public Funnel entrance.
  viaLan = lanConfigured() && lanReachable();
  Serial.printf("[netlink] connecting via %s\n", viaLan ? "lan" : "funnel");
  if (openSocket(viaLan)) {
    setState(State::Opening);
  } else {
    fail("mac_unreachable");
  }
}

void load() {
  config = Config();
  prefs.begin(kPrefsNamespace, true);
  config.wifiCount = min<uint8_t>(prefs.getUChar("n", 0), kMaxWifi);
  for (uint8_t i = 0; i < config.wifiCount; ++i) {
    config.ssid[i] = prefs.getString((String("s") + i).c_str(), "");
    config.password[i] = prefs.getString((String("p") + i).c_str(), "");
  }
  config.url = prefs.getString("url", "");
  config.deviceId = prefs.getString("dev", "");
  config.token = prefs.getString("tok", "");
  config.lanIp = prefs.getString("lip", "");
  config.lanPort = prefs.getUShort("lpt", 0);
  config.lanCert = prefs.getString("lcrt", "");
  prefs.end();
}

// mbedTLS defaults to internal RAM, which the face, audio and BLE already fill; a TLS
// session needs ~40 KB. Serve its buffers from PSRAM, falling back to internal RAM.
void* tlsCalloc(size_t count, size_t size) {
  void* memory = heap_caps_calloc(count, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return memory != nullptr ? memory : heap_caps_calloc(count, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

const char* text(JsonVariantConst value) {
  return value.is<const char*>() ? value.as<const char*>() : "";
}

}  // namespace

void begin() {
  mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);
  clientLock = xSemaphoreCreateMutex();
  auto* txStorage = static_cast<uint8_t*>(heap_caps_malloc(kTxCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  txQueue = xMessageBufferCreateStatic(kTxCapacity, txStorage, &txQueueControl);
  xTaskCreatePinnedToCore(senderTask, "netlink_tx", 4096, nullptr, 3, nullptr, 0);
  rxBuffer = static_cast<uint8_t*>(heap_caps_malloc(kRxCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (rxBuffer == nullptr) {
    rxBuffer = static_cast<uint8_t*>(malloc(kRxCapacity));
  }
  load();
  Serial.printf("[netlink] configured=%d wifi=%u\n", configured() ? 1 : 0, config.wifiCount);
}

bool configured() {
  return config.wifiCount > 0 && config.url.startsWith("wss://") && config.deviceId.length() > 0 &&
         config.token.length() > 0;
}

bool connected() {
  return state == State::Online && client != nullptr && socketUp && !socketDown;
}

void stop(const char* reason) {
  if (state == State::Off) return;
  closeSocket();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  backoffMs = kFirstBackoffMs;
  setState(State::Off);
  Serial.printf("[netlink] stopped: %s\n", reason);
}

void update(unsigned long now, bool allowed, bool quiet) {
  if (!allowed || !configured()) {
    stop(allowed ? "unconfigured" : "usb_or_display");
    return;
  }
  switch (state) {
    case State::Off:
      wifiTried = 0;
      join(0);
      break;
    case State::Joining:
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[netlink] wifi joined %s rssi=%d\n", WiFi.SSID().c_str(), WiFi.RSSI());
        if (failure == 5) failure = 0;
        startOpening();
      } else if (now - stateSinceMs > kJoinTimeoutMs) {
        if (++wifiTried >= config.wifiCount) {
          wifiTried = 0;
          WiFi.disconnect(false, false);
          fail("wifi_unavailable");
        } else {
          join(wifiIndex + 1);
        }
      }
      break;
    case State::Opening:
      if (socketUp && !socketDown) {
        ++sessions;
        backoffMs = kFirstBackoffMs;
        lastError = "";
        failure = 0;
        lastLanProbeMs = now;
        setState(State::Online);
        Serial.printf("[netlink] online via %s\n", viaLan ? "lan" : "funnel");
      } else if (socketDown || now - stateSinceMs > (viaLan ? kLanOpenTimeoutMs : kOpenTimeoutMs)) {
        if (viaLan) {
          Serial.println("[netlink] lan rejected, trying funnel");
          viaLan = false;
          if (openSocket(false)) {
            setState(State::Opening);
            break;
          }
        }
        fail("mac_unreachable");  // network, TLS or key; the Mac never says which to a stranger
      }
      break;
    case State::Online:
      if (socketDown || WiFi.status() != WL_CONNECTED) {
        fail("link_lost");
      } else if (!viaLan && quiet && lanConfigured() && now - lastLanProbeMs > kLanProbeIntervalMs) {
        // Came home while on the Funnel: move to the LAN between conversations.
        lastLanProbeMs = now;
        if (lanReachable()) {
          Serial.println("[netlink] home LAN reachable, switching");
          startOpening();
        }
      }
      break;
    case State::Backoff:
      if ((long)(now - retryAtMs) >= 0) {
        if (WiFi.status() == WL_CONNECTED) {
          startOpening();
        } else {
          join(wifiIndex);
        }
      }
      break;
  }
}

bool send(const uint8_t* data, size_t length) {
  // Never blocks the main loop: a full queue drops the frame.
  if (!connected() || length > kTxFrameMax) return false;
  if (xMessageBufferSend(txQueue, data, length, 0) != length) {
    ++txDropped;
    return false;
  }
  return true;
}

size_t available() {
  portENTER_CRITICAL(&rxLock);
  const size_t used = (rxHead + kRxCapacity - rxTail) % kRxCapacity;
  portEXIT_CRITICAL(&rxLock);
  return used;
}

int read() {
  int value = -1;
  portENTER_CRITICAL(&rxLock);
  if (rxTail != rxHead) {
    value = rxBuffer[rxTail];
    rxTail = (rxTail + 1) % kRxCapacity;
  }
  portEXIT_CRITICAL(&rxLock);
  return value;
}

bool configure(JsonVariantConst input, String& error) {
  JsonArrayConst wifi = input["wifi"].as<JsonArrayConst>();
  const String url = text(input["url"]);
  const String deviceId = text(input["deviceId"]);
  const String token = text(input["token"]);
  // No Wi-Fi list keeps the networks already saved (re-pairing renews key and LAN certificate).
  const bool keepWifi = wifi.isNull() || wifi.size() == 0;
  if (keepWifi ? config.wifiCount == 0 : wifi.size() > kMaxWifi) {
    error = "wifi_count";
    return false;
  }
  for (JsonVariantConst entry : wifi) {
    const size_t ssid = strlen(text(entry["ssid"]));
    const size_t password = strlen(text(entry["password"]));
    if (ssid == 0 || ssid > 32 || (password != 0 && (password < 8 || password > 63))) {
      error = "wifi_entry";
      return false;
    }
  }
  if (!url.startsWith("wss://") || url.length() > 200 || deviceId.isEmpty() || deviceId.length() > 40 ||
      token.length() < 20 || token.length() > 100) {
    error = "endpoint";
    return false;
  }
  JsonVariantConst lan = input["lan"];
  const String lanCert = text(lan["cert"]);
  const uint32_t lanPort = lan["port"] | 0U;
  IPAddress lanIp;
  const bool hasLan = !lan.isNull();
  if (hasLan && (!lanCert.startsWith("-----BEGIN CERTIFICATE-----") || lanCert.length() > 3000 ||
                 lanPort == 0 || lanPort > 65535 || !lanIp.fromString(text(lan["ip"])))) {
    error = "lan";
    return false;
  }
  const Config previous = config;
  prefs.begin(kPrefsNamespace, false);
  prefs.clear();
  if (keepWifi) {
    prefs.putUChar("n", previous.wifiCount);
    for (uint8_t i = 0; i < previous.wifiCount; ++i) {
      prefs.putString((String("s") + i).c_str(), previous.ssid[i]);
      prefs.putString((String("p") + i).c_str(), previous.password[i]);
    }
  } else {
    prefs.putUChar("n", static_cast<uint8_t>(wifi.size()));
    uint8_t i = 0;
    for (JsonVariantConst entry : wifi) {
      prefs.putString((String("s") + i).c_str(), text(entry["ssid"]));
      prefs.putString((String("p") + i).c_str(), text(entry["password"]));
      ++i;
    }
  }
  prefs.putString("url", url);
  prefs.putString("dev", deviceId);
  prefs.putString("tok", token);
  if (hasLan) {
    prefs.putString("lip", text(lan["ip"]));
    prefs.putUShort("lpt", static_cast<uint16_t>(lanPort));
    prefs.putString("lcrt", lanCert);
  }
  prefs.end();
  load();
  stop("reconfigured");
  Serial.printf("[netlink] configured wifi=%u\n", config.wifiCount);
  return true;
}

void clear() {
  prefs.begin(kPrefsNamespace, false);
  prefs.clear();
  prefs.end();
  load();
  stop("cleared");
}

void setLanIp(const char* ip) {
  IPAddress parsed;
  if (ip == nullptr || !parsed.fromString(ip) || config.lanIp == ip) return;
  config.lanIp = ip;
  prefs.begin(kPrefsNamespace, false);
  prefs.putString("lip", config.lanIp);
  prefs.end();
  Serial.printf("[netlink] lan address updated %s\n", ip);
}

uint8_t indicator() {
  switch (state) {
    case State::Off: return 0;
    case State::Online: return viaLan ? 2 : 3;
    default: return failure != 0 ? failure : 4;  // keep the last failure while retrying
  }
}

const char* stateName() {
  switch (state) {
    case State::Off: return "off";
    case State::Joining: return "joining";
    case State::Opening: return "opening";
    case State::Online: return "online";
    case State::Backoff: return "backoff";
  }
  return "unknown";
}

void writeStatus(JsonObject out) {
  out["configured"] = configured();
  out["state"] = stateName();
  JsonArray ssids = out["ssids"].to<JsonArray>();
  for (uint8_t i = 0; i < config.wifiCount; ++i) ssids.add(config.ssid[i]);
  out["url"] = config.url;
  out["deviceId"] = config.deviceId;
  if (WiFi.status() == WL_CONNECTED) {
    out["ssid"] = WiFi.SSID();
    out["rssi"] = WiFi.RSSI();
  }
  out["lastError"] = lastError;
  out["via"] = state == State::Online ? (viaLan ? "lan" : "funnel") : "";
  out["lanConfigured"] = lanConfigured();
  out["lanIp"] = config.lanIp;
  out["sessions"] = sessions;
  out["rxDropped"] = rxDropped;
  out["txDropped"] = txDropped;
  out["heapInternal"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  out["heapInternalMin"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

}  // namespace NetLink
