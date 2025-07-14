/*
 * Complete SDN Control Plane Program - FIXED VERSION
 * Integrates all enhancements: discovery, configuration, data management
 * Author: SDN IoT Project
 * Version: 2.1
 */

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <vector>

// SD Card Pins
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18

// Login credentials
const char* username = "admin";
const char* password = "admin";

// Web Server & WiFi
WebServer server(80);
HTTPClient httpClient;

// Device discovery structure
struct DiscoveredDevice {
  String ssid;
  String deviceId;
  String deviceType;
  String description;
  String firmwareVersion;
  String hardwareVersion;
  bool configured;
  int rssi;
  JsonObject capability;
};

// Global variables
std::vector<DiscoveredDevice> discoveredDevices;

// ==================== WIFI AP MODE ====================
void setupWiFiAP() {
  WiFi.softAP("ESP32-IoT-Server", "12345678");
  Serial.println("Access Point Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
}

// ==================== SD CARD INIT ====================
void initSDCard() {
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card Mount Failed");
    return;
  }
  Serial.println("SD Card Initialized.");
}

// ==================== OFFLINE STORAGE ====================
bool createDirectoryIfNotExists(const char* path) {
  if (!SD.exists(path)) {
    return SD.mkdir(path);
  }
  return true;
}

void initOfflineStorage() {
  createDirectoryIfNotExists("/data");
  createDirectoryIfNotExists("/data/sensors");
  createDirectoryIfNotExists("/data/commands");
  createDirectoryIfNotExists("/data/cloud");
  createDirectoryIfNotExists("/config");
  
  // Initialize cloud queue if not exists
  if (!SD.exists("/data/cloud/queue.json")) {
    File file = SD.open("/data/cloud/queue.json", FILE_WRITE);
    if (file) {
      file.print("{\"queue\":[]}");
      file.close();
    }
  }
  
  // Initialize devices config if not exists
  if (!SD.exists("/config/devices.json")) {
    File file = SD.open("/config/devices.json", FILE_WRITE);
    if (file) {
      file.print("{\"devices\":[]}");
      file.close();
    }
  }
}

String getTodayDateString() {
  // Simple date implementation - in production use RTC
  static int dayCounter = 1;
  return "2024-01-" + String(dayCounter < 10 ? "0" : "") + String(dayCounter);
}

bool saveSensorData(String deviceId, String deviceName, String sensorType, 
                   float value, String unit, String timestamp) {
  String dateStr = getTodayDateString();
  String filename = "/data/sensors/" + dateStr + ".json";
  
  StaticJsonDocument<1024> dayData;
  
  // Load existing data if file exists
  if (SD.exists(filename)) {
    File file = SD.open(filename, FILE_READ);
    if (file) {
      DeserializationError error = deserializeJson(dayData, file);
      file.close();
      if (error) {
        dayData["date"] = dateStr;
        dayData["data"] = JsonArray();
      }
    }
  } else {
    dayData["date"] = dateStr;
    dayData["data"] = JsonArray();
  }
  
  // Add new data point
  JsonArray dataArray = dayData["data"].as<JsonArray>();
  JsonObject newData = dataArray.createNestedObject();
  newData["timestamp"] = timestamp;
  newData["deviceId"] = deviceId;
  newData["deviceName"] = deviceName;
  newData["type"] = sensorType;
  newData["value"] = value;
  newData["unit"] = unit;
  newData["status"] = "ok";
  
  // Save back to file
  File file = SD.open(filename, FILE_WRITE);
  if (file) {
    serializeJson(dayData, file);
    file.close();
    return true;
  }
  return false;
}

void addToCloudQueue(JsonObject sensorData) {
  File file = SD.open("/data/cloud/queue.json", FILE_READ);
  StaticJsonDocument<2048> queueData;
  
  if (file) {
    DeserializationError error = deserializeJson(queueData, file);
    file.close();
    if (error) {
      queueData["queue"] = JsonArray();
    }
  } else {
    queueData["queue"] = JsonArray();
  }
  
  JsonArray queue = queueData["queue"].as<JsonArray>();
  queue.add(sensorData);
  
  // Save back
  File saveFile = SD.open("/data/cloud/queue.json", FILE_WRITE);
  if (saveFile) {
    serializeJson(queueData, saveFile);
    saveFile.close();
  }
}

// ==================== FILE SERVING ====================
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css")) return "text/css";
  if (filename.endsWith(".js")) return "application/javascript";
  return "text/plain";
}

bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "login.html";
  String contentType = getContentType(path);
  String fullPath = "/web" + path;
  if (SD.exists(fullPath)) {
    File file = SD.open(fullPath, FILE_READ);
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

// ==================== AUTHENTICATION ====================
void handleLogin() {
  if (server.method() == HTTP_POST) {
    String user = server.arg("username");
    String pass = server.arg("password");
    if (user == username && pass == password) {
      server.send(200, "application/json", "{\"success\":true, \"token\":\"dummy-token\"}");
    } else {
      server.send(401, "application/json", "{\"success\":false, \"message\":\"Invalid credentials\"}");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleLogout() {
  if (server.method() == HTTP_POST) {
    server.send(200, "application/json", "{\"success\":true,\"message\":\"Logged out\"}");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

// ==================== DEVICE DISCOVERY ====================
bool getDeviceInfo(String ssid, DiscoveredDevice& device) {
  Serial.println("Connecting to " + ssid + " to get device info...");
  
  // Connect to device AP
  WiFi.begin(ssid.c_str(), "12345678");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to " + ssid);
    WiFi.softAP("ESP32-IoT-Server", "12345678");
    return false;
  }
  
  // Get device info via HTTP
  HTTPClient http;
  String url = "http://192.168.4.1/api/info";
  
  http.begin(url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    StaticJsonDocument<1024> deviceInfo;
    DeserializationError error = deserializeJson(deviceInfo, payload);
    
    if (!error) {
      device.ssid = ssid;
      // Fixed: Explicitly convert JsonVariant to String
      device.deviceId = deviceInfo["deviceId"].as<String>();
      device.deviceType = deviceInfo["deviceType"].as<String>();
      device.description = deviceInfo["description"].as<String>();
      device.firmwareVersion = deviceInfo["firmwareVersion"].as<String>();
      device.hardwareVersion = deviceInfo["hardwareVersion"].as<String>();
      device.configured = deviceInfo["configured"];
      device.rssi = WiFi.RSSI();
      
      Serial.println("Device info retrieved successfully");
      http.end();
      
      WiFi.disconnect();
      WiFi.softAP("ESP32-IoT-Server", "12345678");
      
      return true;
    }
  }
  
  http.end();
  WiFi.disconnect();
  WiFi.softAP("ESP32-IoT-Server", "12345678");
  
  return false;
}

// ==================== DEVICE DISCOVERY (FIXED) ====================
void handleAdvancedWiFiScan() {
  Serial.println("Starting advanced WiFi scan for SDN devices...");
  
  discoveredDevices.clear();
  
  // Save current WiFi mode
  wifi_mode_t currentMode = WiFi.getMode();
  
  // Switch to STA mode for scanning
  WiFi.mode(WIFI_STA);
  delay(100); // Let mode change settle
  
  int networkCount = WiFi.scanNetworks();
  
  if (networkCount == 0) {
    // Restore AP mode before returning
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-IoT-Server", "12345678");
    server.send(200, "application/json", "{\"devices\":[]}");
    return;
  }
  
  // First, collect all ESP32_Device SSIDs
  std::vector<String> deviceSSIDs;
  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.startsWith("ESP32_Device_")) {
      deviceSSIDs.push_back(ssid);
      Serial.println("Found potential SDN device: " + ssid);
    }
  }
  
  // Clear scan results to free memory
  WiFi.scanDelete();
  
  // Now connect to each device to get info
  StaticJsonDocument<2048> response;
  JsonArray devices = response.createNestedArray("devices");
  
  for (const String& ssid : deviceSSIDs) {
    // Switch to STA mode to connect
    WiFi.mode(WIFI_STA);
    
    DiscoveredDevice device;
    if (getDeviceInfo(ssid, device)) {
      discoveredDevices.push_back(device);
      
      JsonObject deviceJson = devices.createNestedObject();
      deviceJson["ssid"] = device.ssid;
      deviceJson["deviceId"] = device.deviceId;
      deviceJson["deviceType"] = device.deviceType;
      deviceJson["description"] = device.description;
      deviceJson["configured"] = device.configured;
      deviceJson["rssi"] = device.rssi;
      deviceJson["firmwareVersion"] = device.firmwareVersion;
    }
  }
  
  // Restore AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-IoT-Server", "12345678");
  
  String jsonResponse;
  serializeJson(response, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

// Alternative: Two-phase discovery for better UX
void handleQuickScan() {
  Serial.println("Quick scan for SDN devices...");
  
  // Switch to STA mode
  WiFi.mode(WIFI_STA);
  delay(100);
  
  int networkCount = WiFi.scanNetworks();
  
  StaticJsonDocument<1024> response;
  JsonArray devices = response.createNestedArray("devices");
  
  for (int i = 0; i < networkCount; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.startsWith("ESP32_Device_")) {
      JsonObject device = devices.createNestedObject();
      device["ssid"] = ssid;
      device["rssi"] = WiFi.RSSI(i);
      device["configured"] = false; // Unknown until we connect
      device["needsInfo"] = true;
    }
  }
  
  WiFi.scanDelete();
  
  // Restore AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-IoT-Server", "12345678");
  
  String jsonResponse;
  serializeJson(response, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

// Get info for specific device
void handleGetDeviceInfo() {
  String targetSSID = server.arg("ssid");
  
  if (targetSSID.isEmpty()) {
    server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing SSID\"}");
    return;
  }
  
  // Switch to STA mode
  WiFi.mode(WIFI_STA);
  
  DiscoveredDevice device;
  if (getDeviceInfo(targetSSID, device)) {
    StaticJsonDocument<512> deviceInfo;
    deviceInfo["success"] = true;
    deviceInfo["deviceId"] = device.deviceId;
    deviceInfo["deviceType"] = device.deviceType;
    deviceInfo["description"] = device.description;
    deviceInfo["firmwareVersion"] = device.firmwareVersion;
    deviceInfo["configured"] = device.configured;
    
    String response;
    serializeJson(deviceInfo, response);
    
    // Restore AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-IoT-Server", "12345678");
    
    server.send(200, "application/json", response);
  } else {
    // Restore AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-IoT-Server", "12345678");
    
    server.send(500, "application/json", "{\"success\":false,\"message\":\"Failed to get device info\"}");
  }
}

// ==================== LEGACY WIFI SCAN ====================
void handleWiFiScan() {
  int n = WiFi.scanNetworks();
  if (n == 0) {
    server.send(200, "application/json", "{\"devices\":[]}");
    return;
  }

  String json = "{\"devices\":[";
  for (int i = 0; i < n; i++) {
    json += "{";
    json += "\"name\":\"" + WiFi.SSID(i) + "\",";
    json += "\"ip\":\"192.168.4.1\"";
    json += "}";
    if (i < n - 1) json += ",";
  }
  json += "]}";

  WiFi.scanDelete();
  server.send(200, "application/json", json);
}
// ==================== SECURITY ENHANCEMENTS ====================

// Generate unique password based on device ID
String generateDevicePassword(String deviceId) {
  // Use last 6 characters of device ID for simple hash
  String idPart = deviceId.substring(deviceId.length() - 6);
  
  // Simple hash calculation
  uint32_t hash = 0;
  for (char c : idPart) {
    hash = hash * 31 + c;
  }
  
  // Create password with prefix and hash
  return "IOT_" + String(hash % 100000); // 5-digit number
}

// Store known device passwords
struct KnownDevice {
  String deviceId;
  String password;
};

std::vector<KnownDevice> knownDevices;

// Enhanced device info retrieval with dynamic password
bool getDeviceInfoSecure(String ssid, DiscoveredDevice& device) {
  Serial.println("Connecting to " + ssid + " to get device info...");
  
  // Extract device ID hint from SSID (last part)
  String deviceIdHint = ssid.substring(13); // After "ESP32_Device_"
  
  // Try known passwords first
  String password = "";
  bool found = false;
  
  for (auto& known : knownDevices) {
    if (known.deviceId.endsWith(deviceIdHint)) {
      password = known.password;
      found = true;
      break;
    }
  }
  
  // If not found, try default password for first-time devices
  if (!found) {
    password = "12345678"; // Default for unconfigured devices
  }
  
  // Try to connect
  WiFi.begin(ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to " + ssid);
    WiFi.disconnect();
    return false;
  }
  
  // Get device info via HTTP
  HTTPClient http;
  String url = "http://192.168.4.1/api/info";
  
  http.begin(url);
  http.setTimeout(5000);
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String payload = http.getString();
    
    StaticJsonDocument<1024> deviceInfo;
    DeserializationError error = deserializeJson(deviceInfo, payload);
    
    if (!error) {
      device.ssid = ssid;
      device.deviceId = deviceInfo["deviceId"].as<String>();
      device.deviceType = deviceInfo["deviceType"].as<String>();
      device.description = deviceInfo["description"].as<String>();
      device.firmwareVersion = deviceInfo["firmwareVersion"].as<String>();
      device.hardwareVersion = deviceInfo["hardwareVersion"].as<String>();
      device.configured = deviceInfo["configured"];
      device.rssi = WiFi.RSSI();
      
      // Store device ID and password for future use
      if (!found && device.deviceId != "") {
        KnownDevice known;
        known.deviceId = device.deviceId;
        known.password = password;
        knownDevices.push_back(known);
      }
      
      Serial.println("Device info retrieved successfully");
      http.end();
      WiFi.disconnect();
      
      return true;
    }
  }
  
  http.end();
  WiFi.disconnect();
  
  return false;
}

// Configuration with secure password
bool configureDeviceSecure(DiscoveredDevice& device, String deviceName, String deviceType, int readInterval) {
  Serial.println("Configuring device: " + device.deviceId);
  
  // Find password for this device
  String password = "12345678"; // Default
  for (auto& known : knownDevices) {
    if (known.deviceId == device.deviceId) {
      password = known.password;
      break;
    }
  }
  
  WiFi.begin(device.ssid.c_str(), password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to device for configuration");
    WiFi.disconnect();
    return false;
  }
  
  HTTPClient http;
  String url = "http://192.168.4.1/api/config";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  // Generate new password for this device
  String newPassword = generateDevicePassword(device.deviceId);
  
  StaticJsonDocument<512> configPayload;
  configPayload["deviceName"] = deviceName;
  configPayload["deviceType"] = deviceType;
  configPayload["wifiSSID"] = "ESP32-IoT-Server";
  configPayload["wifiPassword"] = "12345678"; // Control plane password
  configPayload["controlPlaneIP"] = WiFi.softAPIP().toString();
  configPayload["controlPlanePort"] = 80;
  configPayload["readInterval"] = readInterval;
  configPayload["newAPPassword"] = newPassword; // Tell device to change its AP password
  
  String payload;
  serializeJson(configPayload, payload);
  
  int httpCode = http.POST(payload);
  
  bool success = (httpCode == 200);
  
  if (success) {
    Serial.println("Device configured successfully");
    
    // Update known password for this device
    bool updated = false;
    for (auto& known : knownDevices) {
      if (known.deviceId == device.deviceId) {
        known.password = newPassword;
        updated = true;
        break;
      }
    }
    
    if (!updated) {
      KnownDevice known;
      known.deviceId = device.deviceId;
      known.password = newPassword;
      knownDevices.push_back(known);
    }
    
    // Save known devices to SD card
    saveKnownDevices();
  } else {
    Serial.println("Device configuration failed: " + String(httpCode));
  }
  
  http.end();
  WiFi.disconnect();
  
  return success;
}

// Save and load known devices
void saveKnownDevices() {
  StaticJsonDocument<2048> doc;
  JsonArray devices = doc.createNestedArray("knownDevices");
  
  for (auto& known : knownDevices) {
    JsonObject device = devices.createNestedObject();
    device["deviceId"] = known.deviceId;
    device["password"] = known.password;
  }
  
  File file = SD.open("/config/known_devices.json", FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

void loadKnownDevices() {
  File file = SD.open("/config/known_devices.json", FILE_READ);
  if (file) {
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (!error) {
      knownDevices.clear();
      JsonArray devices = doc["knownDevices"];
      
      for (JsonObject device : devices) {
        KnownDevice known;
        known.deviceId = device["deviceId"].as<String>();
        known.password = device["password"].as<String>();
        knownDevices.push_back(known);
      }
    }
  }
}

// ==================== DEVICE CONFIGURATION ====================
bool configureDevice(DiscoveredDevice& device, String deviceName, String deviceType, int readInterval) {
  Serial.println("Configuring device: " + device.deviceId);
  
  WiFi.begin(device.ssid.c_str(), "12345678");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(500);
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to device for configuration");
    WiFi.softAP("ESP32-IoT-Server", "12345678");
    return false;
  }
  
  HTTPClient http;
  String url = "http://192.168.4.1/api/config";
  
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  
  StaticJsonDocument<512> configPayload;
  configPayload["deviceName"] = deviceName;
  configPayload["deviceType"] = deviceType;
  configPayload["wifiSSID"] = "ESP32-IoT-Server";
  configPayload["wifiPassword"] = "12345678";
  configPayload["controlPlaneIP"] = WiFi.softAPIP().toString();
  configPayload["controlPlanePort"] = 80;
  configPayload["readInterval"] = readInterval;
  
  String payload;
  serializeJson(configPayload, payload);
  
  int httpCode = http.POST(payload);
  
  bool success = (httpCode == 200);
  
  if (success) {
    Serial.println("Device configured successfully");
  } else {
    Serial.println("Device configuration failed: " + String(httpCode));
  }
  
  http.end();
  WiFi.disconnect();
  WiFi.softAP("ESP32-IoT-Server", "12345678");
  
  return success;
}

void saveConfiguredDevice(DiscoveredDevice& device, String deviceName, String deviceType, int readInterval) {
  File file = SD.open("/config/devices.json", FILE_READ);
  StaticJsonDocument<2048> data;
  
  if (file) {
    DeserializationError error = deserializeJson(data, file);
    file.close();
    if (error) {
      data["devices"] = JsonArray();
    }
  } else {
    data["devices"] = JsonArray();
  }
  
  JsonArray devices = data["devices"].as<JsonArray>();
  JsonObject newDevice = devices.createNestedObject();
  
  newDevice["id"] = device.deviceId;
  newDevice["name"] = deviceName;
  newDevice["type"] = deviceType;
  newDevice["ip"] = "pending";
  newDevice["readInterval"] = readInterval;
  newDevice["connected"] = false;
  newDevice["configured"] = true;
  newDevice["lastSeen"] = "";
  newDevice["firmwareVersion"] = device.firmwareVersion;
  newDevice["hardwareVersion"] = device.hardwareVersion;
  
  File saveFile = SD.open("/config/devices.json", FILE_WRITE);
  if (saveFile) {
    serializeJson(data, saveFile);
    saveFile.close();
    Serial.println("Device saved to database");
  }
}

void handleDeviceConfiguration() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    
    StaticJsonDocument<512> configData;
    DeserializationError error = deserializeJson(configData, body);
    
    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }
    
    String targetDeviceId = configData["deviceId"];
    String deviceName = configData["deviceName"];
    String deviceType = configData["deviceType"];
    int readInterval = configData["readInterval"];
    
    DiscoveredDevice* targetDevice = nullptr;
    for (auto& device : discoveredDevices) {
      if (device.deviceId == targetDeviceId) {
        targetDevice = &device;
        break;
      }
    }
    
    if (!targetDevice) {
      server.send(404, "application/json", "{\"success\":false,\"message\":\"Device not found\"}");
      return;
    }
    
    if (configureDevice(*targetDevice, deviceName, deviceType, readInterval)) {
      saveConfiguredDevice(*targetDevice, deviceName, deviceType, readInterval);
      server.send(200, "application/json", "{\"success\":true,\"message\":\"Device configured successfully\"}");
    } else {
      server.send(500, "application/json", "{\"success\":false,\"message\":\"Configuration failed\"}");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

// ==================== DEVICE MANAGEMENT ====================
void handleDeviceConfig() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");

    StaticJsonDocument<256> newDevice;
    DeserializationError error = deserializeJson(newDevice, body);
    if (error) {
      server.send(400, "application/json", "{\"success\":false, \"message\":\"Invalid JSON\"}");
      return;
    }

    File file = SD.open("/config/devices.json", FILE_READ);
    StaticJsonDocument<1024> data;

    if (file) {
      DeserializationError err = deserializeJson(data, file);
      file.close();
      if (err) {
        data["devices"] = JsonArray();
      }
    } else {
      data["devices"] = JsonArray();
    }

    JsonArray devices = data["devices"].as<JsonArray>();
    devices.add(newDevice);

    File saveFile = SD.open("/config/devices.json", FILE_WRITE);
    if (!saveFile) {
      server.send(500, "application/json", "{\"success\":false, \"message\":\"Save failed\"}");
      return;
    }

    serializeJson(data, saveFile);
    saveFile.close();

    server.send(200, "application/json", "{\"success\":true}");
  }
  else if (server.method() == HTTP_GET) {
    File file = SD.open("/config/devices.json", FILE_READ);
    if (file) {
      server.streamFile(file, "application/json");
      file.close();
    } else {
      server.send(200, "application/json", "{\"devices\":[]}");
    }
  }
  else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

bool updateDeviceIP(String deviceId, String newIP) {
  File file = SD.open("/config/devices.json", FILE_READ);
  StaticJsonDocument<2048> data;
  
  if (file) {
    DeserializationError error = deserializeJson(data, file);
    file.close();
    if (error) return false;
  } else {
    return false;
  }
  
  JsonArray devices = data["devices"].as<JsonArray>();
  bool found = false;
  
  for (JsonObject device : devices) {
    if (device["id"] == deviceId) {
      device["ip"] = newIP;
      device["connected"] = true;
      device["lastSeen"] = String(millis());
      found = true;
      break;
    }
  }
  
  if (!found) return false;
  
  File saveFile = SD.open("/config/devices.json", FILE_WRITE);
  if (saveFile) {
    serializeJson(data, saveFile);
    saveFile.close();
    return true;
  }
  
  return false;
}

// ==================== ENHANCED DEVICE REGISTRATION ====================
void handleDeviceRegistration() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    
    StaticJsonDocument<512> regData;
    DeserializationError error = deserializeJson(regData, body);
    
    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }
    
    String deviceId = regData["deviceId"];
    String deviceIP = regData["ip"];
    String deviceName = regData["name"];
    String deviceType = regData["type"];
    int readInterval = regData["readInterval"];
    
    // Update or create device entry
    if (updateOrCreateDevice(deviceId, deviceIP, deviceName, deviceType, readInterval)) {
      server.send(200, "application/json", "{\"success\":true}");
      Serial.println("Device registered: " + deviceId + " at " + deviceIP);
    } else {
      server.send(500, "application/json", "{\"success\":false,\"message\":\"Registration failed\"}");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

bool updateOrCreateDevice(String deviceId, String newIP, String deviceName, String deviceType, int readInterval) {
  File file = SD.open("/config/devices.json", FILE_READ);
  StaticJsonDocument<2048> data;
  
  if (file) {
    DeserializationError error = deserializeJson(data, file);
    file.close();
    if (error) {
      data["devices"] = JsonArray();
    }
  } else {
    data["devices"] = JsonArray();
  }
  
  JsonArray devices = data["devices"].as<JsonArray>();
  bool found = false;
  
  // Check if device already exists
  for (JsonObject device : devices) {
    if (device["id"] == deviceId) {
      // Update existing device
      device["ip"] = newIP;
      device["name"] = deviceName;
      device["type"] = deviceType;
      device["readInterval"] = readInterval;
      device["connected"] = true;
      device["lastSeen"] = String(millis());
      device["configured"] = true;
      found = true;
      Serial.println("Updated existing device: " + deviceId);
      break;
    }
  }
  
  // If not found, create new device entry
  if (!found) {
    JsonObject newDevice = devices.createNestedObject();
    newDevice["id"] = deviceId;
    newDevice["name"] = deviceName;
    newDevice["type"] = deviceType;
    newDevice["ip"] = newIP;
    newDevice["readInterval"] = readInterval;
    newDevice["connected"] = true;
    newDevice["configured"] = true;
    newDevice["lastSeen"] = String(millis());
    Serial.println("Created new device entry: " + deviceId);
  }
  
  // Save back to file
  File saveFile = SD.open("/config/devices.json", FILE_WRITE);
  if (saveFile) {
    serializeJson(data, saveFile);
    saveFile.close();
    return true;
  }
  
  return false;
}

// Enhanced heartbeat with connection tracking
void handleHeartbeat() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    
    StaticJsonDocument<512> heartbeatData;
    // FIX: Parameter order should be (destination, source)
    DeserializationError error = deserializeJson(heartbeatData, body);
    
    if (error) {
      // Try simple form data
      String deviceId = server.arg("deviceId");
      if (deviceId.isEmpty()) {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing deviceId\"}");
        return;
      }
      updateDeviceHeartbeat(deviceId);
    } else {
      // JSON data
      String deviceId = heartbeatData["deviceId"];
      if (deviceId.isEmpty()) {
        server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing deviceId in JSON\"}");
        return;
      }
      updateDeviceHeartbeat(deviceId);
    }
    
    server.send(200, "application/json", "{\"success\":true,\"serverTime\":\"" + String(millis()) + "\"}");
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}


void updateDeviceHeartbeat(String deviceId) {
  if (deviceId.isEmpty()) {
    Serial.println("Error: Empty deviceId in heartbeat");
    return;
  }
  
  File file = SD.open("/config/devices.json", FILE_READ);
  StaticJsonDocument<2048> data;
  
  if (file) {
    DeserializationError error = deserializeJson(data, file);
    file.close();
    if (!error) {
      JsonArray devices = data["devices"].as<JsonArray>();
      bool found = false;
      
      for (JsonObject device : devices) {
        if (device["id"] == deviceId) {
          device["lastHeartbeat"] = String(millis());
          device["connected"] = true;
          found = true;
          
          File saveFile = SD.open("/config/devices.json", FILE_WRITE);
          if (saveFile) {
            serializeJson(data, saveFile);
            saveFile.close();
            Serial.println("Heartbeat updated for device: " + deviceId);
          } else {
            Serial.println("Error: Could not save heartbeat for device: " + deviceId);
          }
          break;
        }
      }
      
      if (!found) {
        Serial.println("Warning: Device not found in heartbeat: " + deviceId);
      }
    } else {
      Serial.println("Error: Could not parse devices.json for heartbeat");
    }
  } else {
    Serial.println("Error: Could not open devices.json for heartbeat");
  }
}


// Monitor device connectivity
void checkDeviceConnectivity() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  
  // Check every 30 seconds
  if (now - lastCheck < 30000) {
    return;
  }
  lastCheck = now;
  
  File file = SD.open("/config/devices.json", FILE_READ);
  StaticJsonDocument<2048> data;
  
  if (file) {
    DeserializationError error = deserializeJson(data, file);
    file.close();
    if (!error) {
      JsonArray devices = data["devices"].as<JsonArray>();
      bool hasChanges = false;
      
      unsigned long timeout = 60000; // 60 seconds timeout
      
      for (JsonObject device : devices) {
        if (device["connected"] == true) {
          String lastHeartbeatStr = device["lastHeartbeat"];
          if (!lastHeartbeatStr.isEmpty()) {
            unsigned long lastHeartbeat = lastHeartbeatStr.toInt();
            
            if (now - lastHeartbeat > timeout) {
              device["connected"] = false;
              hasChanges = true;
              Serial.println("Device " + device["id"].as<String>() + " marked as disconnected");
            }
          }
        }
      }
      
      if (hasChanges) {
        File saveFile = SD.open("/config/devices.json", FILE_WRITE);
        if (saveFile) {
          serializeJson(data, saveFile);
          saveFile.close();
        }
      }
    }
  }
}


void handleDeviceStatus() {
  if (server.method() == HTTP_POST) {
    String deviceId = server.arg("deviceId");
    String status = server.arg("status");
    String lastSeen = server.arg("lastSeen");
    
    if (deviceId.isEmpty() || status.isEmpty()) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing parameters\"}");
      return;
    }
    
    File file = SD.open("/config/devices.json", FILE_READ);
    StaticJsonDocument<2048> data;
    
    if (file) {
      DeserializationError error = deserializeJson(data, file);
      file.close();
      if (error) {
        data["devices"] = JsonArray();
      }
    } else {
      data["devices"] = JsonArray();
    }
    
    JsonArray devices = data["devices"].as<JsonArray>();
    bool found = false;
    
    for (JsonObject device : devices) {
      if (device["id"] == deviceId) {
        device["connected"] = (status == "connected");
        if (!lastSeen.isEmpty()) {
          device["lastSeen"] = lastSeen;
        }
        found = true;
        break;
      }
    }
    
    if (!found) {
      server.send(404, "application/json", "{\"success\":false,\"message\":\"Device not found\"}");
      return;
    }
    
    File saveFile = SD.open("/config/devices.json", FILE_WRITE);
    if (saveFile) {
      serializeJson(data, saveFile);
      saveFile.close();
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(500, "application/json", "{\"success\":false,\"message\":\"Save failed\"}");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

// ==================== DATA MANAGEMENT ====================
void handleSensorData() {
  if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    
    StaticJsonDocument<512> sensorData;
    DeserializationError error = deserializeJson(sensorData, body);
    
    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }
    
    String deviceId = sensorData["deviceId"];
    String deviceName = sensorData["deviceName"];
    String sensorType = sensorData["type"];
    float value = sensorData["value"];
    String unit = sensorData["unit"];
    String timestamp = sensorData["timestamp"];
    
    if (saveSensorData(deviceId, deviceName, sensorType, value, unit, timestamp)) {
      // Fixed: Convert to JsonObject before passing
      JsonObject sensorObj = sensorData.as<JsonObject>();
      addToCloudQueue(sensorObj);
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(500, "application/json", "{\"success\":false,\"message\":\"Storage failed\"}");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

void handleLogData() {
  if (server.method() == HTTP_GET) {
    String date = server.arg("date");
    if (date.isEmpty()) {
      date = getTodayDateString();
    }
    
    String filename = "/data/sensors/" + date + ".json";
    
    if (SD.exists(filename)) {
      File file = SD.open(filename, FILE_READ);
      if (file) {
        server.streamFile(file, "application/json");
        file.close();
      } else {
        server.send(500, "application/json", "{\"success\":false,\"message\":\"File read error\"}");
      }
    } else {
      server.send(200, "application/json", "{\"date\":\"" + date + "\",\"data\":[]}");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}


// ==================== COMMAND MANAGEMENT ====================
void handleDeviceCommands() {
  if (server.method() == HTTP_GET) {
    String deviceId = server.arg("deviceId");
    
    if (deviceId.isEmpty()) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Missing deviceId\"}");
      return;
    }
    
    File file = SD.open("/data/commands/pending.json", FILE_READ);
    StaticJsonDocument<1024> commandData;
    
    if (file) {
      DeserializationError error = deserializeJson(commandData, file);
      file.close();
      if (error) {
        commandData["commands"] = JsonArray();
      }
    } else {
      commandData["commands"] = JsonArray();
    }
    
    JsonArray commands = commandData["commands"].as<JsonArray>();
    StaticJsonDocument<512> response;
    JsonArray deviceCommands = response.createNestedArray("commands");
    
    for (JsonObject cmd : commands) {
      if (cmd["deviceId"] == deviceId) {
        deviceCommands.add(cmd);
      }
    }
    
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);
    
  } else if (server.method() == HTTP_POST) {
    String body = server.arg("plain");
    
    StaticJsonDocument<256> newCommand;
    DeserializationError error = deserializeJson(newCommand, body);
    
    if (error) {
      server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
      return;
    }
    
    newCommand["id"] = String(millis());
    newCommand["timestamp"] = String(millis());
    newCommand["status"] = "pending";
    
    File file = SD.open("/data/commands/pending.json", FILE_READ);
    StaticJsonDocument<1024> commandData;
    
    if (file) {
      DeserializationError err = deserializeJson(commandData, file);
      file.close();
      if (err) {
        commandData["commands"] = JsonArray();
      }
    } else {
      commandData["commands"] = JsonArray();
    }
    
    JsonArray commands = commandData["commands"].as<JsonArray>();
    commands.add(newCommand);
    
    File saveFile = SD.open("/data/commands/pending.json", FILE_WRITE);
    if (saveFile) {
      serializeJson(commandData, saveFile);
      saveFile.close();
      server.send(200, "application/json", "{\"success\":true}");
    } else {
      server.send(500, "application/json", "{\"success\":false,\"message\":\"Save failed\"}");
    }
  } else {
    server.send(405, "text/plain", "Method Not Allowed");
  }
}

// ==================== FIRMWARE UPDATE ====================
void handleFirmwareUpdate() {
  File updateFile = SD.open("/firmware/firmware.bin", FILE_READ);
  if (!updateFile) {
    server.send(404, "text/plain", "Firmware not found");
    return;
  }
  server.streamFile(updateFile, "application/octet-stream");
  updateFile.close();
}

void handleFirmwareVersion() {
  File versionFile = SD.open("/firmware/version.txt", FILE_READ);
  if (!versionFile) {
    server.send(404, "text/plain", "Version file not found");
    return;
  }
  server.streamFile(versionFile, "text/plain");
  versionFile.close();
}

// ==================== SETUP & LOOP ====================
void setup() {
  Serial.begin(115200);
  Serial.println("=== SDN Control Plane Starting ===");
  
  setupWiFiAP();
  initSDCard();
  initOfflineStorage();
  loadKnownDevices();

  // Authentication endpoints
  server.on("/login", handleLogin);
  server.on("/logout", handleLogout);
  
  // Device discovery and configuration
  server.on("/api/scan", HTTP_POST, handleWiFiScan);
  server.on("/api/scan/advanced", HTTP_POST, handleAdvancedWiFiScan);
  server.on("/api/configure", HTTP_POST, handleDeviceConfiguration);
  server.on("/api/register", HTTP_POST, handleDeviceRegistration);
  
  // Device management
  server.on("/api/devices", handleDeviceConfig);
  server.on("/api/devices/status", handleDeviceStatus);
  
  // Data management
  server.on("/api/data", handleSensorData);
  server.on("/api/logdata", handleLogData);
  server.on("/api/heartbeat", handleHeartbeat);
  
  // Command management
  server.on("/api/commands", handleDeviceCommands);
  
  // Firmware management
  server.on("/firmware/version.txt", HTTP_GET, handleFirmwareVersion);
  server.on("/firmware/firmware.bin", HTTP_GET, handleFirmwareUpdate);
  
  // File serving
  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "File Not Found");
    }
  });

  server.begin();
  Serial.println("=== SDN Control Plane Ready ===");
  Serial.println("Web server started with complete SDN features");
  Serial.println("Access Point: ESP32-IoT-Server");
  Serial.println("IP Address: " + WiFi.softAPIP().toString());
}

void loop() {
  server.handleClient();
  checkDeviceConnectivity();
  // Add any periodic tasks here
  delay(100);
}
