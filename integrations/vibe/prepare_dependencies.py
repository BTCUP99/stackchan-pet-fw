import json
from pathlib import Path
import subprocess

Import("env")


project_dir = Path(env.subst("$PROJECT_DIR"))
environment_name = env.subst("$PIOENV")
integration_dir = project_dir / "integrations" / "vibe"
generated_dir = integration_dir / "generated"
upstream_lock = json.loads(
    (integration_dir / "upstreams.json").read_text(encoding="utf-8")
)["dependencies"]


def replace_once(source: str, label: str, old: str, new: str) -> str:
    count = source.count(old)
    if count != 1:
        raise RuntimeError(
            f"Vibe dependency marker '{label}' expected once, found {count}"
        )
    return source.replace(old, new, 1)


def write_if_changed(path: Path, source: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text(encoding="utf-8") != source:
        path.write_text(source, encoding="utf-8")
        print(f"[vibe-dependency] generated {path.relative_to(project_dir)}")


def validate_dependency(name: str) -> Path:
    dependency = upstream_lock[name]
    path = project_dir / dependency["path"]
    if not (path / ".git").exists():
        raise RuntimeError(
            f"Missing {name} submodule. Run: git submodule update --init --recursive"
        )
    actual_commit = subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if actual_commit != dependency["commit"]:
        raise RuntimeError(
            f"{name} is at {actual_commit}, expected {dependency['commit']} from "
            "integrations/vibe/upstreams.json"
        )
    dirty = subprocess.run(
        ["git", "-C", str(path), "status", "--porcelain"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    if dirty:
        raise RuntimeError(
            f"{name} submodule has local modifications; keep adaptations in "
            "integrations/vibe instead"
        )
    return path


def generate_vibewatch() -> None:
    upstream = validate_dependency("vibewatch") / "src" / "main.cpp"
    source = upstream.read_text(encoding="utf-8")

    source = replace_once(
        source,
        "vibewatch-resident-state",
        "volatile bool g_pairingSuccessPending = false;\n"
        "String g_rxBuffer;\n",
        "volatile bool g_pairingSuccessPending = false;\n"
        "bool g_bleStarted = false;\n"
        "volatile std::uint16_t g_hidConnectionHandle = "
        "BLE_HS_CONN_HANDLE_NONE;\n"
        "String g_rxBuffer;\n",
    )

    source = replace_once(
        source,
        "vibewatch-server-callbacks",
        "class HidServerCallbacks : public NimBLEServerCallbacks {\n"
        "    void onConnect(NimBLEServer* server, NimBLEConnInfo& connection) override {\n"
        "        g_connected = true;\n"
        "        g_uiDirty = true;\n"
        "        server->updateConnParams(connection.getConnHandle(), 12, 24, 0, 180);\n"
        "        Serial.printf(\"BLE connected: %s\\n\", connection.getAddress().toString().c_str());\n"
        "    }\n\n"
        "    void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int reason) override {\n"
        "        g_connected = false;\n"
        "        g_activeTouch = -1;\n"
        "        g_uiDirty = true;\n"
        "        Serial.printf(\"BLE disconnected: %d\\n\", reason);\n"
        "        NimBLEDevice::startAdvertising();\n"
        "    }\n\n"
        "    void onAuthenticationComplete(NimBLEConnInfo& connection) override {\n"
        "        if (!connection.isEncrypted()) {\n"
        "            Serial.println(\"BLE encryption failed\");\n"
        "            NimBLEDevice::getServer()->disconnect(connection.getConnHandle());\n"
        "            return;\n"
        "        }\n"
        "        // Defer hardware feedback to Arduino's main loop; NimBLE owns this\n"
        "        // callback task and should only publish the successful result.\n"
        "        g_pairingSuccessPending = true;\n"
        "        Serial.println(\"BLE pairing authenticated\");\n"
        "    }\n"
        "};\n\n"
        "RpcOutputCallbacks g_rpcCallbacks;\n"
        "HidServerCallbacks g_serverCallbacks;\n",
        "class HidServerCallbacks : public NimBLEServerCallbacks {\n"
        "    void onConnect(NimBLEServer* server, NimBLEConnInfo& connection) override {\n"
        "        g_uiDirty = true;\n"
        "        server->updateConnParams(connection.getConnHandle(), 12, 24, 0, 180);\n"
        "        Serial.printf(\"BLE connected: %s\\n\", connection.getAddress().toString().c_str());\n"
        "    }\n\n"
        "    void onDisconnect(NimBLEServer*, NimBLEConnInfo& connection, int reason) override {\n"
        "        if (g_hidConnectionHandle == connection.getConnHandle()) {\n"
        "            g_connected = false;\n"
        "            g_hidConnectionHandle = BLE_HS_CONN_HANDLE_NONE;\n"
        "        }\n"
        "        g_activeTouch = -1;\n"
        "        g_uiDirty = true;\n"
        "        Serial.printf(\"BLE disconnected: %d\\n\", reason);\n"
        "        NimBLEDevice::startAdvertising();\n"
        "    }\n\n"
        "    void onAuthenticationComplete(NimBLEConnInfo& connection) override {\n"
        "        if (!connection.isEncrypted()) {\n"
        "            Serial.println(\"BLE encryption failed\");\n"
        "            NimBLEDevice::getServer()->disconnect(connection.getConnHandle());\n"
        "            return;\n"
        "        }\n"
        "        // Defer hardware feedback to Arduino's main loop; NimBLE owns this\n"
        "        // callback task and should only publish the successful result.\n"
        "        g_pairingSuccessPending = true;\n"
        "        Serial.println(\"BLE pairing authenticated\");\n"
        "    }\n\n"
        "    void onConnParamsUpdate(NimBLEConnInfo& connection) override {\n"
        "#ifdef VIBEWATCH_ON_HID_CONN_PARAMS_UPDATED\n"
        "        VIBEWATCH_ON_HID_CONN_PARAMS_UPDATED(connection);\n"
        "#endif\n"
        "    }\n"
        "};\n\n"
        "class HidInputCallbacks : public NimBLECharacteristicCallbacks {\n"
        "    void onSubscribe(NimBLECharacteristic*, NimBLEConnInfo& connection,\n"
        "                     std::uint16_t subValue) override {\n"
        "        if (subValue != 0) {\n"
        "            g_hidConnectionHandle = connection.getConnHandle();\n"
        "            g_connected = true;\n"
        "            g_uiDirty = true;\n"
        "            Serial.printf(\"BLE HID subscribed: handle=%u\\n\",\n"
        "                          static_cast<unsigned>(g_hidConnectionHandle));\n"
        "#ifdef VIBEWATCH_ON_HID_SUBSCRIBED\n"
        "            VIBEWATCH_ON_HID_SUBSCRIBED(connection);\n"
        "#endif\n"
        "        } else if (g_hidConnectionHandle == connection.getConnHandle()) {\n"
        "            g_connected = false;\n"
        "            g_hidConnectionHandle = BLE_HS_CONN_HANDLE_NONE;\n"
        "            g_uiDirty = true;\n"
        "            Serial.println(\"BLE HID unsubscribed\");\n"
        "        }\n"
        "    }\n"
        "};\n\n"
        "RpcOutputCallbacks g_rpcCallbacks;\n"
        "HidServerCallbacks g_serverCallbacks;\n"
        "HidInputCallbacks g_hidInputCallbacks;\n",
    )

    source = replace_once(
        source,
        "vibewatch-init",
        "    NimBLEDevice::init(g_deviceName);\n"
        "    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);\n",
        "    g_bleStarted = false;\n"
        "    if (!NimBLEDevice::init(g_deviceName)) {\n"
        "        Serial.println(\"Failed to initialize BLE\");\n"
        "        return;\n"
        "    }\n"
        "#ifdef VIBEWATCH_CONFIGURE_BLE_IDENTITY\n"
        "    if (!VIBEWATCH_CONFIGURE_BLE_IDENTITY()) {\n"
        "        Serial.println(\"Failed to configure BLE identity\");\n"
        "        NimBLEDevice::deinit(true);\n"
        "        return;\n"
        "    }\n"
        "#endif\n"
        "    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);\n",
    )

    source = replace_once(
        source,
        "vibewatch-report-hooks",
        "    g_vendorInput->setValue(vendorIdle, sizeof(vendorIdle));\n"
        "    g_vendorOutput->setCallbacks(&g_rpcCallbacks);\n\n"
        "    updateBattery(false);\n",
        "    g_vendorInput->setValue(vendorIdle, sizeof(vendorIdle));\n"
        "    g_vendorInput->setCallbacks(&g_hidInputCallbacks);\n"
        "    g_vendorOutput->setCallbacks(&g_rpcCallbacks);\n\n"
        "#ifdef VIBEWATCH_BEFORE_BLE_SERVER_START\n"
        "    if (!VIBEWATCH_BEFORE_BLE_SERVER_START(g_server)) {\n"
        "        Serial.println(\"Failed to attach shared BLE services\");\n"
        "        return;\n"
        "    }\n"
        "#endif\n"
        "    updateBattery(false);\n",
    )

    source = replace_once(
        source,
        "vibewatch-advertising-result",
        "    advertising->enableScanResponse(true);\n"
        "    advertising->start();\n"
        "    Serial.printf(\"BLE HID advertising started as %s\\n\", g_deviceName);\n",
        "    advertising->enableScanResponse(true);\n"
        "    if (!advertising->start()) {\n"
        "        Serial.println(\"Failed to start BLE advertising\");\n"
        "        return;\n"
        "    }\n"
        "    g_bleStarted = true;\n"
        "    Serial.printf(\"BLE HID advertising started as %s\\n\", g_deviceName);\n",
    )

    write_if_changed(generated_dir / "vibewatch" / "main.cpp", source)


def generate_vibestackchan() -> None:
    upstream_dir = validate_dependency("vibestackchan") / "src"
    output_dir = generated_dir / "vibestackchan"
    files = [
        "VibeStackChanApp.cpp",
        "VibeStackChanApp.h",
        "Sound.cpp",
        "Sound.h",
        "HidProtocol.h",
        "internal/HidRuntime.inc",
    ]
    sources = {
        relative: (upstream_dir / relative).read_text(encoding="utf-8")
        for relative in files
    }

    header = sources["VibeStackChanApp.h"]
    header = replace_once(
        header,
        "vibestackchan-public-api",
        "#include <Arduino.h>\n\n"
        "namespace VibeStackChanApp {\n",
        "#include <Arduino.h>\n\n"
        "class NimBLEServer;\n\n"
        "namespace VibeStackChanApp {\n\n"
        "enum class TransportPowerProfile : uint8_t {\n"
        "    Active,\n"
        "    Background,\n"
        "    ScreenOff,\n"
        "};\n\n"
        "struct EmbeddedHooks {\n"
        "    bool (*beforeBleServerStart)(NimBLEServer*) = nullptr;\n"
        "    void (*beforeBleTransportStop)() = nullptr;\n"
        "};\n\n"
        "void configureEmbeddedHooks(const EmbeddedHooks& hooks);\n",
    )
    header = replace_once(
        header,
        "vibestackchan-lifecycle-api",
        "// Updates screen input, BLE RPC processing, feedback, and rendering.\n"
        "void update(uint32_t now);\n",
        "// Suspends only the Stack-chan UI and input while retaining the BLE\n"
        "// transport so the host keeps the same HID device instance.\n"
        "void suspend();\n\n"
        "// Updates screen input, BLE RPC processing, feedback, and rendering.\n"
        "// RPC remains serviced while the embedded UI is suspended.\n"
        "void update(uint32_t now);\n\n"
        "void setUiVisible(bool visible);\n"
        "void pauseInputs();\n"
        "void redraw();\n"
        "bool active();\n\n"
        "void setTransportPowerProfile(TransportPowerProfile profile);\n"
        "bool hidConnected();\n"
        "bool bleLinkConnected();\n",
    )
    sources["VibeStackChanApp.h"] = header

    app = sources["VibeStackChanApp.cpp"]
    app = replace_once(
        app,
        "vibestackchan-forward-declarations",
        "namespace VibeStackChanApp {\n"
        "enum class TransportPowerProfile : uint8_t {\n"
        "    Active,\n"
        "    Background,\n"
        "    ScreenOff,\n"
        "};\n\n"
        "void setTransportPowerProfile(TransportPowerProfile profile);\n"
        "bool hidConnected();\n"
        "void onHidSubscribed(NimBLEConnInfo& connection);\n"
        "void onHidConnParamsUpdated(NimBLEConnInfo& connection);\n"
        "}\n",
        "namespace VibeStackChanApp {\n"
        "bool beforeBleServerStart(NimBLEServer* server);\n"
        "void onBleTransportStopping();\n"
        "void onHidSubscribed(NimBLEConnInfo& connection);\n"
        "void onHidConnParamsUpdated(NimBLEConnInfo& connection);\n"
        "}\n",
    )
    app = replace_once(
        app,
        "vibestackchan-hook-state",
        "bool appActive = false;\n"
        "bool uiVisible = true;\n",
        "EmbeddedHooks embeddedHooks{};\n"
        "bool appActive = false;\n"
        "bool uiVisible = true;\n",
    )
    app = replace_once(
        app,
        "vibestackchan-stop-hook",
        "void stopBleTransport() {\n"
        "    if (g_server != nullptr) {\n",
        "void stopBleTransport() {\n"
        "    VibeStackChanApp::onBleTransportStopping();\n"
        "    if (g_server != nullptr) {\n",
    )
    app = replace_once(
        app,
        "vibestackchan-embedded-hook-functions",
        "}  // namespace\n\n"
        "bool begin() {\n",
        "}  // namespace\n\n"
        "void configureEmbeddedHooks(const EmbeddedHooks& hooks) {\n"
        "    embeddedHooks = hooks;\n"
        "}\n\n"
        "bool beforeBleServerStart(NimBLEServer* server) {\n"
        "    return embeddedHooks.beforeBleServerStart == nullptr ||\n"
        "           embeddedHooks.beforeBleServerStart(server);\n"
        "}\n\n"
        "void onBleTransportStopping() {\n"
        "    if (embeddedHooks.beforeBleTransportStop != nullptr) {\n"
        "        embeddedHooks.beforeBleTransportStop();\n"
        "    }\n"
        "}\n\n"
        "bool begin() {\n",
    )
    app = replace_once(
        app,
        "vibestackchan-embedded-lifecycle",
        "void update(uint32_t loopNow) {\n",
        "void suspend() {\n"
        "    if (!appActive) {\n"
        "        return;\n"
        "    }\n"
        "    releasePressedControls();\n"
        "    resetCoreBackTouchGesture();\n"
        "    resetCoreSwipeGesture(false);\n"
        "    resetCoreApprovalMotion(true);\n"
        "    forceCoreAgentLedsOff();\n"
        "    if (M5.Speaker.isRunning()) {\n"
        "        M5.Speaker.stop();\n"
        "        M5.Speaker.end();\n"
        "    }\n"
        "    appActive = false;\n"
        "    uiVisible = false;\n"
        "    g_restartAt = 0;\n"
        "    g_coreSurfaceReady = false;\n"
        "    setTransportPowerProfile(TransportPowerProfile::Background);\n"
        "    Serial.println(\"[app] embedded UI suspended; HID kept connected\");\n"
        "}\n\n"
        "void setUiVisible(bool visible) {\n"
        "    if (!appActive || uiVisible == visible) {\n"
        "        return;\n"
        "    }\n"
        "    uiVisible = visible;\n"
        "    if (!visible) {\n"
        "        releasePressedControls();\n"
        "        resetCoreBackTouchGesture();\n"
        "        resetCoreSwipeGesture(false);\n"
        "        g_coreSurfaceReady = false;\n"
        "        forceCoreAgentLedsOff();\n"
        "        return;\n"
        "    }\n"
        "    g_uiDirty = true;\n"
        "    g_coreAgentLedLastUpdateAt = 0;\n"
        "    renderCoreUi(millis());\n"
        "}\n\n"
        "void pauseInputs() {\n"
        "    if (appActive) {\n"
        "        releasePressedControls();\n"
        "        resetCoreBackTouchGesture();\n"
        "        resetCoreSwipeGesture(false);\n"
        "    }\n"
        "}\n\n"
        "void redraw() {\n"
        "    if (!appActive) {\n"
        "        return;\n"
        "    }\n"
        "    g_coreSurfaceReady = false;\n"
        "    g_uiDirty = true;\n"
        "    if (uiVisible) {\n"
        "        renderCoreUi(millis());\n"
        "    }\n"
        "}\n\n"
        "bool active() {\n"
        "    return appActive;\n"
        "}\n\n"
        "void update(uint32_t loopNow) {\n",
    )
    app = replace_once(
        app,
        "vibestackchan-background-rpc-feedback",
        "    char* message = nullptr;\n"
        "    while (xQueueReceive(g_rpcQueue, &message, 0) == pdTRUE) {\n"
        "        processRpc(message);\n"
        "        std::free(message);\n"
        "        message = nullptr;\n"
        "    }\n",
        "    char* message = nullptr;\n"
        "    while (xQueueReceive(g_rpcQueue, &message, 0) == pdTRUE) {\n"
        "        const bool savedAgentStateVibe = g_agentStateVibeEnabled;\n"
        "        if (!appActive) {\n"
        "            // Keep the resident Codex channel serviced without\n"
        "            // vibrating while another parent mode owns the device.\n"
        "            g_agentStateVibeEnabled = false;\n"
        "        }\n"
        "        processRpc(message);\n"
        "        g_agentStateVibeEnabled = savedAgentStateVibe;\n"
        "        std::free(message);\n"
        "        message = nullptr;\n"
        "    }\n",
    )
    app = replace_once(
        app,
        "vibestackchan-link-status",
        "bool hidConnected() {\n"
        "    return g_connected &&\n"
        "           g_hidConnectionHandle != BLE_HS_CONN_HANDLE_NONE;\n"
        "}\n",
        "bool hidConnected() {\n"
        "    return g_connected &&\n"
        "           g_hidConnectionHandle != BLE_HS_CONN_HANDLE_NONE;\n"
        "}\n\n"
        "bool bleLinkConnected() {\n"
        "    return g_server != nullptr && g_server->getConnectedCount() != 0;\n"
        "}\n",
    )
    sources["VibeStackChanApp.cpp"] = app

    runtime = sources["internal/HidRuntime.inc"]
    runtime = replace_once(
        runtime,
        "vibestackchan-before-server-start",
        "    g_vendorInput->setCallbacks(&g_hidInputCallbacks);\n"
        "    g_vendorOutput->setCallbacks(&g_rpcCallbacks);\n\n"
        "    updateBattery(false);\n",
        "    g_vendorInput->setCallbacks(&g_hidInputCallbacks);\n"
        "    g_vendorOutput->setCallbacks(&g_rpcCallbacks);\n\n"
        "    if (!VibeStackChanApp::beforeBleServerStart(g_server)) {\n"
        "        Serial.println(\"Failed to attach embedded BLE services\");\n"
        "        return;\n"
        "    }\n"
        "    updateBattery(false);\n",
    )
    sources["internal/HidRuntime.inc"] = runtime

    for relative, source in sources.items():
        write_if_changed(output_dir / relative, source)


if environment_name == "m5stack-stopwatch-vibewatch":
    generate_vibewatch()
elif environment_name == "m5stack-cores3-vibestackchan":
    generate_vibestackchan()
else:
    raise RuntimeError(f"Unsupported Vibe dependency environment: {environment_name}")
