from pathlib import Path

Import("env")


project_dir = Path(env.subst("$PROJECT_DIR"))
environment_name = env.subst("$PIOENV")
source_path = project_dir / "src" / "main.cpp"
generated_path = project_dir / "integrations" / "vibe" / "generated" / "main.cpp"
source = source_path.read_text(encoding="utf-8")

if environment_name == "m5stack-cores3-vibestackchan":
    vibe_mode_label = "StackChan"
    vibe_mode_name = "vibestackchan"
elif environment_name == "m5stack-stopwatch-vibewatch":
    vibe_mode_label = "Watch"
    vibe_mode_name = "vibewatch"
else:
    raise RuntimeError(f"Unsupported Vibe integration environment: {environment_name}")


def replace_once(label: str, old: str, new: str) -> None:
    global source
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"Vibe integration marker '{label}' expected once, found {count}"
        )
    source = source.replace(old, new, 1)


def replace_region_once(label: str, start: str, end: str, new: str) -> None:
    """Replace one generated integration region while retaining end marker."""
    global source
    if source.count(start) != 1:
        raise RuntimeError(
            f"Vibe integration region '{label}' start expected once, "
            f"found {source.count(start)}"
        )
    start_index = source.index(start)
    end_index = source.find(end, start_index + len(start))
    if end_index < 0:
        raise RuntimeError(
            f"Vibe integration region '{label}' end marker was not found"
        )
    source = source[:start_index] + new + source[end_index:]


replace_once(
    "include",
    '#include "WebSocketServerController.h"\n',
    '#include "WebSocketServerController.h"\n'
    '#include "VibeMode.h"\n',
)

replace_once(
    "local-mode-value",
    "namespace Usb = UsbSerialProtocol;\n",
    "namespace Usb = UsbSerialProtocol;\n\n"
    "// Dependency-backed mode value. AppState.h remains unchanged so normal\n"
    "// builds keep their existing mode ABI.\n"
    "constexpr ExperienceMode kVibeExperienceMode =\n"
    "  static_cast<ExperienceMode>(4);\n",
)

replace_once(
    "streetpass-shared-ble-flag",
    "bool streetPassBleReady = false;\n",
    "bool streetPassBleReady = false;\n"
    "bool streetPassSharedVibeBle = false;\n",
)

replace_once(
    "streetpass-shared-ble-attach",
    "void beginStreetPassBle() {\n",
    "bool attachStreetPassToVibeBle(NimBLEServer* server) {\n"
    "  if (server == nullptr) {\n"
    "    return false;\n"
    "  }\n"
    "  if (streetPassBleReady) {\n"
    "    return streetPassSharedVibeBle && streetPassGattServer == server;\n"
    "  }\n\n"
    "  NimBLEDevice::setPower(ESP_PWR_LVL_P3);\n"
    "  streetPassGattServer = server;\n"
    "  // The Vibe controller retains the server callback so its HID subscription and\n"
    "  // connection survive mode changes. StreetPass still receives writes\n"
    "  // through its characteristic callback.\n"
    "  NimBLEService* service = streetPassGattServer->createService(STREETPASS_SERVICE_UUID);\n"
    "  streetPassInfoCharacteristic = service->createCharacteristic(\n"
    "    STREETPASS_INFO_UUID, NIMBLE_PROPERTY::READ);\n"
    "  streetPassPublicCardCharacteristic = service->createCharacteristic(\n"
    "    STREETPASS_PUBLIC_CARD_UUID, NIMBLE_PROPERTY::READ);\n"
    "  streetPassEncounterWriteCharacteristic = service->createCharacteristic(\n"
    "    STREETPASS_ENCOUNTER_WRITE_UUID, NIMBLE_PROPERTY::WRITE);\n"
    "  streetPassEncounterWriteCharacteristic->setCallbacks(\n"
    "    &streetPassEncounterWriteCallbacks);\n"
    "  updateStreetPassGattCharacteristics();\n\n"
    "  NimBLEScan* scan = NimBLEDevice::getScan();\n"
    "  scan->setScanCallbacks(&streetPassScanCallbacks, true);\n"
    "  scan->setActiveScan(false);\n"
    "  scan->setInterval(80);\n"
    "  scan->setWindow(80);\n"
    "  streetPassBleReady = true;\n"
    "  streetPassSharedVibeBle = true;\n"
    "  streetPassBlePaused = true;\n"
    "  streetPassAdvertising = false;\n"
    "  nextStreetPassScanMs = millis() + streetPassInitialScanDelayMs();\n"
    "  Serial.println(\"[streetpass] attached to resident Vibe BLE server\");\n"
    "  return true;\n"
    "}\n\n"
    "void resetStreetPassSharedBleState() {\n"
    "  if (!streetPassSharedVibeBle) {\n"
    "    return;\n"
    "  }\n"
    "  streetPassBleReady = false;\n"
    "  streetPassSharedVibeBle = false;\n"
    "  streetPassScanActive = false;\n"
    "  streetPassAdvertising = false;\n"
    "  streetPassBlePaused = true;\n"
    "  streetPassGattServerConnected = false;\n"
    "  streetPassExchangeInProgress = false;\n"
    "  streetPassGattServer = nullptr;\n"
    "  streetPassGattClient = nullptr;\n"
    "  streetPassInfoCharacteristic = nullptr;\n"
    "  streetPassPublicCardCharacteristic = nullptr;\n"
    "  streetPassEncounterWriteCharacteristic = nullptr;\n"
    "  Serial.println(\"[streetpass] detached from Vibe BLE server\");\n"
    "}\n\n"
    "void beginStreetPassBle() {\n",
)

replace_once(
    "streetpass-shared-ble-deinit",
    "  streetPassGattServerConnected = false;\n"
    "  streetPassExchangeInProgress = false;\n\n"
    "  const bool stopped = NimBLEDevice::deinit(true);\n",
    "  streetPassGattServerConnected = false;\n"
    "  streetPassExchangeInProgress = false;\n\n"
    "  if (streetPassSharedVibeBle) {\n"
    "    // Do not tear down the HID server or its macOS connection. The normal\n"
    "    // mode loop will resume StreetPass scan/advertising on this stack.\n"
    "    streetPassBlePaused = true;\n"
    "    streetPassBleSettleUntilMs = 0;\n"
    "    Serial.println(\"[streetpass] paused on resident Vibe BLE server\");\n"
    "    return true;\n"
    "  }\n\n"
    "  const bool stopped = NimBLEDevice::deinit(true);\n",
)

replace_once(
    "streetpass-shared-ble-idempotent-pause",
    "bool deinitStreetPassBle(const char* reason) {\n"
    "  if (!streetPassBleReady) {\n"
    "    return false;\n"
    "  }\n\n"
    "  Serial.printf(\"[streetpass] BLE deinit reason=%s largestDMA=%u\\n\",\n",
    "bool deinitStreetPassBle(const char* reason) {\n"
    "  if (!streetPassBleReady) {\n"
    "    return false;\n"
    "  }\n"
    "  if (streetPassSharedVibeBle && streetPassBlePaused) {\n"
    "    // appClientConnected() stays true across loop iterations while the\n"
    "    // shared HID server intentionally keeps streetPassBleReady true.\n"
    "    // Do not repeat an already-completed pause or its serial logging.\n"
    "    return false;\n"
    "  }\n\n"
    "  Serial.printf(\"[streetpass] BLE deinit reason=%s largestDMA=%u\\n\",\n",
)

replace_once(
    "display-off-hid-radio-busy",
    "bool streetPassDisplayOffRadioBusy() {\n"
    "  if (displayOn || !streetPassController.enabled()) {\n",
    "bool streetPassDisplayOffRadioBusy() {\n"
    "  // Manual M5.Power.lightSleep tears down the macOS HID session. Test the\n"
    "  // BLE link itself instead of waiting for the vendor HID subscription:\n"
    "  // macOS discovery and subscription are asynchronous after reconnect.\n"
    "  if (VibeMode::bleLinkConnected()) {\n"
    "    return true;\n"
    "  }\n"
    "  if (displayOn || !streetPassController.enabled()) {\n",
)

replace_once(
    "streetpass-pause-for-resident-hid-link",
    "const char* streetPassBusyReason() {\n"
    "#if !STACKCHAN_DEVICE_STOPWATCH\n",
    "const char* streetPassBusyReason() {\n"
    "  // Continuous StreetPass scanning on the shared controller can starve\n"
    "  // the interactive HID link. Pause it only while Vibe owns the UI; a\n"
    "  // resident background HID connection must not prevent StreetPass from\n"
    "  // resuming after the user returns to a normal experience mode.\n"
    "  if (VibeMode::active() && VibeMode::bleLinkConnected()) {\n"
    f'    return "{vibe_mode_name}_hid";\n'
    "  }\n"
    "#if !STACKCHAN_DEVICE_STOPWATCH\n",
)

replace_once(
    "display-off-hid-power-profile",
    "  appCommsSuspendedForDisplayOff = true;\n\n"
    "  pendingStateAfterPlayback = false;\n",
    "  appCommsSuspendedForDisplayOff = true;\n"
    "  VibeMode::setTransportPowerProfile(\n"
    "    VibeMode::TransportPowerProfile::ScreenOff);\n\n"
    "  pendingStateAfterPlayback = false;\n",
)

replace_once(
    "display-on-hid-power-profile",
    "  appCommsSuspendedForDisplayOff = false;\n"
    "  wifiConnectStartedMs = 0;\n",
    "  appCommsSuspendedForDisplayOff = false;\n"
    "  VibeMode::setTransportPowerProfile(\n"
    "    experienceMode == kVibeExperienceMode\n"
    "      ? VibeMode::TransportPowerProfile::Active\n"
    "      : VibeMode::TransportPowerProfile::Background);\n"
    "  wifiConnectStartedMs = 0;\n",
)

replace_once(
    "display-off-external-speaker-stop",
    "  audioController.stopForDisplayOff();\n"
    '  speechBubbleController.reset("display_off");\n',
    "  audioController.stopForDisplayOff();\n"
    "  if (M5.Speaker.isRunning()) {\n"
    "    // Vibe effects start M5.Speaker outside AudioController, so its\n"
    "    // internal speakerEnabled flag cannot reliably describe the hardware.\n"
    "    // Screen-off must always silence a still-running speaker/I2S task.\n"
    "    M5.Speaker.stop();\n"
    "    M5.Speaker.end();\n"
    '    Serial.println("[audio] external speaker/I2S stopped for display off");\n'
    "  }\n"
    '  speechBubbleController.reset("display_off");\n',
)

replace_once(
    "mode-name",
    "const char* experienceModeName(ExperienceMode mode) {\n"
    "  switch (mode) {\n",
    "const char* experienceModeName(ExperienceMode mode) {\n"
    "  if (mode == kVibeExperienceMode) {\n"
    f'    return "{vibe_mode_name}";\n'
    "  }\n"
    "  switch (mode) {\n",
)

replace_once(
    "mode-leave-audio-busy",
    "  if (audioBusyForUiEffects()) {\n"
    "    pendingExperienceMode = mode;\n"
    "    pendingExperienceModeValid = true;\n"
    '    Serial.printf("[mode] deferred target=%s until playback drains\\n", experienceModeName(mode));\n'
    "    return;\n"
    "  }\n",
    "  const bool leavingVibeMode =\n"
    "    experienceMode == kVibeExperienceMode &&\n"
    "    mode != kVibeExperienceMode;\n"
    "  const bool audioBusy = audioBusyForUiEffects();\n"
    "  if (audioBusy && !leavingVibeMode) {\n"
    "    pendingExperienceMode = mode;\n"
    "    pendingExperienceModeValid = true;\n"
    '    Serial.printf("[mode] deferred target=%s until playback drains\\n", experienceModeName(mode));\n'
    "    return;\n"
    "  }\n"
    "  if (audioBusy) {\n"
    "    // The parent audio updater is intentionally suspended in Vibe mode.\n"
    "    // Leave immediately so the selected normal mode can resume it and\n"
    "    // drain any stale Speaking/playback state.\n"
    '    Serial.printf("[mode] leaving Vibe mode despite suspended audio busy target=%s\\n",\n'
    "                  experienceModeName(mode));\n"
    "  }\n",
)

replace_once(
    "mode-leave",
    "  pendingExperienceModeValid = false;\n"
    "  const ExperienceMode previousMode = experienceMode;\n",
    "  pendingExperienceModeValid = false;\n"
    "  const ExperienceMode previousMode = experienceMode;\n"
    "  if (previousMode == kVibeExperienceMode) {\n"
    "    VibeMode::end();\n"
    "    applyDisplayBrightness();\n"
    "    audioController.setVolume(deviceSettings.volume);\n"
    "  }\n",
)

replace_once(
    "mode-idle",
    "  if (currentState == ChanState::Listening ||\n"
    "      (mode == ExperienceMode::Travel && currentState != ChanState::Idle)) {\n",
    "  if (currentState == ChanState::Listening ||\n"
    "      ((mode == ExperienceMode::Travel ||\n"
    "        mode == kVibeExperienceMode) &&\n"
    "       currentState != ChanState::Idle)) {\n",
)

replace_once(
    "mode-enter",
    "#if STACKCHAN_GURUGURU_FACE_ENABLED\n"
    "  setGuruguruFaceMode(mode == ExperienceMode::Guruguru, now);\n"
    "#endif\n"
    "  experienceMode = mode;\n",
    "#if STACKCHAN_GURUGURU_FACE_ENABLED\n"
    "  setGuruguruFaceMode(mode == ExperienceMode::Guruguru, now);\n"
    "#endif\n"
    "  if (mode == kVibeExperienceMode) {\n"
    f'    deinitStreetPassBle("{vibe_mode_name}_mode");\n'
    "    faceController.setEnabled(false);\n"
    "    if (!VibeMode::begin()) {\n"
    '      Serial.println("[vibe] start failed; returning to conversation");\n'
    "      mode = ExperienceMode::Conversation;\n"
    "    }\n"
    "  }\n"
    "  experienceMode = mode;\n",
)

replace_once(
    "mode-face-enable",
    "  faceController.setEnabled(displayOn && !infoScreenVisible && !experienceModeMenuVisible &&\n"
    "                            !timekeeperDurationMenuVisible && !travelFacePickerVisible);\n",
    "  faceController.setEnabled(displayOn && !infoScreenVisible && !experienceModeMenuVisible &&\n"
    "                            !timekeeperDurationMenuVisible && !travelFacePickerVisible &&\n"
    "                            experienceMode != kVibeExperienceMode);\n",
)

replace_once(
    "mode-sector-geometry",
    "void experienceModeSectorGeometry(ExperienceMode mode,\n"
    "                                  float& boundaryAngleA,\n"
    "                                  float& boundaryAngleB,\n"
    "                                  float& arcStart,\n"
    "                                  float& arcEnd) {\n"
    "  boundaryAngleA = 315.0f;\n"
    "  boundaryAngleB = 45.0f;\n"
    "  arcStart = 225.0f;\n"
    "  arcEnd = 315.0f;\n"
    "  if (mode == ExperienceMode::Guruguru) {\n"
    "    boundaryAngleA = 45.0f;\n"
    "    boundaryAngleB = 135.0f;\n"
    "    arcStart = 315.0f;\n"
    "    arcEnd = 45.0f;\n"
    "  } else if (mode == ExperienceMode::Timekeeper) {\n"
    "    boundaryAngleA = 135.0f;\n"
    "    boundaryAngleB = 225.0f;\n"
    "    arcStart = 45.0f;\n"
    "    arcEnd = 135.0f;\n"
    "  } else if (mode == ExperienceMode::Travel) {\n"
    "    boundaryAngleA = 225.0f;\n"
    "    boundaryAngleB = 315.0f;\n"
    "    arcStart = 135.0f;\n"
    "    arcEnd = 225.0f;\n"
    "  }\n"
    "}\n",
    "void experienceModeSectorGeometry(ExperienceMode mode,\n"
    "                                  float& boundaryAngleA,\n"
    "                                  float& boundaryAngleB,\n"
    "                                  float& arcStart,\n"
    "                                  float& arcEnd) {\n"
    "  // Five equal 72-degree sectors, measured clockwise from 12 o'clock.\n"
    "  boundaryAngleA = 324.0f;\n"
    "  boundaryAngleB = 36.0f;\n"
    "  arcStart = 234.0f;\n"
    "  arcEnd = 306.0f;\n"
    "  if (mode == ExperienceMode::Guruguru) {\n"
    "    boundaryAngleA = 36.0f;\n"
    "    boundaryAngleB = 108.0f;\n"
    "    arcStart = 306.0f;\n"
    "    arcEnd = 18.0f;\n"
    "  } else if (mode == ExperienceMode::Timekeeper) {\n"
    "    boundaryAngleA = 108.0f;\n"
    "    boundaryAngleB = 180.0f;\n"
    "    arcStart = 18.0f;\n"
    "    arcEnd = 90.0f;\n"
    "  } else if (mode == ExperienceMode::Travel) {\n"
    "    boundaryAngleA = 180.0f;\n"
    "    boundaryAngleB = 252.0f;\n"
    "    arcStart = 90.0f;\n"
    "    arcEnd = 162.0f;\n"
    "  } else if (mode == kVibeExperienceMode) {\n"
    "    boundaryAngleA = 252.0f;\n"
    "    boundaryAngleB = 324.0f;\n"
    "    arcStart = 162.0f;\n"
    "    arcEnd = 234.0f;\n"
    "  }\n"
    "}\n",
)

replace_once(
    "round-menu",
    "  M5.Display.fillScreen(TFT_BLACK);\n"
    "  // M5GFX arc angles use zero at 3 o'clock and advance clockwise. Match the\n"
    "  // four touch quadrants: top, right, bottom, and left.\n"
    "  fillExperienceModeSector(225.0f, 315.0f, ExperienceMode::Conversation);\n"
    "  fillExperienceModeSector(315.0f, 360.0f, ExperienceMode::Guruguru);\n"
    "  fillExperienceModeSector(0.0f, 45.0f, ExperienceMode::Guruguru);\n"
    "  fillExperienceModeSector(45.0f, 135.0f, ExperienceMode::Timekeeper);\n"
    "  fillExperienceModeSector(135.0f, 225.0f, ExperienceMode::Travel);\n\n"
    "  const int32_t cx = M5.Display.width() / 2;\n"
    "  const int32_t cy = M5.Display.height() / 2;\n"
    "  const int32_t radius = min(M5.Display.width(), M5.Display.height()) / 2 - 2;\n"
    "  const uint16_t separator = M5.Display.color565(28, 28, 28);\n"
    "  const float boundaryAngles[] = {45.0f, 135.0f, 225.0f, 315.0f};\n"
    "  for (float angle : boundaryAngles) {\n"
    "    const float radians = angle * DEG_TO_RAD;\n"
    "    const int32_t x = cx + static_cast<int32_t>(sinf(radians) * radius);\n"
    "    const int32_t y = cy - static_cast<int32_t>(cosf(radians) * radius);\n"
    "    M5.Display.drawWideLine(cx, cy, x, y, 2.0f, separator);\n"
    "  }\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Conversation);\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Guruguru);\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Timekeeper);\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Travel);\n"
    "  M5.Display.drawCircle(cx, cy, radius, TFT_BLACK);\n"
    "  M5.Display.fillCircle(cx, cy, 6, separator);\n\n"
    '  drawExperienceModeLabel("音声対話", cx, cy - 91, ExperienceMode::Conversation);\n'
    '  drawExperienceModeLabel("ぐるぐる", cx + 88, cy, ExperienceMode::Guruguru);\n'
    '  drawExperienceModeLabel("ストップ", cx, cy + 75, ExperienceMode::Timekeeper);\n'
    '  drawExperienceModeLabel("ウォッチ", cx, cy + 99, ExperienceMode::Timekeeper);\n'
    '  drawExperienceModeLabel("旅モード", cx - 88, cy, ExperienceMode::Travel);\n',
    "  M5.Display.fillScreen(TFT_BLACK);\n"
    "  // M5GFX arc angles use zero at 3 o'clock and advance clockwise.\n"
    "  fillExperienceModeSector(234.0f, 306.0f, ExperienceMode::Conversation);\n"
    "  fillExperienceModeSector(306.0f, 360.0f, ExperienceMode::Guruguru);\n"
    "  fillExperienceModeSector(0.0f, 18.0f, ExperienceMode::Guruguru);\n"
    "  fillExperienceModeSector(18.0f, 90.0f, ExperienceMode::Timekeeper);\n"
    "  fillExperienceModeSector(90.0f, 162.0f, ExperienceMode::Travel);\n"
    "  fillExperienceModeSector(162.0f, 234.0f, kVibeExperienceMode);\n\n"
    "  const int32_t cx = M5.Display.width() / 2;\n"
    "  const int32_t cy = M5.Display.height() / 2;\n"
    "  const int32_t radius = min(M5.Display.width(), M5.Display.height()) / 2 - 2;\n"
    "  const uint16_t separator = M5.Display.color565(28, 28, 28);\n"
    "  const float boundaryAngles[] = {36.0f, 108.0f, 180.0f, 252.0f, 324.0f};\n"
    "  for (float angle : boundaryAngles) {\n"
    "    const float radians = angle * DEG_TO_RAD;\n"
    "    const int32_t x = cx + static_cast<int32_t>(sinf(radians) * radius);\n"
    "    const int32_t y = cy - static_cast<int32_t>(cosf(radians) * radius);\n"
    "    M5.Display.drawWideLine(cx, cy, x, y, 2.0f, separator);\n"
    "  }\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Conversation);\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Guruguru);\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Timekeeper);\n"
    "  drawExperienceModeBevel(cx, cy, radius, ExperienceMode::Travel);\n"
    "  drawExperienceModeBevel(cx, cy, radius, kVibeExperienceMode);\n"
    "  M5.Display.drawCircle(cx, cy, radius, TFT_BLACK);\n"
    "  M5.Display.fillCircle(cx, cy, 6, separator);\n\n"
    '  drawExperienceModeLabel("音声対話", cx, cy - 112, ExperienceMode::Conversation);\n'
    '  drawExperienceModeLabel("ぐるぐる", cx + 108, cy - 35, ExperienceMode::Guruguru);\n'
    '  drawExperienceModeLabel("ストップ", cx + 69, cy + 79, ExperienceMode::Timekeeper);\n'
    '  drawExperienceModeLabel("ウォッチ", cx + 69, cy + 102, ExperienceMode::Timekeeper);\n'
    '  drawExperienceModeLabel("旅モード", cx - 69, cy + 94, ExperienceMode::Travel);\n'
    '  drawExperienceModeLabel("Vibe", cx - 108, cy - 48, kVibeExperienceMode);\n'
    f'  drawExperienceModeLabel("{vibe_mode_label}", cx - 108, cy - 25, kVibeExperienceMode);\n',
)

replace_region_once(
    "cores3-five-slice-menu",
    "#if STACKCHAN_DEVICE_CORES3\n"
    "  {\n"
    "  // Use the entire rectangular display as four pizza-like buttons. Lines from\n",
    "#endif\n"
    "  M5.Display.fillScreen(TFT_BLACK);\n"
    "  // M5GFX arc angles use zero at 3 o'clock and advance clockwise.\n",
    "#if STACKCHAN_DEVICE_CORES3\n"
    "  {\n"
    "  // Five equal radial slices. The radius reaches every display corner, so\n"
    "  // the rectangular panel reads as one pizza cut into five pieces.\n"
    "  const int32_t width = M5.Display.width();\n"
    "  const int32_t height = M5.Display.height();\n"
    "  const int32_t cx = width / 2;\n"
    "  const int32_t cy = height / 2;\n"
    "  const int32_t radius = static_cast<int32_t>(\n"
    "    sqrtf(static_cast<float>(cx * cx + cy * cy))) + 3;\n"
    "  const auto sectorFill = [](ExperienceMode mode) {\n"
    "    const bool selected = experienceModeMenuSelection == mode;\n"
    "    if (mode == ExperienceMode::Conversation) {\n"
    "      return selected ? M5.Display.color565(70, 110, 190)\n"
    "                      : M5.Display.color565(40, 68, 122);\n"
    "    }\n"
    "    if (mode == ExperienceMode::Guruguru) {\n"
    "      return selected ? M5.Display.color565(152, 88, 196)\n"
    "                      : M5.Display.color565(91, 53, 119);\n"
    "    }\n"
    "    if (mode == ExperienceMode::Timekeeper) {\n"
    "      return selected ? M5.Display.color565(57, 161, 119)\n"
    "                      : M5.Display.color565(35, 94, 72);\n"
    "    }\n"
    "    if (mode == ExperienceMode::Travel) {\n"
    "      return selected ? M5.Display.color565(211, 132, 62)\n"
    "                      : M5.Display.color565(123, 76, 38);\n"
    "    }\n"
    "    return selected ? M5.Display.color565(135, 103, 235)\n"
    "                    : M5.Display.color565(75, 55, 139);\n"
    "  };\n"
    "  M5.Display.fillScreen(TFT_BLACK);\n"
    "  const struct { float start; float end; ExperienceMode mode; } sectors[] = {\n"
    "    {234.0f, 306.0f, ExperienceMode::Conversation},\n"
    "    {306.0f, 360.0f, ExperienceMode::Guruguru},\n"
    "    {0.0f, 18.0f, ExperienceMode::Guruguru},\n"
    "    {18.0f, 90.0f, ExperienceMode::Timekeeper},\n"
    "    {90.0f, 162.0f, ExperienceMode::Travel},\n"
    "    {162.0f, 234.0f, kVibeExperienceMode},\n"
    "  };\n"
    "  for (const auto& sector : sectors) {\n"
    "    M5.Display.fillArc(cx, cy, radius, 0, sector.start, sector.end,\n"
    "                       sectorFill(sector.mode));\n"
    "  }\n"
    "  const uint16_t separator = M5.Display.color565(20, 23, 31);\n"
    "  const float boundaryAngles[] = {36.0f, 108.0f, 180.0f, 252.0f, 324.0f};\n"
    "  for (float angle : boundaryAngles) {\n"
    "    const float radians = angle * DEG_TO_RAD;\n"
    "    const int32_t x = cx + static_cast<int32_t>(sinf(radians) * radius);\n"
    "    const int32_t y = cy - static_cast<int32_t>(cosf(radians) * radius);\n"
    "    M5.Display.drawWideLine(cx, cy, x, y, 3.0f, separator);\n"
    "  }\n"
    "  M5.Display.fillCircle(cx, cy, 6, separator);\n"
    '  drawExperienceModeLabel("音声対話", cx, 43,\n'
    "                          ExperienceMode::Conversation);\n"
    '  drawExperienceModeLabel("ぐるぐる", 232, 93,\n'
    "                          ExperienceMode::Guruguru);\n"
    '  drawExperienceModeLabel("ストップ", 208, 174,\n'
    "                          ExperienceMode::Timekeeper);\n"
    '  drawExperienceModeLabel("ウォッチ", 208, 198,\n'
    "                          ExperienceMode::Timekeeper);\n"
    '  drawExperienceModeLabel("旅モード", 112, 185,\n'
    "                          ExperienceMode::Travel);\n"
    '  drawExperienceModeLabel("VibeStack", 88, 80,\n'
    "                          kVibeExperienceMode);\n"
    '  drawExperienceModeLabel("Chan", 88, 106,\n'
    "                          kVibeExperienceMode);\n"
    "  return;\n"
    "  }\n",
)

replace_region_once(
    "cores3-five-slice-hit-test",
    "#if STACKCHAN_DEVICE_CORES3\n"
    "  {\n"
    "  const int32_t width = M5.Display.width();\n"
    "  const int32_t height = M5.Display.height();\n"
    "  const int32_t dx = touch.x - width / 2;\n",
    "#endif\n"
    "  const int32_t cx = M5.Display.width() / 2;\n",
    "#if STACKCHAN_DEVICE_CORES3\n"
    "  {\n"
    "  const int32_t dx = touch.x - M5.Display.width() / 2;\n"
    "  const int32_t dy = touch.y - M5.Display.height() / 2;\n"
    "  float angle = atan2f(static_cast<float>(dx),\n"
    "                       static_cast<float>(-dy)) * 180.0f / PI;\n"
    "  if (angle < 0.0f) {\n"
    "    angle += 360.0f;\n"
    "  }\n"
    "  ExperienceMode selected;\n"
    "  if (angle >= 324.0f || angle < 36.0f) {\n"
    "    selected = ExperienceMode::Conversation;\n"
    "  } else if (angle < 108.0f) {\n"
    "    selected = ExperienceMode::Guruguru;\n"
    "  } else if (angle < 180.0f) {\n"
    "    selected = ExperienceMode::Timekeeper;\n"
    "  } else if (angle < 252.0f) {\n"
    "    selected = ExperienceMode::Travel;\n"
    "  } else {\n"
    "    selected = kVibeExperienceMode;\n"
    "  }\n"
    "  experienceModeMenuSelection = selected;\n"
    "  requestExperienceMode(selected, now);\n"
    "  hideExperienceModeMenu();\n"
    "  return true;\n"
    "  }\n",
)

replace_once(
    "cores3-disable-parent-settings-swipe-in-vibe-stackchan",
    "  if (isRightEdgeSettingsSwipe(touch)) {\n"
    "    if (experienceModeMenuVisible) {\n"
    "      hideExperienceModeMenu();\n"
    '      Serial.println("[gesture] mode_menu close right_to_left");\n'
    "      return;\n"
    "    }\n"
    "    setInfoScreenVisible(true);\n"
    "    return;\n"
    "  }\n",
    "  if (isRightEdgeSettingsSwipe(touch)) {\n"
    "    if (experienceModeMenuVisible) {\n"
    "      hideExperienceModeMenu();\n"
    '      Serial.println("[gesture] mode_menu close right_to_left");\n'
    "      return;\n"
    "    }\n"
    "    if (experienceMode == kVibeExperienceMode) {\n"
    "      // VibeStackChan owns its in-mode SET button. A completed parent\n"
    "      // settings swipe only cancels any pressed HID control.\n"
    "      VibeMode::pauseInputs();\n"
    '      Serial.println("[gesture] VibeStackChan settings swipe ignored");\n'
    "      return;\n"
    "    }\n"
    "    setInfoScreenVisible(true);\n"
    "    return;\n"
    "  }\n",
)

replace_once(
    "show-menu-pause",
    "  timekeeperDurationMenuVisible = false;\n"
    "  experienceModeMenuVisible = true;\n",
    "  timekeeperDurationMenuVisible = false;\n"
    "  if (experienceMode == kVibeExperienceMode) {\n"
    "    VibeMode::pauseInputs();\n"
    "  }\n"
    "  experienceModeMenuVisible = true;\n",
)

replace_once(
    "hide-menu-redraw",
    "  experienceModeMenuVisible = false;\n"
    "  faceController.setEnabled(displayOn && !infoScreenVisible &&\n"
    "                            !timekeeperDurationMenuVisible &&\n"
    "                            !travelFacePickerVisible);\n"
    "  if (displayOn && !infoScreenVisible) {\n"
    "    faceController.redrawNow();\n"
    "    if (experienceMode == ExperienceMode::Timekeeper) {\n"
    "      drawTimekeeperOverlay(monotonicMs(), true);\n"
    "    }\n"
    "  }\n",
    "  experienceModeMenuVisible = false;\n"
    "  const bool vibeWatchActive =\n"
    "    experienceMode == kVibeExperienceMode;\n"
    "  faceController.setEnabled(displayOn && !infoScreenVisible &&\n"
    "                            !timekeeperDurationMenuVisible &&\n"
    "                            !travelFacePickerVisible &&\n"
    "                            !vibeWatchActive);\n"
    "  if (displayOn && !infoScreenVisible) {\n"
    "    if (vibeWatchActive) {\n"
    "      VibeMode::redraw();\n"
    "    } else {\n"
    "      faceController.redrawNow();\n"
    "      if (experienceMode == ExperienceMode::Timekeeper) {\n"
    "        drawTimekeeperOverlay(monotonicMs(), true);\n"
    "      }\n"
    "    }\n"
    "  }\n",
)

replace_once(
    "round-menu-hit-test",
    "  ExperienceMode selected;\n"
    "  if (angle >= 315.0f || angle < 45.0f) {\n"
    "    selected = ExperienceMode::Conversation;\n"
    "  } else if (angle < 135.0f) {\n"
    "    selected = ExperienceMode::Guruguru;\n"
    "  } else if (angle < 225.0f) {\n"
    "    selected = ExperienceMode::Timekeeper;\n"
    "  } else {\n"
    "    selected = ExperienceMode::Travel;\n"
    "  }\n",
    "  ExperienceMode selected;\n"
    "  if (angle >= 324.0f || angle < 36.0f) {\n"
    "    selected = ExperienceMode::Conversation;\n"
    "  } else if (angle < 108.0f) {\n"
    "    selected = ExperienceMode::Guruguru;\n"
    "  } else if (angle < 180.0f) {\n"
    "    selected = ExperienceMode::Timekeeper;\n"
    "  } else if (angle < 252.0f) {\n"
    "    selected = ExperienceMode::Travel;\n"
    "  } else {\n"
    "    selected = kVibeExperienceMode;\n"
    "  }\n",
)

replace_once(
    "yellow-short-vibe",
    "    if (wasLongPress || experienceModeMenuVisible) {\n"
    "      hideExperienceModeMenu();\n"
    "      return;\n"
    "    }\n"
    "    if (timekeeperDurationMenuVisible) {\n",
    "    if (wasLongPress || experienceModeMenuVisible) {\n"
    "      hideExperienceModeMenu();\n"
    "      return;\n"
    "    }\n"
    "    if (experienceMode == kVibeExperienceMode) {\n"
    "      // The Vibe controller consumes the completed short press below.\n"
    "      return;\n"
    "    }\n"
    "    if (timekeeperDurationMenuVisible) {\n",
)

replace_once(
    "yellow-chord-cancels-mode-hold",
    "  if (stopwatchYellowButtonPressed && M5.BtnA.isPressed() &&\n"
    "      !stopwatchYellowButtonLongHandled &&\n"
    "      monotonicMs() - stopwatchYellowButtonPressedAtMs >= 700ULL) {\n",
    "  if (experienceMode == kVibeExperienceMode &&\n"
    "      stopwatchYellowButtonPressed && M5.BtnB.isPressed()) {\n"
    "    // The Vibe controller owns the yellow+blue chord. Once blue joins, this gesture\n"
    "    // must never turn into the parent mode-selector hold.\n"
    "    stopwatchYellowButtonPressed = false;\n"
    "    stopwatchYellowButtonLongHandled = false;\n"
    "    stopwatchYellowButtonPressedAtMs = 0;\n"
    '    Serial.println("[button] yellow mode hold canceled for Vibe chord");\n'
    "  }\n"
    "  if (stopwatchYellowButtonPressed && M5.BtnA.isPressed() &&\n"
    "      !stopwatchYellowButtonLongHandled &&\n"
    "      monotonicMs() - stopwatchYellowButtonPressedAtMs >= 700ULL) {\n",
)

replace_once(
    "vibe-owns-buttons",
    "  if (experienceMode == ExperienceMode::Travel && !infoScreenVisible &&\n"
    "      M5.BtnA.wasDecideClickCount()) {\n"
    "    handleTravelYellowClickCount(M5.BtnA.getClickCount());\n"
    "    return;\n"
    "  }\n"
    "  if (M5.BtnB.wasHold()) {\n",
    "  if (experienceMode == ExperienceMode::Travel && !infoScreenVisible &&\n"
    "      M5.BtnA.wasDecideClickCount()) {\n"
    "    handleTravelYellowClickCount(M5.BtnA.getClickCount());\n"
    "    return;\n"
    "  }\n"
    "  if (experienceMode == kVibeExperienceMode) {\n"
    "    // The parent only reserves yellow-button hold for the mode selector.\n"
    "    return;\n"
    "  }\n"
    "  if (M5.BtnB.wasHold()) {\n",
)

replace_once(
    "cores3-vibestackchan-owns-touch",
    "#if STACKCHAN_TIMEKEEPER_ENABLED\n"
    "  if (updateExperienceModeMenuTouch(touch, now)) {\n"
    "    return;\n"
    "  }\n"
    "#if STACKCHAN_DEVICE_CORES3\n"
    "  if (handleTravelScreenDoubleTap(touch, now)) {\n",
    "#if STACKCHAN_TIMEKEEPER_ENABLED\n"
    "  if (updateExperienceModeMenuTouch(touch, now)) {\n"
    "    return;\n"
    "  }\n"
    "#if STACKCHAN_DEVICE_CORES3\n"
    "  // Edge swipes and the parent mode selector run above. All ordinary\n"
    "  // CoreS3 touches belong to VibeStackChan while its UI is visible.\n"
    "  if (experienceMode == kVibeExperienceMode &&\n"
    "      !experienceModeMenuVisible) {\n"
    "    return;\n"
    "  }\n"
    "  if (handleTravelScreenDoubleTap(touch, now)) {\n",
)

replace_once(
    "cores3-vibestackchan-back-touch",
    "#if STACKCHAN_TIMEKEEPER_ENABLED\n"
    "  if (handleTimekeeperBackTouch(now, backTouchDetected)) {\n",
    "  if (experienceMode == kVibeExperienceMode) {\n"
    "    // VibeStackChan uses a rear tap to start microphone input and a\n"
    "    // rear hold to stop and send. Release filtering remains active.\n"
    "    VibeMode::updateBackTouch(backTouchDetected);\n"
    "    return;\n"
    "  }\n\n"
    "#if STACKCHAN_TIMEKEEPER_ENABLED\n"
    "  if (handleTimekeeperBackTouch(now, backTouchDetected)) {\n",
)

replace_once(
    "exclusive-loop",
    "  unsigned long now = millis();\n"
    "  const unsigned long loopStartedAt = now;\n",
    "  unsigned long now = millis();\n"
    "  const unsigned long loopStartedAt = now;\n"
    "  if (experienceMode == kVibeExperienceMode) {\n"
    "    updateButtons(now);\n"
    "#if STACKCHAN_DEVICE_CORES3\n"
    "    // CoreS3 uses its existing left-edge swipe for the parent selector.\n"
    "    // updateTouch returns before ordinary touches so the embedded UI can\n"
    "    // consume them below.\n"
    "    if (displayOn) {\n"
    "      updateTouch(now);\n"
    "    }\n"
    "#else\n"
    "    if (experienceModeMenuVisible) {\n"
    "      updateTouch(now);\n"
    "    }\n"
    "#endif\n"
    "    if (experienceMode != kVibeExperienceMode) {\n"
    "      return;\n"
    "    }\n"
    "#if STACKCHAN_DEVICE_CORES3\n"
    "    updateBackTouch(now);\n"
    "#endif\n"
    "    VibeMode::setUiVisible(displayOn &&\n"
    "                                !experienceModeMenuVisible);\n"
    "    VibeMode::update(now);\n"
    "    return;\n"
    "  }\n"
    "  VibeMode::update(now);\n",
)

replace_once(
    "return-after-button-mode-entry",
    "  updateButtons(now);\n"
    "  updatePendingExperienceMode(now);\n",
    "  updateButtons(now);\n"
    "  updatePendingExperienceMode(now);\n"
    "  // Vibe mode owns BLE activity while its UI is active. Stop this\n"
    "  // normal-mode loop as\n"
    "  // soon as a button or deferred transition enters it, before the later\n"
    "  // StreetPass updater has a chance to recreate its BLE transport.\n"
    "  if (experienceMode == kVibeExperienceMode) {\n"
    "    return;\n"
    "  }\n",
)

replace_once(
    "return-after-touch-mode-entry",
    "  updateMicStatusOverlay();\n"
    "  updateTouch(now);\n"
    "#if STACKCHAN_GURUGURU_FACE_ENABLED\n",
    "  updateMicStatusOverlay();\n"
    "  updateTouch(now);\n"
    "  // CoreS3 enters VibeStackChan from the touch selector. As above, do not\n"
    "  // allow the remainder of this normal-mode loop to restart StreetPass.\n"
    "  if (experienceMode == kVibeExperienceMode) {\n"
    "    return;\n"
    "  }\n"
    "#if STACKCHAN_GURUGURU_FACE_ENABLED\n",
)

generated_path.parent.mkdir(parents=True, exist_ok=True)
if not generated_path.exists() or generated_path.read_text(encoding="utf-8") != source:
    generated_path.write_text(source, encoding="utf-8")
    print(f"[vibe] generated {generated_path.relative_to(project_dir)}")
