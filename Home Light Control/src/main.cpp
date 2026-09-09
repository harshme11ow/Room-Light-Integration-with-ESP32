#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <mbedtls/aes.h> 

// --- Wi-Fi Credentials ---
const char* ssid = "MySpectrumWiFi02-2G";
const char* password = "recentnews374";

// --- Status LED ---
#define STATUS_LED_PIN 2 // Default onboard LED for ESP32 DevKit V1

// --- Govee BLE MAC Addresses ---
#define GOVEE_STRIP_MAC "d3:21:c6:46:0d:46" 
#define GOVEE_BARS_MAC  "e1:de:81:46:66:19" 

static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

// --- Feit Tuya Plugs Config ---
IPAddress tuyaPlugIP1(192, 168, 1, 31);
const char* tuyaPlugID1 = "ebf9fa6de05d0ece56ecfw"; // Shelf Lights
const char* tuyaPlugKey1 = "6n~O6wS-Hzk+sPyj";

IPAddress tuyaPlugIP2(192, 168, 1, 33);
const char* tuyaPlugID2 = "ebb7d988301f646e51npsj"; // Lamp Light
const char* tuyaPlugKey2 = "MGH`N!_hX8gP|wkv";

// --- Feit Tuya Bulbs Config ---
IPAddress tuyaBulbIP1(192, 168, 1, 36);
const char* tuyaBulbID1 = "eb4f2a34b49a57791cdam6"; // Dining Room 1
const char* tuyaBulbKey1 = "*]0X4r^dyn6stGjr";

IPAddress tuyaBulbIP2(192, 168, 1, 35);
const char* tuyaBulbID2 = "eb8fd032caddb07e315rom"; // Dining Room 3
const char* tuyaBulbKey2 = "eUD.dUzP^Mu`F#K>";

IPAddress tuyaBulbIP3(192, 168, 1, 34);
const char* tuyaBulbID3 = "eb446bf1e41ba6d37dudft"; // Tall Barlast 1
const char* tuyaBulbKey3 = "~]S~=}LKRa!stFL0";

// --- Hardware Button Configuration ---
#define BUTTON_1_PIN 4   // Plugs & Govee ON/OFF
#define BUTTON_2_PIN 5   // Smart Bulbs ON/OFF
#define BUTTON_3_PIN 18  // Scene Controller (Color Override)
#define CASCADE_DELAY 300 

bool isExecuting = false; 
const unsigned long debounceDelay = 50;

// Button States
bool globalState1 = false; bool button1State = HIGH; bool lastBtn1 = HIGH; unsigned long debounceTime1 = 0;
bool globalState2 = false; bool button2State = HIGH; bool lastBtn2 = HIGH; unsigned long debounceTime2 = 0;
bool sceneState   = false; bool button3State = HIGH; bool lastBtn3 = HIGH; unsigned long debounceTime3 = 0;

// =========================================================================
// CORE PRIMITIVES (Handles encryption and radio delivery automatically)
// =========================================================================
uint32_t getTuyaCRC(const uint8_t *data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return ~crc;
}

// Base function for all Tuya TCP Data
bool sendTuyaPayload(IPAddress ip, const char* devId, const char* localKey, String dpsJson) {
    WiFiClient client;
    client.setTimeout(1); 
    if (!client.connect(ip, 6668, 1000)) return false;

    String json = "{\"devId\":\"" + String(devId) + "\",\"uid\":\"\",\"t\":\"1600000000\",\"dps\":{" + dpsJson + "}}";
    size_t plainLen = json.length();
    size_t padAmt = 16 - (plainLen % 16);
    size_t paddedLen = plainLen + padAmt;
    
    uint8_t plaintext[paddedLen];
    memcpy(plaintext, json.c_str(), plainLen);
    for (size_t i = plainLen; i < paddedLen; i++) plaintext[i] = padAmt;

    uint8_t ciphertext[paddedLen];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const uint8_t*)localKey, 128);
    for (size_t i = 0; i < paddedLen; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plaintext + i, ciphertext + i);
    }
    mbedtls_aes_free(&aes);

    uint32_t packetLen = 15 + paddedLen + 8; 
    size_t totalSize = 16 + 15 + paddedLen + 8; 
    uint8_t packet[totalSize];
    memset(packet, 0, totalSize);

    packet[0] = 0x00; packet[1] = 0x00; packet[2] = 0x55; packet[3] = 0xAA;
    packet[11] = 0x07; 
    packet[12] = (packetLen >> 24) & 0xFF; packet[13] = (packetLen >> 16) & 0xFF;
    packet[14] = (packetLen >> 8) & 0xFF;  packet[15] = packetLen & 0xFF;
    packet[16] = '3'; packet[17] = '.'; packet[18] = '3';
    
    memcpy(&packet[31], ciphertext, paddedLen);
    uint32_t crc = getTuyaCRC(&packet[4], 27 + paddedLen);
    int crcIdx = 31 + paddedLen;
    packet[crcIdx] = (crc >> 24) & 0xFF;     packet[crcIdx+1] = (crc >> 16) & 0xFF;
    packet[crcIdx+2] = (crc >> 8) & 0xFF;    packet[crcIdx+3] = crc & 0xFF;
    packet[crcIdx+4] = 0x00; packet[crcIdx+5] = 0x00; packet[crcIdx+6] = 0xAA; packet[crcIdx+7] = 0x55;

    client.write(packet, totalSize);
    client.stop();
    return true;
}

// Base function for all Govee BLE Data
bool sendGoveeBLE(const char* macAddress, uint8_t* payload, size_t length) {
    BLEAddress goveeAddress(macAddress, 1);
    NimBLEClient* pClient = NimBLEDevice::createClient();
    pClient->setConnectTimeout(1); 
    
    bool success = false;
    if(pClient->connect(goveeAddress)) {
        BLERemoteService* pService = pClient->getService(serviceUUID);
        if(pService != nullptr) {
            BLERemoteCharacteristic* pChar = pService->getCharacteristic(charUUID);
            if(pChar != nullptr) {
                pChar->writeValue(payload, length, false);
                success = true;
            }
        }
        delay(50);
        pClient->disconnect();
    }
    NimBLEDevice::deleteClient(pClient);
    return success;
}

// =========================================================================
// ACTION WRAPPERS 
// =========================================================================
bool toggleTuyaPlug(IPAddress ip, const char* id, const char* key, bool state) {
    return sendTuyaPayload(ip, id, key, "\"1\":" + String(state ? "true" : "false"));
}

bool toggleTuyaBulb(IPAddress ip, const char* id, const char* key, bool state) {
    return sendTuyaPayload(ip, id, key, "\"20\":" + String(state ? "true" : "false"));
}

bool setTuyaBulbColor(IPAddress ip, const char* id, const char* key, String payload) {
    return sendTuyaPayload(ip, id, key, payload);
}

bool toggleGovee(const char* mac, bool state) {
    return sendGoveeBLE(mac, state ? powerOn : powerOff, 20);
}

bool setGoveeColor(const char* mac, uint8_t r, uint8_t g, uint8_t b) {
    uint8_t packet[20] = {0x33, 0x05, 0x02, r, g, b, 0x00, 0xFF, 0,0,0,0,0,0,0,0,0,0,0,0};
    uint8_t checksum = 0;
    for(int i = 0; i < 19; i++) checksum ^= packet[i];
    packet[19] = checksum; // Calculate Govee XOR Checksum dynamically
    return sendGoveeBLE(mac, packet, 20);
}

// =========================================================================
// SETUP & LOOP
// =========================================================================
void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);
    pinMode(BUTTON_3_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
    
    digitalWrite(STATUS_LED_PIN, LOW); // Start with LED off
    
    Serial.print("Connecting to Wi-Fi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");

    // --- Wi-Fi Success Sequence: Blink Red LED 3 Times ---
    for (int i = 0; i < 3; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH);
        delay(200);
        digitalWrite(STATUS_LED_PIN, LOW);
        delay(200);
    }

    NimBLEDevice::init("");
    Serial.println("ESP32 Ready. Press buttons to trigger lights.");
}

void loop() {
    unsigned long currentMillis = millis();
    
    // ==========================================
    // BUTTON 1 LOGIC (Plugs & Govee)
    // ==========================================
    bool reading1 = digitalRead(BUTTON_1_PIN);
    if (reading1 != lastBtn1) debounceTime1 = currentMillis;
    if ((currentMillis - debounceTime1) > debounceDelay && reading1 != button1State) {
        button1State = reading1;
        if (button1State == LOW && !isExecuting) {
            isExecuting = true;
            globalState1 = !globalState1;
            Serial.printf("\n--- B1 PRESSED: %s ---\n", globalState1 ? "ON" : "OFF");
            
            bool p1 = toggleTuyaPlug(tuyaPlugIP1, tuyaPlugID1, tuyaPlugKey1, globalState1); if(p1) delay(CASCADE_DELAY);
            bool p2 = toggleTuyaPlug(tuyaPlugIP2, tuyaPlugID2, tuyaPlugKey2, globalState1); if(p2) delay(CASCADE_DELAY);
            bool s1 = toggleGovee(GOVEE_STRIP_MAC, globalState1); if(s1) delay(CASCADE_DELAY);
            bool s2 = toggleGovee(GOVEE_BARS_MAC, globalState1);  if(s2) delay(CASCADE_DELAY);
            
            int retries = 0;
            while ((!p1 || !p2 || !s1 || !s2) && retries < 25) {
                retries++;
                if (!p1) p1 = toggleTuyaPlug(tuyaPlugIP1, tuyaPlugID1, tuyaPlugKey1, globalState1);
                if (!p2) p2 = toggleTuyaPlug(tuyaPlugIP2, tuyaPlugID2, tuyaPlugKey2, globalState1);
                if (!s1) s1 = toggleGovee(GOVEE_STRIP_MAC, globalState1);
                if (!s2) s2 = toggleGovee(GOVEE_BARS_MAC, globalState1);
                if (!p1 || !p2 || !s1 || !s2) delay(500); 
            }
            isExecuting = false;
        }
    }
    lastBtn1 = reading1;

    // ==========================================
    // BUTTON 2 LOGIC (Smart Bulbs ON/OFF)
    // ==========================================
    bool reading2 = digitalRead(BUTTON_2_PIN);
    if (reading2 != lastBtn2) debounceTime2 = currentMillis;
    if ((currentMillis - debounceTime2) > debounceDelay && reading2 != button2State) {
        button2State = reading2;
        if (button2State == LOW && !isExecuting) {
            isExecuting = true;
            globalState2 = !globalState2;
            Serial.printf("\n--- B2 PRESSED: %s ---\n", globalState2 ? "ON" : "OFF");
            
            bool b1 = toggleTuyaBulb(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, globalState2); if(b1) delay(CASCADE_DELAY);
            bool b2 = toggleTuyaBulb(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, globalState2); if(b2) delay(CASCADE_DELAY);
            bool b3 = toggleTuyaBulb(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, globalState2); if(b3) delay(CASCADE_DELAY);
            
            int retries = 0;
            while ((!b1 || !b2 || !b3) && retries < 25) {
                retries++;
                if (!b1) b1 = toggleTuyaBulb(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, globalState2);
                if (!b2) b2 = toggleTuyaBulb(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, globalState2);
                if (!b3) b3 = toggleTuyaBulb(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, globalState2);
                if (!b1 || !b2 || !b3) delay(500); 
            }
            isExecuting = false;
        }
    }
    lastBtn2 = reading2;

    // ==========================================
    // BUTTON 3 LOGIC (Scene Controller)
    // ==========================================
    bool reading3 = digitalRead(BUTTON_3_PIN);
    if (reading3 != lastBtn3) debounceTime3 = currentMillis;
    if ((currentMillis - debounceTime3) > debounceDelay && reading3 != button3State) {
        button3State = reading3;
        if (button3State == LOW && !isExecuting) {
            isExecuting = true;
            sceneState = !sceneState; // Toggle between Warm White (Work) and Deep Blue (Ambient)
            Serial.printf("\n--- B3 PRESSED: %s ---\n", sceneState ? "WORK MODE" : "AMBIENT MODE");
            
            // Tuya Scene Payloads: Forces power ON (DP 20) while applying Color Mode (DP 21)
            // Warm White: Temp 0, Bright 1000.  Deep Blue: Hue 240 (00F0), Sat 1000, Val 1000
            String tuyaScene = sceneState ? "\"20\":true,\"21\":\"white\",\"22\":1000,\"23\":0" 
                                          : "\"20\":true,\"21\":\"colour\",\"24\":\"00F003e803e8\"";
            
            // Govee RGB Conversions
            uint8_t r = sceneState ? 255 : 0;
            uint8_t g = sceneState ? 214 : 0;
            uint8_t b = sceneState ? 170 : 255;

            bool c1 = setTuyaBulbColor(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, tuyaScene); if(c1) delay(CASCADE_DELAY);
            bool c2 = setTuyaBulbColor(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, tuyaScene); if(c2) delay(CASCADE_DELAY);
            bool c3 = setTuyaBulbColor(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, tuyaScene); if(c3) delay(CASCADE_DELAY);
            bool c4 = setGoveeColor(GOVEE_STRIP_MAC, r, g, b);                             if(c4) delay(CASCADE_DELAY);
            bool c5 = setGoveeColor(GOVEE_BARS_MAC, r, g, b);                              if(c5) delay(CASCADE_DELAY);
            
            int retries = 0;
            while ((!c1 || !c2 || !c3 || !c4 || !c5) && retries < 25) {
                retries++;
                if (!c1) c1 = setTuyaBulbColor(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, tuyaScene);
                if (!c2) c2 = setTuyaBulbColor(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, tuyaScene);
                if (!c3) c3 = setTuyaBulbColor(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, tuyaScene);
                if (!c4) c4 = setGoveeColor(GOVEE_STRIP_MAC, r, g, b);
                if (!c5) c5 = setGoveeColor(GOVEE_BARS_MAC, r, g, b);
                if (!c1 || !c2 || !c3 || !c4 || !c5) delay(500); 
            }
            isExecuting = false;
        }
    }
    lastBtn3 = reading3;
}