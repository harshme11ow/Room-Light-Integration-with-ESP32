#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <mbedtls/aes.h> 

// --- Wi-Fi Credentials ---
const char* ssid = "MySpectrumWiFi02-2G";
const char* password = "recentnews374";

// --- Govee BLE MAC Addresses ---
#define GOVEE_STRIP_MAC "d3:21:c6:46:0d:46" 
#define GOVEE_BARS_MAC  "AA:BB:CC:DD:EE:FF" // <--- Replace with your Lightbars' MAC

static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

// --- Feit Tuya Plug 1 Configuration ---
IPAddress tuyaIP1(192, 168, 1, 31);
const char* tuyaID1 = "ebf9fa6de05d0ece56ecfw";
const char* tuyaKey1 = "6n~O6wS-Hzk+sPyj";

// --- Feit Tuya Plug 2 Configuration ---
IPAddress tuyaIP2(192, 168, 1, 33);
const char* tuyaID2 = "ebb7d988301f646e51npsj";
const char* tuyaKey2 = "MGH`N!_hX8gP|wkv";

// --- Button Configuration ---
#define BUTTON_PIN 4
bool globalState = false; 
bool buttonState = HIGH;
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

// Execution lock to prevent mashing while radios are busy
bool isExecuting = false; 

// --- Tuya Protocol Checksum Engine ---
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

// --- Reusable Raw Tuya V3.3 TCP Sender ---
bool toggleTuyaPlug(IPAddress ip, const char* devId, const char* localKey, bool state) {
    WiFiClient client;
    client.setTimeout(1); 
    
    if (!client.connect(ip, 6668, 1000)) {
        Serial.printf("Tuya TCP connection failed for device %s (Timeout).\n", devId);
        return false;
    }

    String json = "{\"devId\":\"" + String(devId) + "\",\"uid\":\"\",\"t\":\"1600000000\",\"dps\":{\"1\":";
    json += (state ? "true" : "false");
    json += "}}";

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
    Serial.printf("Feit plug (%s) toggled natively over TCP!\n", devId);
    return true;
}

// --- Reusable Govee BLE Sender ---
bool toggleGoveeBLE(const char* macAddress, bool state) {
    BLEAddress goveeAddress(macAddress, 1);
    NimBLEClient* pClient = NimBLEDevice::createClient();
    pClient->setConnectTimeout(1); 
    
    bool success = false;
    if(pClient->connect(goveeAddress)) {
        BLERemoteService* pService = pClient->getService(serviceUUID);
        if(pService != nullptr) {
            BLERemoteCharacteristic* pChar = pService->getCharacteristic(charUUID);
            if(pChar != nullptr) {
                if(state) {
                    pChar->writeValue(powerOn, sizeof(powerOn), false);
                } else {
                    pChar->writeValue(powerOff, sizeof(powerOff), false);
                }
                Serial.printf("Govee device at %s toggled over BLE!\n", macAddress);
                success = true;
            }
        }
        delay(50);
        pClient->disconnect();
    } else {
        Serial.printf("Govee BLE at %s unreachable (Timeout).\n", macAddress);
    }
    NimBLEDevice::deleteClient(pClient);
    return success;
}

void setup() {
    Serial.begin(115200);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    
    Serial.print("Connecting to Wi-Fi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");

    NimBLEDevice::init("");
    Serial.println("ESP32 Ready. Press button to trigger all lights.");
}

void loop() {
    bool reading = digitalRead(BUTTON_PIN);
    
    if (reading != lastButtonState) {
        lastDebounceTime = millis();
    }
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (reading != buttonState) {
            buttonState = reading;
            
            if (buttonState == LOW && !isExecuting) {
                isExecuting = true;
                globalState = !globalState;
                
                Serial.printf("\n--- COMMANDING ALL DEVICES: %s ---\n", globalState ? "ON" : "OFF");
                
                // Pass 1: Try firing all devices concurrently
                bool plug1Ok = toggleTuyaPlug(tuyaIP1, tuyaID1, tuyaKey1, globalState);
                bool plug2Ok = toggleTuyaPlug(tuyaIP2, tuyaID2, tuyaKey2, globalState);
                bool stripOk = toggleGoveeBLE(GOVEE_STRIP_MAC, globalState);
                bool barsOk  = toggleGoveeBLE(GOVEE_BARS_MAC, globalState);
                
                // Pass 2: Retry queue for any devices that timed out
                if (!plug1Ok || !plug2Ok || !stripOk || !barsOk) {
                    Serial.println("--- RETRYING FAILED DEVICES ---");
                    
                    if (!plug1Ok) toggleTuyaPlug(tuyaIP1, tuyaID1, tuyaKey1, globalState);
                    if (!plug2Ok) toggleTuyaPlug(tuyaIP2, tuyaID2, tuyaKey2, globalState);
                    if (!stripOk) toggleGoveeBLE(GOVEE_STRIP_MAC, globalState);
                    if (!barsOk)  toggleGoveeBLE(GOVEE_BARS_MAC, globalState);
                }
                
                isExecuting = false;
                Serial.println("--- TRANSMISSION COMPLETE ---");
            }
        }
    }
    
    lastButtonState = reading;
}