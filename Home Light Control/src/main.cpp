#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <mbedtls/aes.h> 

// --- OLED & DHT Libraries ---
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// --- Wi-Fi Credentials ---
const char* ssid = "harsh";
const char* password = "bruhlmao";

// --- Status LED ---
#define STATUS_LED_PIN 2

// --- OLED Setup ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- DHT11 Setup ---
#define DHTPIN 15  // Moved to GPIO 15
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// --- Govee BLE MAC Addresses ---
#define GOVEE_STRIP_MAC "d3:21:c6:46:0d:46" 
#define GOVEE_BARS_MAC  "e1:de:81:46:66:19" 

static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

// --- Feit Tuya Plugs ---
IPAddress tuyaPlugIP1(192, 168, 86, 98); // Shelf Lights
const char* tuyaPlugID1 = "ebf9fa6de05d0ece56ecfw"; 
const char* tuyaPlugKey1 = "xe0-aeL`3B/P`5e}";

IPAddress tuyaPlugIP2(192, 168, 86, 95); // Lamp Light
const char* tuyaPlugID2 = "ebb7d988301f646e51npsj"; 
const char* tuyaPlugKey2 = "Q1n1Xt=VBFJPEhd5";

IPAddress tuyaPlugIP3(192, 168, 86, 99); // Bedroom Lamps
const char* tuyaPlugID3 = "eb1390e08784797c244bqy"; 
const char* tuyaPlugKey3 = "0Ubh9U6&r[|`&Ktg";

// --- Feit Tuya Bulbs ---
IPAddress tuyaBulbIP1(192, 168, 86, 93); // Dining Room 1
const char* tuyaBulbID1 = "eb4f2a34b49a57791cdam6"; 
const char* tuyaBulbKey1 = "Pu!iegYR*s@p=kNR";

IPAddress tuyaBulbIP2(192, 168, 86, 92); // Dining Room 3
const char* tuyaBulbID2 = "eb8fd032caddb07e315rom"; 
const char* tuyaBulbKey2 = "ag+4/mcs/JkM.5;k";

IPAddress tuyaBulbIP3(192, 168, 86, 97); // Tall Barlast 1
const char* tuyaBulbID3 = "eb446bf1e41ba6d37dudft"; 
const char* tuyaBulbKey3 = "+)10B4WWwEg{UZPV";

IPAddress tuyaBulbIP4(192, 168, 86, 94); // TV stand lights
const char* tuyaBulbID4 = "eb4370caacc3241b81sknb"; 
const char* tuyaBulbKey4 = "y>.u1~n7R=3uOh&<"; 

// --- Hardware Button Configuration ---
#define BUTTON_1_PIN 4   // Master Control / Double Tap OFF
#define BUTTON_2_PIN 5   // Smart Bulbs / Double Tap DHT11
#define BUTTON_3_PIN 18  // Scene Control
#define BUTTON_4_PIN 19  // Gaming Mode
#define CASCADE_DELAY 300 

const unsigned long debounceDelay = 50;
const unsigned long doubleTapWindow = 300; 

// Button Click Trackers
int btn1_clicks = 0; unsigned long btn1_lastPress = 0; bool btn1State = HIGH; bool lastBtn1 = HIGH; unsigned long dbTime1 = 0;
int btn2_clicks = 0; unsigned long btn2_lastPress = 0; bool btn2State = HIGH; bool lastBtn2 = HIGH; unsigned long dbTime2 = 0;
int btn3_clicks = 0; unsigned long btn3_lastPress = 0; bool btn3State = HIGH; bool lastBtn3 = HIGH; unsigned long dbTime3 = 0;
int btn4_clicks = 0; unsigned long btn4_lastPress = 0; bool btn4State = HIGH; bool lastBtn4 = HIGH; unsigned long dbTime4 = 0;

// Button 1 Queue
bool globalState1 = false; 
bool b1_p1_ok = true, b1_p2_ok = true, b1_p3_ok = true, b1_s1_ok = true, b1_s2_ok = true;
bool b1_b1_ok = true, b1_b2_ok = true, b1_b3_ok = true, b1_b4_ok = true;
int b1_retries = 5; unsigned long b1_lastRetry = 0;

// Button 2 Queue
bool globalState2 = false; 
bool b2_b1_ok = true, b2_b2_ok = true, b2_b3_ok = true, b2_b4_ok = true;
int b2_retries = 5; unsigned long b2_lastRetry = 0;

// Button 3 Queue
bool sceneState = false; 
bool b3_c1_ok = true, b3_c2_ok = true, b3_c3_ok = true, b3_c4_ok = true, b3_c5_ok = true, b3_c6_ok = true;
int b3_retries = 5; unsigned long b3_lastRetry = 0;

// Button 4 Queue
bool globalState4 = false; 
bool b4_p1_ok = true, b4_p2_ok = true, b4_b4_ok = true; 
int b4_retries = 5; unsigned long b4_lastRetry = 0;


// =========================================================================
// OLED UI ENGINE
// =========================================================================
unsigned long lastOLEDUpdate = 0;

void updateOLED(String title, String line1, String line2) {
    lastOLEDUpdate = millis(); 
    
    display.clearDisplay();
    display.fillRect(0, 0, 128, 14, SSD1306_WHITE);
    display.setTextSize(1);
    
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor((128 - w) / 2, 3);
    display.print(title);
    
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


// =========================================================================
// SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_1_PIN, INPUT_PULLUP);
    pinMode(BUTTON_2_PIN, INPUT_PULLUP);
    pinMode(BUTTON_3_PIN, INPUT_PULLUP);
    pinMode(BUTTON_4_PIN, INPUT_PULLUP);
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    dht.begin();
    
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
    
    updateOLED("SYSTEM READY", "All nodes synced.", "Awaiting command...");
}


// =========================================================================
// MAIN LOOP (Non-Blocking)
// =========================================================================
void loop() {
    unsigned long currentMillis = millis();
    
    // --- 30-SECOND OLED POWER SAVER ---
    if (currentMillis - lastOLEDUpdate > 30000 && lastOLEDUpdate != 0) {
        display.clearDisplay(); 
        display.display();
        lastOLEDUpdate = 0;     
    }


    // =========================================================================
    // BUTTON 1 (MASTER / DOUBLE TAP OFF)
    // =========================================================================
    bool reading1 = digitalRead(BUTTON_1_PIN);
    if (reading1 != lastBtn1) dbTime1 = currentMillis;
    if ((currentMillis - dbTime1) > debounceDelay && reading1 != btn1State) {
        btn1State = reading1;
        if (btn1State == LOW) {
            btn1_clicks++;
            btn1_lastPress = currentMillis;
        }
    }
    lastBtn1 = reading1;

    if (btn1_clicks > 0 && (currentMillis - btn1_lastPress) > doubleTapWindow) {
        if (btn1_clicks == 1) {
            // SINGLE TAP -> Toggle
            globalState1 = !globalState1;
            updateOLED("MASTER CONTROL", "Target: " + String(globalState1 ? "ON" : "OFF"), "Transmitting...");
        } else if (btn1_clicks >= 2) {
            // DOUBLE TAP -> Force Master OFF
            globalState1 = false;
            updateOLED("MASTER CONTROL", "Double Tap Sync", "Forcing ALL OFF...");
        }
        
        globalState2 = globalState1; 
        globalState4 = globalState1; 
        
        b1_p1_ok = b1_p2_ok = b1_p3_ok = b1_s1_ok = b1_s2_ok = false;
        b1_b1_ok = b1_b2_ok = b1_b3_ok = b1_b4_ok = false;
        b1_retries = 0; b1_lastRetry = 0;
        btn1_clicks = 0;
    }

    if ((!b1_p1_ok || !b1_p2_ok || !b1_p3_ok || !b1_s1_ok || !b1_s2_ok || !b1_b1_ok || !b1_b2_ok || !b1_b3_ok || !b1_b4_ok) && b1_retries < 5) {
        if (currentMillis - b1_lastRetry >= 500) {
            b1_lastRetry = currentMillis;
            b1_retries++;
            
            if (b1_retries > 1) {
                String failDev = "";
                if (!b1_p1_ok) failDev = "Shelf Lights";
                else if (!b1_p2_ok) failDev = "Lamp Light";
                else if (!b1_p3_ok) failDev = "Bedroom Lamps";
                else if (!b1_b1_ok) failDev = "Dining Rm 1";
                else if (!b1_b2_ok) failDev = "Dining Rm 3";
                else if (!b1_b3_ok) failDev = "Tall Barlast";
                else if (!b1_b4_ok) failDev = "TV Stand";
                else if (!b1_s1_ok) failDev = "Govee Strip";
                else if (!b1_s2_ok) failDev = "Govee Bars";
                
                updateOLED("RETRY " + String(b1_retries) + "/5", "Failing Device:", failDev);
            }
            
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
                updateOLED("MASTER CONTROL", "TX Complete!", "Status: " + String(globalState1 ? "ON" : "OFF"));
            } else if (b1_retries >= 5) {
                updateOLED("TX ABORTED", "Max retries hit.", "Returning to idle...");
                
                // Flushes the queue by forcing everything to OK so the ESP32 doesn't freeze
                b1_p1_ok = b1_p2_ok = b1_p3_ok = b1_s1_ok = b1_s2_ok = true; 
                b1_b1_ok = b1_b2_ok = b1_b3_ok = b1_b4_ok = true; 
            }
        }
    }


    // =========================================================================
    // BUTTON 2 (BULBS ONLY / DOUBLE TAP DHT11)
    // =========================================================================
    bool reading2 = digitalRead(BUTTON_2_PIN);
    if (reading2 != lastBtn2) dbTime2 = currentMillis;
    if ((currentMillis - dbTime2) > debounceDelay && reading2 != btn2State) {
        btn2State = reading2;
        if (btn2State == LOW) {
            btn2_clicks++;
            btn2_lastPress = currentMillis;
        }
    }
    lastBtn2 = reading2;

    if (btn2_clicks > 0 && (currentMillis - btn2_lastPress) > doubleTapWindow) {
        if (btn2_clicks == 1) {
            // SINGLE TAP -> Bulbs Only
            globalState2 = !globalState2;
            b2_b1_ok = b2_b2_ok = b2_b3_ok = b2_b4_ok = false;
            b2_retries = 0; b2_lastRetry = 0;
            updateOLED("SMART BULBS", "Target: " + String(globalState2 ? "ON" : "OFF"), "Transmitting...");
        } else if (btn2_clicks >= 2) {
            // DOUBLE TAP -> DHT11 Readout
            float h = dht.readHumidity();
            float t = dht.readTemperature(true); // Fahrenheit
            
            if (isnan(h) || isnan(t)) {
                updateOLED("ROOM CLIMATE", "Sensor Error:", "Check DHT11 Wiring");
            } else {
                updateOLED("ROOM CLIMATE", "Temp: " + String(t, 1) + " F", "Humidity: " + String(h, 1) + " %");
            }
        }
        btn2_clicks = 0;
    }

    if ((!b2_b1_ok || !b2_b2_ok || !b2_b3_ok || !b2_b4_ok) && b2_retries < 5) {
        if (currentMillis - b2_lastRetry >= 500) {
            b2_lastRetry = currentMillis;
            b2_retries++;
            
            if (b2_retries > 1) {
                String failDev = "";
                if (!b2_b1_ok) failDev = "Dining Rm 1";
                else if (!b2_b2_ok) failDev = "Dining Rm 3";
                else if (!b2_b3_ok) failDev = "Tall Barlast";
                else if (!b2_b4_ok) failDev = "TV Stand";
                updateOLED("RETRY " + String(b2_retries) + "/5", "Failing Device:", failDev);
            }
            
            if (!b2_b1_ok) { b2_b1_ok = toggleTuyaBulb(tuyaBulbIP1, tuyaBulbID1, tuyaBulbKey1, globalState2); if (b2_b1_ok) delay(CASCADE_DELAY); }
            if (!b2_b2_ok) { b2_b2_ok = toggleTuyaBulb(tuyaBulbIP2, tuyaBulbID2, tuyaBulbKey2, globalState2); if (b2_b2_ok) delay(CASCADE_DELAY); }
            if (!b2_b3_ok) { b2_b3_ok = toggleTuyaBulb(tuyaBulbIP3, tuyaBulbID3, tuyaBulbKey3, globalState2); if (b2_b3_ok) delay(CASCADE_DELAY); }
            if (!b2_b4_ok) { b2_b4_ok = toggleTuyaBulb(tuyaBulbIP4, tuyaBulbID4, tuyaBulbKey4, globalState2); if (b2_b4_ok) delay(CASCADE_DELAY); }

            if (b2_b1_ok && b2_b2_ok && b2_b3_ok && b2_b4_ok) {
                updateOLED("SMART BULBS", "TX Complete!", "Status: " + String(globalState2 ? "ON" : "OFF"));
            } else if (b2_retries >= 5) {
                updateOLED("TX ABORTED", "Max retries hit.", "Returning to idle...");
                b2_b1_ok = b2_b2_ok = b2_b3_ok = b2_b4_ok = true;
            }
        }
    }


    // =========================================================================
    // BUTTON 3 (SCENES)
    // =========================================================================
    bool reading3 = digitalRead(BUTTON_3_PIN);
    if (reading3 != lastBtn3) dbTime3 = currentMillis;
    if ((currentMillis - dbTime3) > debounceDelay && reading3 != btn3State) {
        btn3State = reading3;
        if (btn3State == LOW) {
            btn3_clicks++;
            btn3_lastPress = currentMillis;
        }
    }
    lastBtn3 = reading3;

    if (btn3_clicks > 0 && (currentMillis - btn3_lastPress) > doubleTapWindow) {
        if (btn3_clicks == 1) {
            sceneState = !sceneState;
            b3_c1_ok = b3_c2_ok = b3_c3_ok = b3_c4_ok = b3_c5_ok = b3_c6_ok = false;
            b3_retries = 0; b3_lastRetry = 0;
            updateOLED("SCENE CONTROL", "Target: " + String(sceneState ? "WORK MODE" : "AMBIENT MODE"), "Transmitting...");
        }
        btn3_clicks = 0;
    }

    if ((!b3_c1_ok || !b3_c2_ok || !b3_c3_ok || !b3_c4_ok || !b3_c5_ok || !b3_c6_ok) && b3_retries < 5) {
        if (currentMillis - b3_lastRetry >= 500) {
            b3_lastRetry = currentMillis;
            b3_retries++;

            if (b3_retries > 1) {
                String failDev = "";
                if (!b3_c1_ok) failDev = "Dining Rm 1";
                else if (!b3_c2_ok) failDev = "Dining Rm 3";
                else if (!b3_c3_ok) failDev = "Tall Barlast";
                else if (!b3_c4_ok) failDev = "TV Stand";
                else if (!b3_c5_ok) failDev = "Govee Strip";
                else if (!b3_c6_ok) failDev = "Govee Bars";
                updateOLED("RETRY " + String(b3_retries) + "/5", "Failing Device:", failDev);
            }

            String tuyaScene = sceneState ? "\"20\":true,\"21\":\"white\",\"22\":1000,\"23\":0" 
                                          : "\"20\":true,\"21\":\"colour\",\"24\":\"011803e803e8\"";
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
                updateOLED("SCENE CONTROL", "TX Complete!", "Status: " + String(sceneState ? "WORK MODE" : "AMBIENT MODE"));
            } else if (b3_retries >= 5) {
                updateOLED("TX ABORTED", "Max retries hit.", "Returning to idle...");
                b3_c1_ok = b3_c2_ok = b3_c3_ok = b3_c4_ok = b3_c5_ok = b3_c6_ok = true;
            }
        }
    }

    // =========================================================================
    // BUTTON 4 (GAMING SUBSET)
    // =========================================================================
    bool reading4 = digitalRead(BUTTON_4_PIN);
    if (reading4 != lastBtn4) dbTime4 = currentMillis;
    if ((currentMillis - dbTime4) > debounceDelay && reading4 != btn4State) {
        btn4State = reading4;
        if (btn4State == LOW) {
            btn4_clicks++;
            btn4_lastPress = currentMillis;
        }
    }
    lastBtn4 = reading4;

    if (btn4_clicks > 0 && (currentMillis - btn4_lastPress) > doubleTapWindow) {
        if (btn4_clicks == 1) {
            globalState4 = !globalState4;
            b4_p1_ok = b4_p2_ok = b4_b4_ok = false;
            b4_retries = 0; b4_lastRetry = 0;
            updateOLED("GAMING MODE", "Target: " + String(globalState4 ? "ON" : "OFF"), "Transmitting...");
        }
        btn4_clicks = 0;
    }

    if ((!b4_p1_ok || !b4_p2_ok || !b4_b4_ok) && b4_retries < 5) {
        if (currentMillis - b4_lastRetry >= 500) {
            b4_lastRetry = currentMillis;
            b4_retries++;
            
            if (b4_retries > 1) {
                String failDev = "";
                if (!b4_p1_ok) failDev = "Shelf Lights";
                else if (!b4_p2_ok) failDev = "Lamp Light";
                else if (!b4_b4_ok) failDev = "TV Stand";
                updateOLED("RETRY " + String(b4_retries) + "/5", "Failing Device:", failDev);
            }
            
            if (!b4_p1_ok) { b4_p1_ok = toggleTuyaPlug(tuyaPlugIP1, tuyaPlugID1, tuyaPlugKey1, globalState4); if (b4_p1_ok) delay(CASCADE_DELAY); }
            if (!b4_p2_ok) { b4_p2_ok = toggleTuyaPlug(tuyaPlugIP2, tuyaPlugID2, tuyaPlugKey2, globalState4); if (b4_p2_ok) delay(CASCADE_DELAY); }
            if (!b4_b4_ok) { b4_b4_ok = toggleTuyaBulb(tuyaBulbIP4, tuyaBulbID4, tuyaBulbKey4, globalState4); if (b4_b4_ok) delay(CASCADE_DELAY); }

            if (b4_p1_ok && b4_p2_ok && b4_b4_ok) {
                updateOLED("GAMING MODE", "TX Complete!", "Status: " + String(globalState4 ? "ON" : "OFF"));
            } else if (b4_retries >= 5) {
                updateOLED("TX ABORTED", "Max retries hit.", "Returning to idle...");
                b4_p1_ok = b4_p2_ok = b4_b4_ok = true;
            }
        }
    }
}