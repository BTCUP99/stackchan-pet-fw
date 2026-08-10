#include "VibeMode.h"

#include <VibeStackChanApp.h>

#include "MotionController.h"

class NimBLEServer;

bool attachStreetPassToVibeBle(NimBLEServer* server);
void resetStreetPassSharedBleState();
extern MotionController motionController;

namespace {

bool hooksConfigured = false;

void configureHooksOnce() {
    if (hooksConfigured) {
        return;
    }
    VibeStackChanApp::EmbeddedHooks hooks;
    hooks.beforeBleServerStart = attachStreetPassToVibeBle;
    hooks.beforeBleTransportStop = resetStreetPassSharedBleState;
    VibeStackChanApp::configureEmbeddedHooks(hooks);
    hooksConfigured = true;
}

VibeStackChanApp::TransportPowerProfile mapPowerProfile(
    VibeMode::TransportPowerProfile profile) {
    switch (profile) {
        case VibeMode::TransportPowerProfile::Background:
            return VibeStackChanApp::TransportPowerProfile::Background;
        case VibeMode::TransportPowerProfile::ScreenOff:
            return VibeStackChanApp::TransportPowerProfile::ScreenOff;
        case VibeMode::TransportPowerProfile::Active:
        default:
            return VibeStackChanApp::TransportPowerProfile::Active;
    }
}

void synchronizeParentMotion() {
    // VibeStackChan temporarily drives the yaw servo directly for approval
    // notifications. Re-adopt the physical pose before normal pet motion
    // resumes so its logical target never jumps back to a stale position.
    motionController.setMovementPaused(true);
    motionController.setMovementPaused(false);
}

}  // namespace

namespace VibeMode {

bool begin() {
    configureHooksOnce();
    const bool started = VibeStackChanApp::begin();
    if (started) {
        VibeStackChanApp::setTransportPowerProfile(
            VibeStackChanApp::TransportPowerProfile::Active);
    }
    return started;
}

void end() {
    if (!VibeStackChanApp::active()) {
        return;
    }
    VibeStackChanApp::suspend();
    synchronizeParentMotion();
}

void update(uint32_t now) {
    VibeStackChanApp::update(now);
}

void setUiVisible(bool visible) {
    VibeStackChanApp::setUiVisible(visible);
}

void updateBackTouch(bool detected) {
    VibeStackChanApp::updateBackTouch(detected);
}

void pauseInputs() {
    VibeStackChanApp::pauseInputs();
}

void redraw() {
    VibeStackChanApp::redraw();
}

bool active() {
    return VibeStackChanApp::active();
}

void setTransportPowerProfile(TransportPowerProfile profile) {
    VibeStackChanApp::setTransportPowerProfile(mapPowerProfile(profile));
}

bool hidConnected() {
    return VibeStackChanApp::hidConnected();
}

bool bleLinkConnected() {
    return VibeStackChanApp::bleLinkConnected();
}

}  // namespace VibeMode
