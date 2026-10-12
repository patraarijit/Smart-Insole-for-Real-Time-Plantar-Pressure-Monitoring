#include <Arduino.h>
#include <NimBLEDevice.h>

#define NUM_SENSORS_PER_NODE 16
#define TOTAL_SENSORS 32
#define BAUD_RATE 460800

// XIAO ESP32-S3 Hardware UART Pins
#define RX1_PIN 44  // D7
#define TX1_PIN 43  // D6

// Custom typedef for notification callback function pointer
typedef void (*NimBLENotifyCallback)(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);

// Hardware Timer
hw_timer_t *timer = NULL;
volatile bool flagSampleReady = false;

void IRAM_ATTR onTimer() {
    flagSampleReady = true;
}

// Data Packet Structure
typedef struct {
    uint16_t readings[NUM_SENSORS_PER_NODE];
} SensorSweep;

// Data Storage for both nodes
SensorSweep leftData = {0};
SensorSweep rightData = {0};

// BLE Server Credentials
const char* macLeft  = "07:B6:1D:3D:DD:63";
const char* macRight = "4C:0D:3C:38:DC:F0";

static NimBLEUUID serviceUUID_L("891ba4d0-b3de-4716-88f6-b1391f392c4d");
static NimBLEUUID charUUID_L("03a96e4f-6918-49a5-93c1-f38bc2ee9d78");

static NimBLEUUID serviceUUID_R("7d451494-099e-468a-9700-5230c2ba2f34");
static NimBLEUUID charUUID_R("f716a8e8-fa94-4c6a-9f8f-66f536a546f4");

// NimBLE Client pointers
NimBLEClient* clientL = nullptr;
NimBLEClient* clientR = nullptr;

bool isConnectedL = false;
bool isConnectedR = false;

// Notification Callbacks
void notifyCallbackL(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (length == sizeof(SensorSweep)) {
        memcpy(&leftData, pData, sizeof(SensorSweep));
    }
}

void notifyCallbackR(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (length == sizeof(SensorSweep)) {
        memcpy(&rightData, pData, sizeof(SensorSweep));
    }
}

// Connection Handlers
class ClientCallbacksL : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.println("[BLE] Left Node Connected");
        isConnectedL = true;
    }
    void onDisconnect(NimBLEClient* pClient, int reason) override {
        Serial.println("[BLE] Left Node Disconnected");
        isConnectedL = false;
    }
};

class ClientCallbacksR : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.println("[BLE] Right Node Connected");
        isConnectedR = true;
    }
    void onDisconnect(NimBLEClient* pClient, int reason) override {
        Serial.println("[BLE] Right Node Disconnected");
        isConnectedR = false;
    }
};

ClientCallbacksL clientCallbacksL;
ClientCallbacksR clientCallbacksR;

// Connect Helper Function
bool connectNode(
    NimBLEClient** pClient, 
    NimBLEClientCallbacks* callbacks, 
    const char* macStr, 
    NimBLEUUID serviceUUID, 
    NimBLEUUID charUUID, 
    NimBLENotifyCallback callback) 
{
    if (*pClient && (*pClient)->isConnected()) return true;

    if (*pClient) {
        NimBLEDevice::deleteClient(*pClient);
        *pClient = nullptr;
    }

    *pClient = NimBLEDevice::createClient();
    if (!*pClient) return false;

    (*pClient)->setClientCallbacks(callbacks);
    (*pClient)->setConnectionParams(6, 6, 0, 60);

    if (!(*pClient)->connect(NimBLEAddress(macStr, BLE_ADDR_PUBLIC)) &&
        !(*pClient)->connect(NimBLEAddress(macStr, BLE_ADDR_RANDOM))) {
        return false;
    }

    auto pService = (*pClient)->getService(serviceUUID);
    if (!pService) return false;

    auto pChar = pService->getCharacteristic(charUUID);
    if (!pChar) return false;

    if (!pChar->subscribe(true, callback)) return false;

    return true;
}

// Output function
void send32ChannelData() {
    // 16 Left channels
    for (int i = 0; i < NUM_SENSORS_PER_NODE; i++) {
        Serial1.print(leftData.readings[i]);
        Serial1.print(',');
    }
    
    // 16 Right channels
    for (int i = 0; i < NUM_SENSORS_PER_NODE; i++) {
        Serial1.print(rightData.readings[i]);
        if (i < NUM_SENSORS_PER_NODE - 1) {
            Serial1.print(',');
        }
    }
    Serial1.println();
}

void setup() {
    Serial.begin(BAUD_RATE);
    Serial1.begin(BAUD_RATE, SERIAL_8N1, RX1_PIN, TX1_PIN);

    NimBLEDevice::init("Dual_FSR_Client");
    NimBLEDevice::setMTU(512);

    Serial.println("Starting Dual FSR BLE Client...");

    // Setup 100 Hz Hardware Timer (10,000 us)
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    timer = timerBegin(1000000);
    timerAttachInterrupt(timer, &onTimer);
    timerAlarm(timer, 10000, true, 0); 
#else
    timer = timerBegin(0, 80, true);
    timerAttachInterrupt(timer, &onTimer, true);
    timerAlarmWrite(timer, 10000, true);
    timerAlarmEnable(timer);
#endif
}

void loop() {
    static unsigned long lastConnectAttempt = 0;
    
    if (millis() - lastConnectAttempt > 2000) {
        lastConnectAttempt = millis();

        if (!isConnectedL) {
            Serial.println("Connecting to LEFT server...");
            connectNode(&clientL, &clientCallbacksL, macLeft, serviceUUID_L, charUUID_L, notifyCallbackL);
        }

        if (!isConnectedR) {
            Serial.println("Connecting to RIGHT server...");
            connectNode(&clientR, &clientCallbacksR, macRight, serviceUUID_R, charUUID_R, notifyCallbackR);
        }
    }

    if (flagSampleReady) {
        flagSampleReady = false;
        send32ChannelData();
    }
}