#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <LynkTuyaLocal.h>

// --- Wi-Fi Credentials ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- Govee BLE Credentials ---
#define GOVEE_MAC "AA:BB:CC:DD:EE:FF" 
static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

// --- Feit Tuya Wi-Fi Configuration ---
IPAddress tuyaIP(192, 168, 1, 31);
LynkTuyaLocal shelfPlug(tuyaIP, "ebf9fa6de05d0ece56ecfw", "6n~O6wS-Hzk+sPyj"); 

// --- Global State Variables ---
bool globalState = false; // Tracks if lights should be ON or OFF
NimBLEClient* pClient = nullptr;
BLERemoteCharacteristic* pChar = nullptr;

bool connectToGovee() {
    Serial.printf("Connecting to Govee at %s...\n", GOVEE_MAC);
    // Notice the ", 1" is preserved here from your code for the address type
    BLEAddress goveeAddress(GOVEE_MAC, 1); 
    
    if (pClient == nullptr) {
        pClient = NimBLEDevice::createClient();
    }
    
    if(pClient->connect(goveeAddress)) {
        Serial.println("Connected to Govee!");
        BLERemoteService* pService = pClient->getService(serviceUUID);
        if(pService != nullptr) {
            pChar = pService->getCharacteristic(charUUID);
            if(pChar != nullptr) {
                return true; // Successfully connected and characteristic found
            }
        }
        // If it fails to find the service/characteristic, disconnect
        pClient->disconnect();
    } else {
        Serial.println("Govee BLE Connection failed. Retrying later...");
    }
    return false;
}

void setup() {
    Serial.begin(115200);
    
    // 1. Initialize Wi-Fi
    Serial.print("Connecting to Wi-Fi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");

    // 2. Initialize BLE Scanner
    NimBLEDevice::init("");
    Serial.println("ESP32 Ready. Attempting BLE connection...");
}

void loop() {
    // 1. Ensure Wi-Fi stays connected for the Tuya plug
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi disconnected! Reconnecting...");
        WiFi.reconnect();
        delay(2000); // Give it a moment to reconnect before proceeding
        return;      // Skip the rest of the loop until Wi-Fi is back
    }

    // 2. Ensure BLE stays connected for the Govee strip
    if (pClient != nullptr && pClient->isConnected() && pChar != nullptr) {
        
        // --- Both are connected, execute the blink toggle ---
        globalState = !globalState;
        
        Serial.println("Toggling Feit Plug over Wi-Fi...");
        shelfPlug.setDp(1, globalState); 
        
        Serial.println("Toggling Govee Strip over BLE...");
        if(globalState) {
            pChar->writeValue(powerOn, sizeof(powerOn), true);
        } else {
            pChar->writeValue(powerOff, sizeof(powerOff), true);
        }
        
        Serial.println("Both devices toggled.");
        
        // Wait 2 seconds before the next toggle
        delay(2000); 
        
    } else {
        // BLE is not connected. Try to connect.
        if (!connectToGovee()) {
            Serial.println("Will retry BLE connection in 5 seconds...");
            delay(5000); 
        }
    }
}