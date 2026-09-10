#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <mbedtls/aes.h> 

// --- OLED Display Libraries ---
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Wi-Fi Credentials ---
const char* ssid = "MySpectrumWiFi02-2G";
const char* password = "recentnews374";

#define STATUS_LED_PIN 2

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- Govee BLE MAC Addresses ---
#define GOVEE_STRIP_MAC "d3:21:c6:46:0d:46" 
#define GOVEE_BARS_MAC  "AA:BB:CC:DD:EE:FF" 

static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

// --- Feit Tuya Plugs ---
IPAddress tuyaPlugIP1(192, 168, 1, 31);
const char* tuyaPlugID1 = "ebf9fa6de05d0ece56ecfw"; // Shelf Lights
const char* tuyaPlugKey1 = "6n~O6wS-Hzk+sPyj";

IPAddress tuyaPlugIP2(192, 168, 1, 33);
const char* tuyaPlugID2 = "ebb7d988301f646e51npsj"; // Lamp Light
const char* tuyaPlugKey2 = "MGH`N!_hX8gP|wkv";

IPAddress tuyaPlugIP3(192, 168, 1, 38);
const char* tuyaPlugID3 = "eb1390e08784797c244bqy"; // Bedroom Lamps
const char* tuyaPlugKey3 = "4iA^5evDX$HUy}xZ";

// --- Feit Tuya Bulbs ---
IPAddress tuyaBulbIP1(192, 168, 1, 36);
const char* tuyaBulbID1 = "eb4f2a34b49a57791cdam6"; // Dining Room 1
const char* tuyaBulbKey1 = "*]0X4r^dyn6stGjr";

IPAddress tuyaBulbIP2(192, 168, 1, 35);
const char* tuyaBulbID2 = "eb8fd032caddb07e315rom"; // Dining Room 3
const char* tuyaBulbKey2 = "eUD.dUzP^Mu`F#K>";

IPAddress tuyaBulbIP3(192, 168, 1, 34);
const char* tuyaBulbID3 = "eb446bf1e41ba6d37dudft"; // Tall Barlast 1
const char* tuyaBulbKey3 = "~]S~=}LKRa!stFL0";

IPAddress tuyaBulbIP4(192, 168, 1, 37);
const char* tuyaBulbID4 = "eb4370caacc3241b81sknb"; // TV stand lights
const char* tuyaBulbKey4 = "tB:z[5ymGy8f_=SH"; 

// --- Hardware Button Configuration ---
#define BUTTON_1_PIN 4   
#define BUTTON_2_PIN 5   
#define BUTTON_3_PIN 18  
#define CASCADE_DELAY 300 

const unsigned long debounceDelay = 50;

// Button 1 States & Queue
bool globalState1 = false; bool btn1State = HIGH; bool lastBtn1 = HIGH; unsigned long dbTime1 = 0;
bool b1_p1_ok = true, b1_p2_ok = true, b1_p3_ok = true, b1_s1_ok = true, b1_s2_ok = true;
bool b1_b1_ok = true, b1_b2_ok = true, b1_b3_ok = true, b1_b4_ok = true;
int b1_retries = 25; unsigned long b1_lastRetry = 0;

// Button 2 States & Queue
bool globalState2 = false; bool btn2State = HIGH; bool lastBtn2 = HIGH; unsigned long dbTime2 = 0;
bool b2_b1_ok = true, b2_b2_ok = true, b2_b3_ok = true, b2_b4_ok = true;
int b2_retries = 25; unsigned long b2_lastRetry = 0;

// Button 3 States & Queue
bool sceneState = false; bool btn3State = HIGH; bool lastBtn3 = HIGH; unsigned long dbTime3 = 0;
bool b3_c1_ok = true, b3_c2_ok = true, b3_c3_ok = true, b3_c4_ok = true, b3_c5_ok = true, b3_c6_ok = true;
int b3_retries = 25; unsigned long b3_lastRetry = 0;

// =========================================================================
// OLED UI ENGINE
// =========================================================================
unsigned long lastOLEDUpdate = 0;

void updateOLED(String title, String line1, String line2, bool isIdle = false) {
    if (!isIdle) lastOLEDUpdate = millis();
    
    display.clearDisplay();
    
    // Header Background
    display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
    display.setTextSize(1);
    
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor((128 - w) / 2, 3);
    display.print(title);
    
    // Body Text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 22);
    display.print(line1);
    display.setCursor(0, 36);
    display.print(line2);
    
    display.display();
}

// =========================================================================
// CORE PRIMITIVES
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
    packet[19] = checksum;
    return sendGoveeBLE(mac, packet, 20);
}

void flashFailure() {
    for (int i = 0; i < 10; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH); delay(75);
        digitalWrite(STATUS_LED_PIN, LOW); delay(75);
    }
}

// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);
    pinMode(BUTTON_3_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Initialize OLED Display
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 OLED allocation failed."));
    }
    updateOLED("BOOT SEQUENCE", "Connecting Wi-Fi...", "");

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(500); }
    
    updateOLED("BOOT SEQUENCE", "Wi-Fi Connected!", "Init Radio...");

    for (int i = 0; i < 3; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH); delay(200);
        digitalWrite(STATUS_LED_PIN, LOW); delay(200);
    }

    NimBLEDevice::init("");
    
    updateOLED("SYSTEM IDLE", "All nodes synced.", "Awaiting command...", true);
    lastOLEDUpdate = 0;
}

// =========================================================================
// MAIN LOOP (Non-Blocking)
// =========================================================================
void loop() {
    unsigned long currentMillis = millis();
    
    if (currentMillis - lastOLEDUpdate > 15000 && lastOLEDUpdate != 0) {
        updateOLED("SYSTEM IDLE", "All nodes synced.", "Awaiting command...", true);
        lastOLEDUpdate = 0; 
    }

    // --- BUTTON 1 TRIGGER (Master Control) ---
    bool reading1 = digitalRead(BUTTON_1_PIN);
    if (reading1 != lastBtn1) dbTime1 = currentMillis;
    if ((currentMillis - dbTime1) > debounceDelay && reading1 != btn1State) {
        btn1State = reading1;
        if (btn1State == LOW) {
            globalState1 = !globalState1;
            globalState2 = globalState1; // Sync Button 2 so bulbs don't double-toggle
            
            b1_p1_ok = b1_p2_ok = b1_p3_ok = b1_s1_ok = b1_s2_ok = false;
            b1_b1_ok = b1_b2_ok = b1_b3_ok = b1_b4_ok = false;
            b1_retries = 0; b1_lastRetry = 0;
            digitalWrite(STATUS_LED_PIN, HIGH);
            
            updateOLED("MASTER CONTROL", "Target: " + String(globalState1 ? "ON" : "OFF"), "Transmitting...");
        }
    }
    lastBtn1 = reading1;

    // --- BUTTON 1 BACKGROUND QUEUE ---
    if ((!b1_p1_ok || !b1_p2_ok || !b1_p3_ok || !b1_s1_ok || !b1_s2_ok || !b1_b1_ok || !b1_b2_ok || !b1_b3_ok || !b1_b4_ok) && b1_retries < 25) {
        if (currentMillis - b1_lastRetry >= 500) {
            b1_lastRetry = currentMillis;
            b1_retries++;
            
            if (b1_retries > 1) updateOLED("MASTER CONTROL", "Network degraded.", "Retry Sweep: " + String(b1_retries));
            
            if (!b1_p1_ok) { b1_p1_ok = toggleTuyaPlug(tuyaPlugIP1, tuyaPlugID1, tuyaPlugKey1, globalState1); if (b1_p1_ok) delay(CASCADE_DELAY); }
            if (!b1_p2_ok) { b1_p2_ok = toggleTuyaPlug(tuyaPlugIP2, tuyaPlugID2, tuyaPlugKey2, globalState1); if (b1_p2_ok) delay(CASCADE_DELAY); }
            if (!b1_p3_ok) { b1_p3_ok = toggleTuyaPlug(tuyaPlugIP3, tuyaPlugID3, tuyaPlugKey3, globalState1); if (b1_p3_ok) delay(CASCADE_DELAY); }
            
            if (!b1_b1_ok) { b1_b1_ok = toggleTuyaBulb(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, globalState1); if (b1_b1_ok) delay(CASCADE_DELAY); }
            if (!b1_b2_ok) { b1_b2_ok = toggleTuyaBulb(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, globalState1); if (b1_b2_ok) delay(CASCADE_DELAY); }
            if (!b1_b3_ok) { b1_b3_ok = toggleTuyaBulb(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, globalState1); if (b1_b3_ok) delay(CASCADE_DELAY); }
            if (!b1_b4_ok) { b1_b4_ok = toggleTuyaBulb(tuyaBulbIP4, tuyaBulbID4, tuyaBulbKey4, globalState1); if (b1_b4_ok) delay(CASCADE_DELAY); }

            if (!b1_s1_ok) { b1_s1_ok = toggleGovee(GOVEE_STRIP_MAC, globalState1); if (b1_s1_ok) delay(CASCADE_DELAY); }
            if (!b1_s2_ok) { b1_s2_ok = toggleGovee(GOVEE_BARS_MAC, globalState1); if (b1_s2_ok) delay(CASCADE_DELAY); }

            if (b1_p1_ok && b1_p2_ok && b1_p3_ok && b1_s1_ok && b1_s2_ok && b1_b1_ok && b1_b2_ok && b1_b3_ok && b1_b4_ok) {
                digitalWrite(STATUS_LED_PIN, LOW);
                updateOLED("MASTER CONTROL", "TX Complete!", "Status: " + String(globalState1 ? "ON" : "OFF"));
            } else if (b1_retries >= 25) {
                flashFailure();
                updateOLED("ERROR!", "Max Retries Hit", "Check device power.");
            }
        }
    }

    // --- BUTTON 2 TRIGGER (Bulbs Only) ---
    bool reading2 = digitalRead(BUTTON_2_PIN);
    if (reading2 != lastBtn2) dbTime2 = currentMillis;
    if ((currentMillis - dbTime2) > debounceDelay && reading2 != btn2State) {
        btn2State = reading2;
        if (btn2State == LOW) {
            globalState2 = !globalState2;
            b2_b1_ok = b2_b2_ok = b2_b3_ok = b2_b4_ok = false;
            b2_retries = 0; b2_lastRetry = 0;
            digitalWrite(STATUS_LED_PIN, HIGH);
            
            updateOLED("SMART BULBS", "Target: " + String(globalState2 ? "ON" : "OFF"), "Transmitting...");
        }
    }
    lastBtn2 = reading2;

    // --- BUTTON 2 BACKGROUND QUEUE ---
    if ((!b2_b1_ok || !b2_b2_ok || !b2_b3_ok || !b2_b4_ok) && b2_retries < 25) {
        if (currentMillis - b2_lastRetry >= 500) {
            b2_lastRetry = currentMillis;
            b2_retries++;
            
            if (b2_retries > 1) updateOLED("SMART BULBS", "Network degraded.", "Retry Sweep: " + String(b2_retries));
            
            if (!b2_b1_ok) { b2_b1_ok = toggleTuyaBulb(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, globalState2); if (b2_b1_ok) delay(CASCADE_DELAY); }
            if (!b2_b2_ok) { b2_b2_ok = toggleTuyaBulb(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, globalState2); if (b2_b2_ok) delay(CASCADE_DELAY); }
            if (!b2_b3_ok) { b2_b3_ok = toggleTuyaBulb(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, globalState2); if (b2_b3_ok) delay(CASCADE_DELAY); }
            if (!b2_b4_ok) { b2_b4_ok = toggleTuyaBulb(tuyaBulbIP4, tuyaBulbID4, tuyaBulbKey4, globalState2); if (b2_b4_ok) delay(CASCADE_DELAY); }

            if (b2_b1_ok && b2_b2_ok && b2_b3_ok && b2_b4_ok) {
                digitalWrite(STATUS_LED_PIN, LOW);
                updateOLED("SMART BULBS", "TX Complete!", "Status: " + String(globalState2 ? "ON" : "OFF"));
            } else if (b2_retries >= 25) {
                flashFailure();
                updateOLED("ERROR!", "Max Retries Hit", "Check device power.");
            }
        }
    }

    // --- BUTTON 3 TRIGGER (Scenes) ---
    bool reading3 = digitalRead(BUTTON_3_PIN);
    if (reading3 != lastBtn3) dbTime3 = currentMillis;
    if ((currentMillis - dbTime3) > debounceDelay && reading3 != btn3State) {
        btn3State = reading3;
        if (btn3State == LOW) {
            sceneState = !sceneState;
            b3_c1_ok = b3_c2_ok = b3_c3_ok = b3_c4_ok = b3_c5_ok = b3_c6_ok = false;
            b3_retries = 0; b3_lastRetry = 0;
            digitalWrite(STATUS_LED_PIN, HIGH);
            
            updateOLED("SCENE CONTROL", "Target: " + String(sceneState ? "WORK MODE" : "AMBIENT MODE"), "Transmitting...");
        }
    }
    lastBtn3 = reading3;

    // --- BUTTON 3 BACKGROUND QUEUE ---
    if ((!b3_c1_ok || !b3_c2_ok || !b3_c3_ok || !b3_c4_ok || !b3_c5_ok || !b3_c6_ok) && b3_retries < 25) {
        if (currentMillis - b3_lastRetry >= 500) {
            b3_lastRetry = currentMillis;
            b3_retries++;

            if (b3_retries > 1) updateOLED("SCENE CONTROL", "Network degraded.", "Retry Sweep: " + String(b3_retries));

            // Purple Tuya Payload Hex = 0118 (Hue 280), 03e8 (Sat 100%), 03e8 (Val 100%)
            String tuyaScene = sceneState ? "\"20\":true,\"21\":\"white\",\"22\":1000,\"23\":0" 
                                          : "\"20\":true,\"21\":\"colour\",\"24\":\"011803e803e8\"";
            
            // Purple Govee Payload (RGB)
            uint8_t r = sceneState ? 255 : 150;
            uint8_t g = sceneState ? 214 : 0;
            uint8_t b = sceneState ? 170 : 255;

            if (!b3_c1_ok) { b3_c1_ok = setTuyaBulbColor(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, tuyaScene); if (b3_c1_ok) delay(CASCADE_DELAY); }
            if (!b3_c2_ok) { b3_c2_ok = setTuyaBulbColor(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, tuyaScene); if (b3_c2_ok) delay(CASCADE_DELAY); }
            if (!b3_c3_ok) { b3_c3_ok = setTuyaBulbColor(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, tuyaScene); if (b3_c3_ok) delay(CASCADE_DELAY); }
            if (!b3_c4_ok) { b3_c4_ok = setTuyaBulbColor(tuyaBulbIP4, tuyaBulbID4, tuyaBulbKey4, tuyaScene); if (b3_c4_ok) delay(CASCADE_DELAY); }
            if (!b3_c5_ok) { b3_c5_ok = setGoveeColor(GOVEE_STRIP_MAC, r, g, b);                             if (b3_c5_ok) delay(CASCADE_DELAY); }
            if (!b3_c6_ok) { b3_c6_ok = setGoveeColor(GOVEE_BARS_MAC, r, g, b);                              if (b3_c6_ok) delay(CASCADE_DELAY); }

            if (b3_c1_ok && b3_c2_ok && b3_c3_ok && b3_c4_ok && b3_c5_ok && b3_c6_ok) {
                digitalWrite(STATUS_LED_PIN, LOW);
                updateOLED("SCENE CONTROL", "TX Complete!", "Status: " + String(sceneState ? "WORK MODE" : "AMBIENT MODE"));
            } else if (b3_retries >= 25) {
                flashFailure();
                updateOLED("ERROR!", "Max Retries Hit", "Check device power.");
            }
        }
    }
}