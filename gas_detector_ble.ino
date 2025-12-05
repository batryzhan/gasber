#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

/*
 * ESP32 Multi-Gas Detector using MQ-2 and MQ-135 Sensors (MQ-7 Removed)
 * Function: Sets up ESP32 as a BLE Server, reads analog gas sensor data, 
 * and sends the raw ADC values (0-4095) as a JSON string via notifications.
 * Buzzer Control: The buzzer is ONLY activated when any sensor value exceeds DANGER_THRESHOLD (2000).
 */

// --- BLE UUIDs ---
#define SERVICE_UUID      "48b17316-24e5-4f40-848f-37651c6c0391" // Custom Service UUID
#define CHARACTERISTIC_UUID "48b17316-24e5-4f40-848f-37651c6c0392" // Characteristic for sensor data

// --- PIN DEFINITIONS ---
const int MQ2_PIN = 34;    // ADC1_CH6 (MQ-2)
// const int MQ7_PIN = 35; // MQ-7 PIN REMOVED
const int MQ135_PIN = 32;  // ADC1_CH4 (MQ-135)
const int BUZZER_PIN = 23; // Digital Pin for Buzzer

// --- THRESHOLD DEFINITIONS (Raw 0-4095 ADC values) ---
const int DANGER_THRESHOLD = 2000;      // Buzzer activated above this level
const int MEDIUM_ALERT_THRESHOLD = 1000; 

// --- BLE OBJECTS ---
BLEServer *pServer = NULL;
BLECharacteristic *pCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Callback class to handle connection/disconnection events
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("Client connected via BLE.");
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("Client disconnected. Starting advertising...");
      // Ensure the buzzer is OFF upon disconnection
      digitalWrite(BUZZER_PIN, LOW);
    }
};

void setup() {
  Serial.begin(115200);
  Serial.println("--- ESP32 BLE Gas Detector Initializing (2 Sensors) ---"); // Updated description

  // Set the buzzer pin as an output and ensure it is off initially
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); 
  analogReadResolution(12); // Set ADC resolution to 12 bits (0-4095)

  // Initial self-test on buzzer
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);

  // --- 1. Initialize BLE Device ---
  BLEDevice::init("GasDuino v1");
  
  // --- 2. Create BLE Server ---
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // --- 3. Create BLE Service ---
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // --- 4. Create BLE Characteristic ---
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY 
                    );
  
  // Add a descriptor that allows the client to subscribe to notifications
  pCharacteristic->addDescriptor(new BLE2902());

  // --- 5. Start the Service and Advertising ---
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  BLEDevice::startAdvertising();

  Serial.println("BLE advertising started. Waiting for connection...");
}

void loop() {
  // Read sensor values (0-4095)
  int mq2_value = analogRead(MQ2_PIN);
  // MQ-7 reading removed
  int mq135_value = analogRead(MQ135_PIN);

  // DANGER_THRESHOLD (2000+) - Determine if an alarm state is active
  // Alarm check only uses MQ2 and MQ135
  bool danger = (mq2_value > DANGER_THRESHOLD || 
                 mq135_value > DANGER_THRESHOLD);
  
  // --- BUZZER CONTROL LOGIC ---
  if (danger) {
    digitalWrite(BUZZER_PIN, HIGH);
    Serial.println("\n!!! DANGER ALARM: BUZZER ON !!!");
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  // --- SERIAL MONITOR ALERTS ---
  // Medium Alert check only uses MQ2 and MQ135
  if (!danger && (mq2_value >= MEDIUM_ALERT_THRESHOLD || mq135_value >= MEDIUM_ALERT_THRESHOLD)) {
      Serial.println("Medium Alert (1000+): Gas levels elevated. Monitoring closely.");
  }


  // --- BLE DATA TRANSMISSION ---
  if (deviceConnected) {
    // 1. Create a JSON-like string payload (MQ7 entry removed)
    String payload = "{";
    payload += "\"mq2\":" + String(mq2_value) + ",";
    payload += "\"mq135\":" + String(mq135_value) + ",";
    payload += "\"alarm\":" + String(danger ? "true" : "false"); // Send the current alarm state
    payload += "}";

    // 2. Set the characteristic value and notify the client
    pCharacteristic->setValue(payload.c_str());
    pCharacteristic->notify(); 
    
    Serial.print("BLE Notified: ");
    Serial.println(payload);
  }
  
  // Handling disconnection/reconnection
  if (!deviceConnected && oldDeviceConnected) {
      delay(500); 
      pServer->startAdvertising(); 
      oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
      oldDeviceConnected = deviceConnected;
  }
  
  // Control the data update rate
  delay(100); 
}
