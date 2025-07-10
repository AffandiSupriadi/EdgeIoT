/*
 * SDN Data Plane Library Implementation
 * Control Plane-Centric Architecture
 * Fixed for ESP32 compatibility
 */

#include "SDNDataPlane.h"

SDNDataPlane::SDNDataPlane(int port) {
    server = new WebServer(port);
    currentState = DISCOVERY_MODE;
    config.configured = false;
    dataInterval = 10000; // 10 seconds default
    heartbeatInterval = 30000; // 30 seconds default
    lastDataSend = 0;
    lastHeartbeat = 0;
    onCommandReceived = nullptr;
    onStatusChanged = nullptr;
    onSensorRead = nullptr;
}

SDNDataPlane::~SDNDataPlane() {
    delete server;
}

void SDNDataPlane::begin() {
    Serial.begin(115200);
    Serial.println("SDN Data Plane Starting...");
    
    // Initialize SPIFFS
    if (!SPIFFS.begin(true)) {
        Serial.println("SPIFFS initialization failed");
        currentState = ERROR_STATE;
        return;
    }
    
    // Generate device ID
    capability.deviceId = generateDeviceId();
    
    // Load configuration
    loadConfig();
    
    // Start in appropriate mode
    if (config.configured) {
        Serial.println("Configuration found, starting in operational mode");
        startSTAMode();
    } else {
        Serial.println("No configuration found, starting in discovery mode");
        startAPMode();
    }
}

void SDNDataPlane::loop() {
    server->handleClient();
    
    switch (currentState) {
        case DISCOVERY_MODE:
            handleDiscoveryMode();
            break;
        case CONFIGURING:
            handleConfiguring();
            break;
        case OPERATIONAL:
            handleOperational();
            break;
        case ERROR_STATE:
            handleErrorState();
            break;
    }
}

void SDNDataPlane::setCapability(DeviceCapability cap) {
    capability = cap;
    capability.deviceId = generateDeviceId();
}

void SDNDataPlane::setCallbacks(CommandCallback cmdCallback, StatusCallback statusCallback, SensorReadCallback sensorCallback) {
    onCommandReceived = cmdCallback;
    onStatusChanged = statusCallback;
    onSensorRead = sensorCallback;
}

String SDNDataPlane::getDeviceId() {
    return capability.deviceId;
}

SDNDataPlane::DeviceState SDNDataPlane::getState() {
    return currentState;
}

bool SDNDataPlane::isConfigured() {
    return config.configured;
}

bool SDNDataPlane::isOperational() {
    return currentState == OPERATIONAL;
}

void SDNDataPlane::startAPMode() {
    // Generate unique AP name using ESP32 MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char macStr[13];
    sprintf(macStr, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    String apName = "ESP32_Device_" + String(macStr).substring(6); // Use last 6 chars of MAC
    String apPassword = "12345678";
    
    Serial.println("Starting AP Mode: " + apName);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str(), apPassword.c_str());
    
    Serial.println("AP IP: " + WiFi.softAPIP().toString());
    
    setupDiscoveryEndpoints();
    server->begin();
    
    currentState = DISCOVERY_MODE;
    notifyStatusChange("discovery_mode");
}

void SDNDataPlane::startSTAMode() {
    Serial.println("Starting STA Mode...");
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(config.wifiSSID.c_str(), config.wifiPassword.c_str());
    
    currentState = CONFIGURING;
    notifyStatusChange("connecting");
}

void SDNDataPlane::switchToSTAMode() {
    Serial.println("Switching from AP to STA mode...");
    
    // Stop AP mode
    WiFi.softAPdisconnect(true);
    
    // Start STA mode
    startSTAMode();
}

void SDNDataPlane::setupDiscoveryEndpoints() {
    // Device info endpoint for Control Plane discovery
    server->on("/api/info", HTTP_GET, [this]() {
        handleDeviceInfo();
    });
    
    // Configuration endpoint from Control Plane
    server->on("/api/config", HTTP_POST, [this]() {
        handleConfiguration();
    });
    
    // Status endpoint
    server->on("/api/status", HTTP_GET, [this]() {
        handleStatus();
    });
}

void SDNDataPlane::setupOperationalEndpoints() {
    // Data endpoint for Control Plane to get sensor data
    server->on("/api/data", HTTP_GET, [this]() {
        if (capability.deviceType == "sensor") {
            String sensorData = collectSensorData();
            server->send(200, "application/json", sensorData);
        } else {
            server->send(400, "application/json", "{\"error\":\"Not a sensor device\"}");
        }
    });
    
    // Command endpoint for Control Plane to send commands
    server->on("/api/command", HTTP_POST, [this]() {
        handleCommand();
    });
    
    // Status endpoint
    server->on("/api/status", HTTP_GET, [this]() {
        handleStatus();
    });
}

void SDNDataPlane::handleDiscoveryMode() {
    // Just handle web requests, wait for configuration
    delay(100);
}

void SDNDataPlane::handleConfiguring() {
    // Check WiFi connection
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi: " + WiFi.localIP().toString());
        
        // Setup operational endpoints
        setupOperationalEndpoints();
        server->begin();
        
        // Register with Control Plane
        registerWithControlPlane();
        
        currentState = OPERATIONAL;
        notifyStatusChange("operational");
        
        Serial.println("Device is now operational");
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
        Serial.println("WiFi connection failed, reverting to AP mode");
        config.configured = false;
        startAPMode();
    }
    
    delay(500);
}

void SDNDataPlane::handleOperational() {
    unsigned long now = millis();
    
    // Send sensor data periodically
    if (capability.deviceType == "sensor" && now - lastDataSend > dataInterval) {
        sendSensorData();
        lastDataSend = now;
    }
    
    // Send heartbeat
    if (now - lastHeartbeat > heartbeatInterval) {
        sendHeartbeat();
        lastHeartbeat = now;
    }
    
    delay(100);
}

void SDNDataPlane::handleErrorState() {
    Serial.println("Device in error state, attempting recovery...");
    delay(5000);
    
    // Attempt to recover
    if (!config.configured) {
        startAPMode();
    } else {
        startSTAMode();
    }
}

void SDNDataPlane::handleDeviceInfo() {
    StaticJsonDocument<1024> info;
    
    info["deviceId"] = capability.deviceId;
    info["deviceType"] = capability.deviceType;
    info["description"] = capability.description;
    info["firmwareVersion"] = capability.firmwareVersion;
    info["hardwareVersion"] = capability.hardwareVersion;
    info["configured"] = config.configured;
    info["mode"] = "AP";
    
    // Add capability information
    if (capability.deviceType == "sensor") {
        JsonArray sensors = info.createNestedArray("capability");
        for (int i = 0; i < capability.sensorCount; i++) {
            JsonObject sensor = sensors.createNestedObject();
            sensor["type"] = capability.sensors[i].sensorType;
            sensor["dataType"] = capability.sensors[i].dataType;
            sensor["unit"] = capability.sensors[i].unit;
            sensor["minValue"] = capability.sensors[i].minValue;
            sensor["maxValue"] = capability.sensors[i].maxValue;
            sensor["accuracy"] = capability.sensors[i].accuracy;
        }
    } else if (capability.deviceType == "actuator") {
        JsonArray actuators = info.createNestedArray("capability");
        for (int i = 0; i < capability.actuatorCount; i++) {
            JsonObject actuator = actuators.createNestedObject();
            actuator["command"] = capability.actuators[i].command;
            actuator["valueType"] = capability.actuators[i].valueType;
            actuator["supportedValues"] = capability.actuators[i].supportedValues;
            actuator["responseTime"] = capability.actuators[i].responseTime;
        }
    }
    
    String response;
    serializeJson(info, response);
    server->send(200, "application/json", response);
}

void SDNDataPlane::handleConfiguration() {
    String body = server->arg("plain");
    StaticJsonDocument<512> configData;
    
    DeserializationError error = deserializeJson(configData, body);
    if (error) {
        server->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    
    // Save configuration
    config.deviceName = configData["deviceName"].as<String>();
    config.deviceType = configData["deviceType"].as<String>();
    config.wifiSSID = configData["wifiSSID"].as<String>();
    config.wifiPassword = configData["wifiPassword"].as<String>();
    config.controlPlaneIP = configData["controlPlaneIP"].as<String>();
    config.controlPlanePort = configData["controlPlanePort"];
    config.readInterval = configData["readInterval"];
    config.configured = true;
    
    dataInterval = config.readInterval * 1000;
    capability.deviceName = config.deviceName;
    capability.deviceType = config.deviceType;
    capability.readInterval = config.readInterval;
    
    // Save to SPIFFS
    if (saveConfig()) {
        server->send(200, "application/json", "{\"success\":true,\"message\":\"Configuration saved\"}");
        
        // Switch to STA mode after delay
        delay(2000);
        switchToSTAMode();
    } else {
        server->send(500, "application/json", "{\"success\":false,\"message\":\"Save failed\"}");
    }
}

void SDNDataPlane::handleCommand() {
    if (capability.deviceType != "actuator") {
        server->send(400, "application/json", "{\"error\":\"Not an actuator device\"}");
        return;
    }
    
    String body = server->arg("plain");
    StaticJsonDocument<256> commandData;
    
    DeserializationError error = deserializeJson(commandData, body);
    if (error) {
        server->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
    }
    
    Command cmd;
    cmd.id = commandData["id"].as<String>();
    cmd.command = commandData["command"].as<String>();
    cmd.value = commandData["value"].as<String>();
    cmd.timestamp = commandData["timestamp"].as<String>();
    
    // Execute command
    bool success = executeCommand(cmd.command, cmd.value);
    
    // Notify callback
    if (onCommandReceived) {
        onCommandReceived(cmd);
    }
    
    if (success) {
        server->send(200, "application/json", "{\"success\":true}");
    } else {
        server->send(500, "application/json", "{\"success\":false,\"message\":\"Command failed\"}");
    }
}

void SDNDataPlane::handleStatus() {
    StaticJsonDocument<256> status;
    
    status["deviceId"] = capability.deviceId;
    status["state"] = currentState;
    status["configured"] = config.configured;
    status["uptime"] = millis() / 1000;
    status["freeMemory"] = ESP.getFreeHeap();
    
    if (currentState == OPERATIONAL) {
        status["mode"] = "STA";
        status["wifiRSSI"] = WiFi.RSSI();
        status["ip"] = WiFi.localIP().toString();
    } else {
        status["mode"] = "AP";
        status["ip"] = WiFi.softAPIP().toString();
    }
    
    String response;
    serializeJson(status, response);
    server->send(200, "application/json", response);
}

void SDNDataPlane::sendSensorData() {
    if (currentState != OPERATIONAL) return;
    
    HTTPClient http;
    String url = "http://" + config.controlPlaneIP + ":" + String(config.controlPlanePort) + "/api/data";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    String sensorData = collectSensorData();
    int httpCode = http.POST(sensorData);
    
    if (httpCode == 200) {
        Serial.println("Sensor data sent");
    } else {
        Serial.println("Failed to send sensor data: " + String(httpCode));
    }
    
    http.end();
}

void SDNDataPlane::sendHeartbeat() {
    if (currentState != OPERATIONAL) return;
    
    HTTPClient http;
    String url = "http://" + config.controlPlaneIP + ":" + String(config.controlPlanePort) + "/api/heartbeat";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<256> heartbeat;
    heartbeat["deviceId"] = capability.deviceId;
    heartbeat["timestamp"] = getCurrentTimestamp();
    heartbeat["status"] = "online";
    heartbeat["uptime"] = millis() / 1000;
    heartbeat["freeMemory"] = ESP.getFreeHeap();
    
    String payload;
    serializeJson(heartbeat, payload);
    
    int httpCode = http.POST(payload);
    
    if (httpCode != 200) {
        Serial.println("Heartbeat failed: " + String(httpCode));
    }
    
    http.end();
}

void SDNDataPlane::registerWithControlPlane() {
    HTTPClient http;
    String url = "http://" + config.controlPlaneIP + ":" + String(config.controlPlanePort) + "/api/register";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<512> registration;
    registration["deviceId"] = capability.deviceId;
    registration["name"] = config.deviceName;
    registration["type"] = config.deviceType;
    registration["ip"] = WiFi.localIP().toString();
    registration["readInterval"] = config.readInterval;
    
    String payload;
    serializeJson(registration, payload);
    
    int httpCode = http.POST(payload);
    
    if (httpCode == 200) {
        Serial.println("Registered with Control Plane");
    } else {
        Serial.println("Registration failed: " + String(httpCode));
    }
    
    http.end();
}

bool SDNDataPlane::saveConfig() {
    StaticJsonDocument<512> configDoc;
    configDoc["deviceName"] = config.deviceName;
    configDoc["deviceType"] = config.deviceType;
    configDoc["wifiSSID"] = config.wifiSSID;
    configDoc["wifiPassword"] = config.wifiPassword;
    configDoc["controlPlaneIP"] = config.controlPlaneIP;
    configDoc["controlPlanePort"] = config.controlPlanePort;
    configDoc["readInterval"] = config.readInterval;
    configDoc["configured"] = config.configured;
    
    File file = SPIFFS.open("/config.json", "w");
    if (file) {
        serializeJson(configDoc, file);
        file.close();
        return true;
    }
    return false;
}

bool SDNDataPlane::loadConfig() {
    File file = SPIFFS.open("/config.json", "r");
    if (file) {
        StaticJsonDocument<512> configDoc;
        DeserializationError error = deserializeJson(configDoc, file);
        file.close();
        
        if (!error) {
            config.deviceName = configDoc["deviceName"].as<String>();
            config.deviceType = configDoc["deviceType"].as<String>();
            config.wifiSSID = configDoc["wifiSSID"].as<String>();
            config.wifiPassword = configDoc["wifiPassword"].as<String>();
            config.controlPlaneIP = configDoc["controlPlaneIP"].as<String>();
            config.controlPlanePort = configDoc["controlPlanePort"];
            config.readInterval = configDoc["readInterval"];
            config.configured = configDoc["configured"];
            
            dataInterval = config.readInterval * 1000;
            return true;
        }
    }
    return false;
}

String SDNDataPlane::generateDeviceId() {
    // For ESP32, use MAC address to generate unique ID
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char deviceId[18];
    sprintf(deviceId, "ESP32_%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(deviceId);
}

String SDNDataPlane::getCurrentTimestamp() {
    return String(millis());
}

void SDNDataPlane::notifyStatusChange(String status) {
    if (onStatusChanged) {
        onStatusChanged(status);
    }
}

// Virtual methods - to be overridden by specific implementations
String SDNDataPlane::collectSensorData() {
    StaticJsonDocument<256> data;
    data["deviceId"] = capability.deviceId;
    data["deviceName"] = config.deviceName;
    data["timestamp"] = getCurrentTimestamp();
    
    // Default implementation - should be overridden
    JsonArray readings = data.createNestedArray("readings");
    JsonObject reading = readings.createNestedObject();
    reading["type"] = "generic";
    reading["value"] = random(0, 100);
    reading["unit"] = "units";
    reading["status"] = "ok";
    
    String response;
    serializeJson(data, response);
    return response;
}

bool SDNDataPlane::executeCommand(String command, String value) {
    Serial.println("Executing command: " + command + " with value: " + value);
    // Default implementation - should be overridden
    return true;
}

void SDNDataPlane::reset() {
    ESP.restart();
}

void SDNDataPlane::factoryReset() {
    SPIFFS.remove("/config.json");
    config.configured = false;
    ESP.restart();
}
