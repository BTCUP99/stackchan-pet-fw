#pragma once

#include <Arduino.h>

namespace VibeWatchMode {

enum class TransportPowerProfile : uint8_t {
  Active,
  Background,
  ScreenOff,
};

// Starts the target-specific embedded controller. StopWatch uses GOROman's
// VibeWatch dependency; CoreS3 uses the VibeStackChan dependency. M5.begin()
// and M5.update() remain owned by the parent firmware.
bool begin();

// Suspends the embedded UI and releases pressed HID controls. The BLE HID
// connection remains resident so Codex keeps the same IOHID device instance.
void end();

// Updates input, BLE RPC processing, feedback, and rendering for one loop.
// Call this in normal modes as well so the resident Codex RPC channel remains
// serviced while the VibeWatch UI is suspended.
void update(uint32_t now);

// Controls whether the embedded UI may accept input and render. BLE/RPC state
// continues to be serviced while the CoreS3 display is off or covered by the
// parent mode selector.
void setUiVisible(bool visible);

// CoreS3 Stack-chan uses one rear tap to begin microphone input, another tap
// to stop it, and a subsequent hold to send. During an approval prompt, a
// rear touch approves instead.
void updateBackTouch(bool detected);

// Releases any HID controls before the parent mode selector covers the UI.
void pauseInputs();

// Repaints VibeWatch after the parent mode selector closes.
void redraw();

bool active();

// Keeps the same macOS IOHID connection while asking the BLE central for a
// progressively lower-duty-cycle link outside the interactive VibeWatch UI.
void setTransportPowerProfile(TransportPowerProfile profile);

// True only after the host subscribes to the VibeWatch HID input report.
bool hidConnected();

// True as soon as a BLE central has an active connection to the resident
// VibeWatch server. This deliberately does not wait for the vendor input
// subscription: display-off sleep must not race macOS HID discovery.
bool bleLinkConnected();

}  // namespace VibeWatchMode
