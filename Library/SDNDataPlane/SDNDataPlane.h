/*
 * SDN Data Plane Library
 * Control Plane-Centric Architecture
 * AP Mode → Configuration → STA Mode
 */

#ifndef SDN_DATAPLANE_H
#define SDN_DATAPLANE_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>

// Device capability structures (unchanged)
struct SensorCapability {
    String sensorType;
    String dataType;
    String unit;
    float minValue;
    float maxValue;
    float accuracy;
};

struct ActuatorCapability {
    String command;
    String valueType;
    String supportedValues;
    int responseTime;
};

struct DeviceCapability {
    String deviceId;
    String deviceName;
    String deviceType;
    String description;
    String firmwareVersion;
    String hardwareVersion;
    
    SensorCapability* sensors;
    int sensorCount;
    int readInterval;
    
    ActuatorCapability* actuators;
    int actuatorCount;
};

// Configuration structure
struct DeviceConfig {
    String deviceName;
    String deviceType;
    String wifiSSID;
    String wifiPassword;
    String controlPlaneIP;
    int controlPlanePort;
    int readInterval;
    bool configured;
};

// Command structure
struct Command {
    String id;
    String command;
    String value;
    String timestamp;
};

// Callback types
typedef void (*CommandCallback)(Command cmd);
typedef void (*StatusCallback)(String status);
typedef bool (*SensorReadCallback)(String sensorType, float& value, String& unit);

class SDNDataPlane {
private:
    WebServer* server;
    DeviceCapability capability;
    DeviceConfig config;
    
    // State management
    enum DeviceState {
        DISCOVERY_MODE,     // AP mode, waiting for configuration
        CONFIGURING,        // Received config, switching modes
        OPERATIONAL,        // STA mode, normal operation
        ERROR_STATE         // Error occurred
    };
    
    DeviceState currentState;
    
    // Timers
    unsigned long lastDataSend;
    unsigned long lastHeartbeat;
    unsigned long dataInterval;
    unsigned long heartbeatInterval;
    
    // Callbacks
    CommandCallback onCommandReceived;
    StatusCallback onStatusChanged;
    SensorReadCallback onSensorRead;
    
public:
    SDNDataPlane(int port = 80);
    ~SDNDataPlane();
    
    // Main lifecycle methods
    void begin();
    void loop();
    
    // Configuration methods
    void setCapability(DeviceCapability cap);
    void setCallbacks(CommandCallback cmdCallback, StatusCallback statusCallback, SensorReadCallback sensorCallback);
    
    // Status methods
    String getDeviceId();
    DeviceState getState();
    bool isConfigured();
    bool isOperational();
    
    // Utility methods
    void reset();
    void factoryReset();
    
private:
    // State machine methods
    void handleDiscoveryMode();
    void handleConfiguring();
    void handleOperational();
    void handleErrorState();
    
    // WiFi management
    void startAPMode();
    void startSTAMode();
    void switchToSTAMode();
    
    // Configuration management
    void setupDiscoveryEndpoints();
    void setupOperationalEndpoints();
    bool saveConfig();
    bool loadConfig();
    
    // Communication methods
    void sendSensorData();
    void sendHeartbeat();
    void registerWithControlPlane();
    
    // HTTP handlers
    void handleDeviceInfo();
    void handleConfiguration();
    void handleCommand();
    void handleStatus();
    
    // Utility methods
    String generateDeviceId();
    String getCurrentTimestamp();
    void notifyStatusChange(String status);
    
    // Virtual methods for device-specific implementation
    virtual String collectSensorData();
    virtual bool executeCommand(String command, String value);
};

#endif