#include <Arduino.h>
#include <NimBLEDevice.h>

// Replace with your Govee Light Strip's MAC Address
#define GOVEE_MAC "AA:BB:CC:DD:EE:FF" 

// Set to 37 if testing on an M5StickC Plus2 (Button A), or your chosen GPIO pin
#define BUTTON_PIN 37 

static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

// Govee 20-byte Hex Payloads
uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

// Variables for toggle state and button debouncing
bool lightState = false; 
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50; 

void toggleGoveeLight() {
    Serial.printf("Connecting to Govee at %s...\n", GOVEE_MAC);
    BLEAddress goveeAddress(GOVEE_MAC);
    
    // Create client
    NimBLEClient* pClient = NimBLEDevice::createClient();
    
    if(pClient->connect(goveeAddress)) {
        Serial.println("Connected to Govee!");
        
        BLERemoteService* pService = pClient->getService(serviceUUID);
        if(pService != nullptr) {
            BLERemoteCharacteristic* pChar = pService->getCharacteristic(charUUID);
            if(pChar != nullptr) {
                
                // Toggle the state and write the payload
                lightState = !lightState;
                if(lightState) {
                    pChar->writeValue(powerOn, sizeof(powerOn), true);
                    Serial.println("Sent Power ON command.");
                } else {
                    pChar->writeValue(powerOff, sizeof(powerOff), true);
                    Serial.println("Sent Power OFF command.");
                }
            } else {
                Serial.println("Failed to find characteristic.");
            }
        } else {
            Serial.println("Failed to find service.");
        }
        
        // Disconnect after sending to free up resources 
        pClient->disconnect();
        Serial.println("Disconnected.");
    } else {
        Serial.println("Connection failed. Is the device powered on and in range?");
    }
    
    // Clean up memory
    NimBLEDevice::deleteClient(pClient);
}

void setup() {
    Serial.begin(115200);
    
    // Use INPUT_PULLUP if your button connects the pin to GND when pressed
    // Note: The M5StickC Plus2 button requires pulling HIGH, so INPUT_PULLUP is correct here too.
    pinMode(BUTTON_PIN, INPUT_PULLUP); 
    
    // Initialize BLE
    NimBLEDevice::init("");
    Serial.println("ESP32 Ready. Press the button to toggle the light.");
}

void loop() {
    // Read the button state
    bool reading = digitalRead(BUTTON_PIN);
    
    // Check for state change to reset debounce timer
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }
    
    // Evaluate if enough time has passed to consider the press valid
    if ((millis() - lastDebounceTime) > debounceDelay) {
        // Trigger action on the falling edge (button pressed down)
        if (reading == LOW && lastButtonState == HIGH) {
            toggleGoveeLight();
        }
    }
    
    // Save the current state for the next loop
    lastButtonState = reading;
}