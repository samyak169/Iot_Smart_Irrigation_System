#include <WiFi.h>
#include <esp_now.h>
#include <DHT.h>

// ===== RELAYS =====
#define RELAY1_PIN 14
#define RELAY2_PIN 13

// ===== DHT11 =====
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ===== RAIN SENSOR =====
#define RAIN_PIN 15

// ===== THRESHOLDS =====
#define DRY_LEVEL 5
#define WET_LEVEL 80

// ===== STRUCT =====
typedef struct {
  int moisture;
  int deviceID;
} struct_message;

struct_message incomingData;

// ===== VARIABLES =====
int moisture1 = 0, moisture2 = 0;

bool manualMode1 = false;
bool manualMode2 = false;

bool pump1State = false;
bool pump2State = false;

unsigned long lastDataTime1 = 0;
unsigned long lastDataTime2 = 0;

bool farm1AlertSent = false;
bool farm2AlertSent = false;

unsigned long lastModeChangeTime = 0;

// ===== NEW SENSOR VALUES =====
float temperature = 0;
float humidity = 0;
bool isRaining = false;

// ===== SERIAL BUFFER =====
String serialBuffer = "";

// ===== ESP-NOW RECEIVE =====
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {

  memcpy(&incomingData, data, sizeof(incomingData));

  int value = incomingData.moisture;

  if (value == 0) value = 5;
  if (value > 100) return;

  // ===== FARM 1 =====
  if (incomingData.deviceID == 1) {

    moisture1 = value;
    lastDataTime1 = millis();
    farm1AlertSent = false;

    if (!manualMode1) {

      if (isRaining) {
        digitalWrite(RELAY1_PIN, HIGH);
        pump1State = false;
      }
      else if (moisture1 <= DRY_LEVEL) {
        digitalWrite(RELAY1_PIN, LOW);
        pump1State = true;
      }
      else if (moisture1 >= WET_LEVEL) {
        digitalWrite(RELAY1_PIN, HIGH);
        pump1State = false;
      }
    }
  }

  // ===== FARM 2 =====
  if (incomingData.deviceID == 2) {

    moisture2 = value;
    lastDataTime2 = millis();
    farm2AlertSent = false;

    if (!manualMode2) {

      if (isRaining) {
        digitalWrite(RELAY2_PIN, HIGH);
        pump2State = false;
      }
      else if (moisture2 <= DRY_LEVEL) {
        digitalWrite(RELAY2_PIN, LOW);
        pump2State = true;
      }
      else if (moisture2 >= WET_LEVEL) {
        digitalWrite(RELAY2_PIN, HIGH);
        pump2State = false;
      }
    }
  }
}

// ===== SEND STATUS =====
void sendStatus() {

  String data = "F1:" + String(moisture1) +
                ",F2:" + String(moisture2) +
                ",P1:" + String(pump1State) +
                ",P2:" + String(pump2State) +
                ",TEMP:" + String(temperature) +
                ",HUM:" + String(humidity) +
                ",RAIN:" + String(isRaining);

  if (millis() - lastModeChangeTime > 3000) {
    data += ",M1:" + String(manualMode1) +
            ",M2:" + String(manualMode2);
  }

  Serial2.println(data);
  Serial.println(data);
}

// ===== PROCESS COMMAND =====
void processCommand(String cmd) {

  Serial.println("CMD: " + cmd);

  if (cmd == "AUTO1") {
    manualMode1 = false;
    lastModeChangeTime = millis();
  }

  else if (cmd == "MANUAL1" || (cmd == "P1OFF" && !manualMode1)) {
    manualMode1 = true;
    lastModeChangeTime = millis();
  }

  else if (cmd == "P1ON") {
    if (manualMode1) {
      digitalWrite(RELAY1_PIN, LOW);
      pump1State = true;
    }
  }

  else if (cmd == "P1OFF") {
    if (manualMode1) {
      digitalWrite(RELAY1_PIN, HIGH);
      pump1State = false;
    }
  }

  else if (cmd == "AUTO2") {
    manualMode2 = false;
    lastModeChangeTime = millis();
  }

  else if (cmd == "MANUAL2" || (cmd == "P2OFF" && !manualMode2)) {
    manualMode2 = true;
    lastModeChangeTime = millis();
  }

  else if (cmd == "P2ON") {
    if (manualMode2) {
      digitalWrite(RELAY2_PIN, LOW);
      pump2State = true;
    }
  }

  else if (cmd == "P2OFF") {
    if (manualMode2) {
      digitalWrite(RELAY2_PIN, HIGH);
      pump2State = false;
    }
  }

  else if (cmd == "STATUS") {
    sendStatus();
  }
}

// ===== SERIAL READ =====
void readSerial() {

  while (Serial2.available()) {

    char c = Serial2.read();

    if (c == '\n') {
      serialBuffer.trim();
      processCommand(serialBuffer);
      serialBuffer = "";
    }
    else {
      serialBuffer += c;
    }
  }
}

// ===== SETUP =====
void setup() {

  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  pinMode(RELAY1_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);

  digitalWrite(RELAY1_PIN, HIGH);
  digitalWrite(RELAY2_PIN, HIGH);

  pinMode(RAIN_PIN, INPUT);

  dht.begin();

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("✅ ESP32 READY");
}

// ===== LOOP =====
void loop() {

  static unsigned long lastSend = 0;
  static unsigned long lastSensorRead = 0;

  readSerial();

  // ===== READ DHT + RAIN =====
  if (millis() - lastSensorRead > 2000) {
    lastSensorRead = millis();

    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    isRaining = (digitalRead(RAIN_PIN) == LOW);
  }

  // ===== SEND STATUS =====
  if (millis() - lastSend > 3000) {
    lastSend = millis();
    sendStatus();
  }

  unsigned long now = millis();

  // ===== FARM 1 NO DATA =====
  if ((now - lastDataTime1 > 15000) && !farm1AlertSent) {

    moisture1 = 0;
    digitalWrite(RELAY1_PIN, HIGH);
    pump1State = false;

    Serial2.println("ALERT:FARM1_NO_DATA");
    farm1AlertSent = true;
  }

  // ===== FARM 2 NO DATA =====
  if ((now - lastDataTime2 > 15000) && !farm2AlertSent) {

    moisture2 = 0;
    digitalWrite(RELAY2_PIN, HIGH);
    pump2State = false;

    Serial2.println("ALERT:FARM2_NO_DATA");
    farm2AlertSent = true;
  }
}
