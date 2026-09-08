#include <Arduino.h>
#include <NimBLEDevice.h>

// Replace with your Govee Light Strip's MAC Address
#define GOVEE_MAC "0A:0B:0C:0D:19:10" // Example MAC address, replace with your actual device's MAC

static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

// Govee 20-byte Hex Payloads
uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

bool lightState = false; 

// Global pointers for the client and characteristic so we can keep the connection alive
NimBLEClient* pClient = nullptr;
BLERemoteCharacteristic* pChar = nullptr;

bool connectToGovee() {
    Serial.printf("Connecting to Govee at %s...\n", GOVEE_MAC);
    BLEAddress goveeAddress(GOVEE_MAC);
    
    if (pClient == nullptr) {
        pClient = NimBLEDevice::createClient();
    }
    
    if(pClient->connect(goveeAddress)) {
        Serial.println("Connected to Govee!");
        
        BLERemoteService* pService = pClient->getService(serviceUUID);
        if(pService != nullptr) {
            pChar = pService->getCharacteristic(charUUID);
            if(pChar != nullptr) {
                return true; // Successfully connected and found the characteristic
            } else {
                Serial.println("Failed to find characteristic.");
            }
        } else {
            Serial.println("Failed to find service.");
        }
        
        // If we found the device but not the service/characteristic, disconnect
        pClient->disconnect();
    } else {
        Serial.println("Connection failed. Is the device powered on and in range?");
    }
    
    return false;
}

void setup() {
    Serial.begin(115200);
    
    // Initialize BLE
    NimBLEDevice::init("");
    Serial.println("ESP32 Ready. Attempting to connect to light strip...");
}

void loop() {
    // Check if we are currently connected
    if (pClient != nullptr && pClient->isConnected() && pChar != nullptr) {
        
        // We are connected, toggle the state and write the payload
        lightState = !lightState;
        if(lightState) {
            pChar->writeValue(powerOn, sizeof(powerOn), true);
            Serial.println("Sent Power ON command.");
        } else {
            pChar->writeValue(powerOff, sizeof(powerOff), true);
            Serial.println("Sent Power OFF command.");
        }
        
        // Wait 2 seconds before the next toggle (change this to blink faster/slower)
        delay(2000);
        
    } else {
        // Not connected. Try to connect.
        if (!connectToGovee()) {
            Serial.println("Will retry connection in 5 seconds...");
            delay(5000); // Wait 5 seconds before trying to connect again
        }
    }
}