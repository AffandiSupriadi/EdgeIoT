/*
 * Temperature & Humidity Sensor Implementation with DUMMY DATA
 * No real sensor readings - generates realistic dummy values
 * Compatible with SDNDataPlane library
 */

#include "SDNDataPlane.h"

// Create specialized sensor class
class TemperatureHumiditySensor : public SDNDataPlane {
private:
  float temperature = 25.0;  // Starting temperature
  float humidity = 50.0;     // Starting humidity
  float tempTrend = 0.1;     // Temperature change trend
  float humidityTrend = 0.5; // Humidity change trend
  
public:
  TemperatureHumiditySensor() : SDNDataPlane(80) {}
  
  void initializeSensor() {
    // Create DeviceCapability structure
    DeviceCapability cap;
    cap.deviceName = "Temperature & Humidity Sensor";
    cap.deviceType = "sensor";
    cap.description = "ESP32 Temperature and Humidity Sensor (Dummy Data)";
    cap.firmwareVersion = "1.0.0";
    cap.hardwareVersion = "ESP32-WROOM-32";
    
    // Allocate sensor array
    cap.sensorCount = 2;
    cap.sensors = new SensorCapability[2];
    
    // Temperature sensor capability
    cap.sensors[0].sensorType = "temperature";
    cap.sensors[0].dataType = "float";
    cap.sensors[0].unit = "°C";
    cap.sensors[0].minValue = -40.0;
    cap.sensors[0].maxValue = 85.0;
    cap.sensors[0].accuracy = 0.5;
    
    // Humidity sensor capability
    cap.sensors[1].sensorType = "humidity";
    cap.sensors[1].dataType = "float";
    cap.sensors[1].unit = "%";
    cap.sensors[1].minValue = 0.0;
    cap.sensors[1].maxValue = 100.0;
    cap.sensors[1].accuracy = 2.0;
    
    // No actuators for sensor device
    cap.actuatorCount = 0;
    cap.actuators = nullptr;
    
    // Default read interval
    cap.readInterval = 10; // seconds
    
    // Set the capability
    setCapability(cap);
    
    Serial.println("Temperature & Humidity Sensor initialized");
    Serial.println("Mode: DUMMY DATA (no real sensors)");
    
    // Seed random number generator
    randomSeed(analogRead(0));
  }
  
  // Override the virtual method collectSensorData() from base class
  String collectSensorData() override {
    // Generate dummy sensor values
    generateDummyReadings();
    
    // Create sensor data response
    StaticJsonDocument<1024> data;
    data["deviceId"] = getDeviceId();
    data["deviceName"] = "Temperature & Humidity Sensor";
    data["timestamp"] = millis();
    
    JsonArray readings = data.createNestedArray("readings");
    
    // Temperature reading
    JsonObject tempReading = readings.createNestedObject();
    tempReading["type"] = "temperature";
    tempReading["value"] = roundToDecimals(temperature, 2);
    tempReading["unit"] = "°C";
    tempReading["status"] = "ok";
    
    // Humidity reading
    JsonObject humReading = readings.createNestedObject();
    humReading["type"] = "humidity";
    humReading["value"] = roundToDecimals(humidity, 1);
    humReading["unit"] = "%";
    humReading["status"] = "ok";
    
    // Add metadata
    JsonObject metadata = data.createNestedObject("metadata");
    metadata["dataSource"] = "dummy";
    metadata["sampleTime"] = millis();
    
    String response;
    serializeJson(data, response);
    return response;
  }
  
  // Override executeCommand for actuator functionality
  bool executeCommand(String command, String value) override {
    Serial.println("Sensor received command: " + command + " = " + value);
    
    if (command == "reset") {
      // Reset to default values
      temperature = 25.0;
      humidity = 50.0;
      tempTrend = 0.1;
      humidityTrend = 0.5;
      Serial.println("Sensor values reset to defaults");
      return true;
    } else if (command == "set_temp") {
      // Set temperature to specific value
      temperature = value.toFloat();
      temperature = constrain(temperature, -40.0, 85.0);
      Serial.println("Temperature set to: " + String(temperature, 2) + "°C");
      return true;
    } else if (command == "set_humidity") {
      // Set humidity to specific value
      humidity = value.toFloat();
      humidity = constrain(humidity, 0.0, 100.0);
      Serial.println("Humidity set to: " + String(humidity, 1) + "%");
      return true;
    } else if (command == "calibrate") {
      // Dummy calibration
      Serial.println("Calibration command received (dummy implementation)");
      return true;
    }
    
    return false;
  }
  
private:
  void generateDummyReadings() {
    // Simulate realistic temperature variations
    // Temperature follows a daily pattern with random variations
    
    // Get time-based variations (simulate daily temperature cycle)
    unsigned long timeOfDay = (millis() / 1000) % 86400; // Seconds in day
    float hourOfDay = timeOfDay / 3600.0;
    
    // Base temperature varies throughout the day (sinusoidal pattern)
    float baseTemp = 25.0 + 5.0 * sin((hourOfDay - 6) * PI / 12.0);
    
    // Add random walk to temperature
    tempTrend += random(-20, 21) / 100.0; // Change trend by -0.2 to +0.2
    tempTrend = constrain(tempTrend, -0.5, 0.5); // Limit trend
    temperature += tempTrend;
    
    // Gradually move toward base temperature
    temperature = temperature * 0.95 + baseTemp * 0.05;
    
    // Add small random noise
    temperature += random(-10, 11) / 20.0; // -0.5 to +0.5 noise
    
    // Constrain to realistic range
    temperature = constrain(temperature, 15.0, 35.0);
    
    // Humidity inversely related to temperature with random variations
    float targetHumidity = 80.0 - (temperature - 15.0) * 2.0;
    
    // Add random walk to humidity
    humidityTrend += random(-50, 51) / 100.0; // Change trend by -0.5 to +0.5
    humidityTrend = constrain(humidityTrend, -2.0, 2.0);
    humidity += humidityTrend;
    
    // Gradually move toward target humidity
    humidity = humidity * 0.9 + targetHumidity * 0.1;
    
    // Add random noise
    humidity += random(-20, 21) / 10.0; // -2.0 to +2.0 noise
    
    // Constrain to valid range
    humidity = constrain(humidity, 20.0, 90.0);
    
    Serial.println("Dummy Sensor Readings:");
    Serial.println("  Temperature: " + String(temperature, 2) + "°C");
    Serial.println("  Humidity: " + String(humidity, 1) + "%");
  }
  
  float roundToDecimals(float value, int decimals) {
    float multiplier = pow(10.0, decimals);
    return round(value * multiplier) / multiplier;
  }
};

// Global sensor instance
TemperatureHumiditySensor sensor;

// Callback functions (optional)
void onCommandReceived(Command cmd) {
  Serial.println("Command callback: " + cmd.command + " = " + cmd.value);
}

void onStatusChanged(String status) {
  Serial.println("Status changed: " + status);
}

bool onSensorRead(String sensorType, float& value, String& unit) {
  // This callback can be used for custom sensor reading logic
  // Return false to use default implementation
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Wait for serial to initialize
  
  Serial.println("\n=== Temperature & Humidity Sensor Starting ===");
  Serial.println("=== DUMMY DATA MODE - No Real Sensors ===");
  
  // Initialize sensor-specific features
  sensor.initializeSensor();
  
  // Set callbacks (optional)
  sensor.setCallbacks(onCommandReceived, onStatusChanged, onSensorRead);
  
  // Start SDN Data Plane
  sensor.begin();
  
  Serial.println("\nSensor ready!");
  Serial.println("Initial Mode: AP (waiting for Control Plane discovery)");
  Serial.println("Device ID: " + sensor.getDeviceId());
  Serial.println("\nDummy data will be generated with realistic patterns:");
  Serial.println("- Temperature follows daily cycle (cooler at night)");
  Serial.println("- Humidity inversely related to temperature");
  Serial.println("- Random variations for realistic data");
}

void loop() {
  // Main SDN Data Plane loop
  sensor.loop();
  
  // The library will automatically handle:
  // - Web server requests
  // - Configuration management
  // - Data transmission to control plane
  // - State management
  // - Dummy data generation when needed
  
  delay(10); // Small delay to prevent watchdog issues
}

/*
 * DUMMY DATA CHARACTERISTICS:
 * - Temperature: 15-35°C range with daily cycle
 * - Humidity: 20-90% range, inversely related to temperature
 * - Realistic variations and noise
 * - Gradual changes (no sudden jumps)
 * 
 * COMMANDS SUPPORTED:
 * - reset: Reset to default values (25°C, 50%)
 * - set_temp: Set temperature to specific value
 * - set_humidity: Set humidity to specific value
 * - calibrate: Dummy calibration command
 * 
 * Expected Flow:
 * 1. Sensor boots in AP mode: "ESP32_Device_XXXX"
 * 2. Control Plane scans WiFi, finds this AP
 * 3. Control Plane connects to AP and calls GET /api/info
 * 4. Control Plane gets device capability information
 * 5. Control Plane sends configuration via POST /api/config
 * 6. Sensor switches to STA mode and connects to main network
 * 7. Sensor automatically registers with Control Plane
 * 8. Sensor starts sending dummy data based on configured interval
 */
