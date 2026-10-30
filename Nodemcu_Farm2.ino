#include <ESP8266WiFi.h>
#include <espnow.h>

// ESP32 MAC
uint8_t receiverMAC[] = {0x6C, 0xC8, 0x40, 0x89, 0xB0, 0x1C};

typedef struct {
  int moisture;
  int deviceID;
} struct_message;

struct_message data;

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);   // ✅ required

  if (esp_now_init() != 0) {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(receiverMAC, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  Serial.println("ESP8266 Ready");
}

void loop() {

  data.deviceID = 2;   //  Farm 2

  int raw = analogRead(A0);

  // convert to %
  int moisture = map(raw, 1023, 300, 0, 100);
  moisture = constrain(moisture, 0, 100);

  data.moisture = moisture;

  uint8_t result = esp_now_send(receiverMAC, (uint8_t *)&data, sizeof(data));

  if (result == 0) {
    Serial.print("Sent OK | Moisture: ");
    Serial.println(moisture);
  } else {
    Serial.println("Send Failed");
  }

  delay(3000);
}
