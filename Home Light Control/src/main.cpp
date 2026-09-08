#include <Arduino.h>
#include <WiFi.h>
#include <NimBLEDevice.h>
#include <mbedtls/aes.h> // ESP32 built-in hardware encryption

// --- Wi-Fi Credentials ---
const char* ssid = "MySpectrumWiFi02-2G";
const char* password = "recentnews374";

// --- Govee BLE Credentials ---
#define GOVEE_MAC "d3:21:c6:46:0d:46" 
static const BLEUUID serviceUUID("00010203-0405-0607-0809-0a0b0c0d1910");
static const BLEUUID charUUID("00010203-0405-0607-0809-0a0b0c0d2b11");

uint8_t powerOn[]  = {0x33, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x33};
uint8_t powerOff[] = {0x33, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x32};

// --- Feit Tuya Wi-Fi Credentials ---
IPAddress tuyaIP(192, 168, 1, 31);
const char* tuyaDeviceID = "ebf9fa6de05d0ece56ecfw";
const char* tuyaLocalKey = "6n~O6wS-Hzk+sPyj";

// --- Button Configuration ---
#define BUTTON_PIN 4
bool globalState = false; 
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;

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

// --- Raw Tuya V3.3 TCP Sender ---
void toggleTuyaPlug(bool state) {
    WiFiClient client;
    if (!client.connect(tuyaIP, 6668)) {
        Serial.println("Tuya TCP connection failed.");
        return;
    }

    // 1. Build the JSON Payload
    String json = "{\"devId\":\"" + String(tuyaDeviceID) + "\",\"uid\":\"\",\"t\":\"1600000000\",\"dps\":{\"1\":";
    json += (state ? "true" : "false");
    json += "}}";

    // 2. Apply PKCS7 Padding
    size_t plainLen = json.length();
    size_t padAmt = 16 - (plainLen % 16);
    size_t paddedLen = plainLen + padAmt;
    uint8_t plaintext[paddedLen];
    memcpy(plaintext, json.c_str(), plainLen);
    for (size_t i = plainLen; i < paddedLen; i++) plaintext[i] = padAmt;

    // 3. Encrypt via ESP32 Hardware AES-128-ECB
    uint8_t ciphertext[paddedLen];
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, (const uint8_t*)tuyaLocalKey, 128);
    for (size_t i = 0; i < paddedLen; i += 16) {
        mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, plaintext + i, ciphertext + i);
    }
    mbedtls_aes_free(&aes);

    // 4. Assemble the Tuya V3.3 Packet
    uint32_t packetLen = 15 + paddedLen + 8; // Version Header + Ciphertext + CRC + Suffix
    size_t totalSize = 16 + 15 + paddedLen + 8; 
    uint8_t packet[totalSize];
    memset(packet, 0, totalSize);

    // Header, Command (7 = Control), and Packet Length
    packet[0] = 0x00; packet[1] = 0x00; packet[2] = 0x55; packet[3] = 0xAA;
    packet[11] = 0x07; 
    packet[12] = (packetLen >> 24) & 0xFF; packet[13] = (packetLen >> 16) & 0xFF;
    packet[14] = (packetLen >> 8) & 0xFF;  packet[15] = packetLen & 0xFF;
    
    // Version Header (3.3 followed by 12 zeros)
    packet[16] = '3'; packet[17] = '.'; packet[18] = '3';

    // Insert Ciphertext
    memcpy(&packet[31], ciphertext, paddedLen);

    // Calculate CRC32 over the payload
    uint32_t crc = getTuyaCRC(&packet[4], 27 + paddedLen);
    int crcIdx = 31 + paddedLen;
    packet[crcIdx] = (crc >> 24) & 0xFF;     packet[crcIdx+1] = (crc >> 16) & 0xFF;
    packet[crcIdx+2] = (crc >> 8) & 0xFF;    packet[crcIdx+3] = crc & 0xFF;

    // Suffix
    packet[crcIdx+4] = 0x00; packet[crcIdx+5] = 0x00; packet[crcIdx+6] = 0xAA; packet[crcIdx+7] = 0x55;

    // 5. Send and Close
    client.write(packet, totalSize);
    client.stop();
    Serial.println("Feit plug toggled natively over TCP!");
}

void toggleGoveeBLE(bool state) {
    BLEAddress goveeAddress(GOVEE_MAC, 1);
    NimBLEClient* pClient = NimBLEDevice::createClient();
    
    if(pClient->connect(goveeAddress)) {
        BLERemoteService* pService = pClient->getService(serviceUUID);
        if(pService != nullptr) {
            BLERemoteCharacteristic* pChar = pService->getCharacteristic(charUUID);
            if(pChar != nullptr) {
                if(state) {
                    pChar->writeValue(powerOn, sizeof(powerOn), true);
                } else {
                    pChar->writeValue(powerOff, sizeof(powerOff), true);
                }
                Serial.println("Govee strip toggled over BLE!");
            }
        }
        pClient->disconnect();
    }
    NimBLEDevice::deleteClient(pClient);
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
    if (reading != lastButtonState) lastDebounceTime = millis();
    
    if ((millis() - lastDebounceTime) > debounceDelay) {
        if (reading == LOW && lastButtonState == HIGH) {
            globalState = !globalState;
            
            // Fire both protocols simultaneously 
            toggleTuyaPlug(globalState);
            toggleGoveeBLE(globalState);
        }
    }
    lastButtonState = reading;
}