#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <time.h>

// ===== WIFI =====
const char* ssid = "Wifi-Name";
const char* password = "Wifi-Password";

// ================= TELEGRAM =================
String BOT_TOKEN = "Bot-Token";
String CHAT_ID = "Chat_Id";

WiFiClientSecure client;
UniversalTelegramBot bot(BOT_TOKEN, client);

// ===== SERIAL =====
HardwareSerial mySerial(1);

// ===== NTP TIME =====
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;

// ===== VARIABLES =====
unsigned long lastTelegram = 0;
unsigned long lastDataReceived = 0;
bool alertSent = false;

String buffer = "";

// ===== STATES =====
int moisture1 = 0, moisture2 = 0;
bool mode1Manual = false;
bool mode2Manual = false;
bool pump1State = false;
bool pump2State = false;

// NEW SENSOR DATA
float temperature = 0;
float humidity = 0;
bool isRaining = false;

// ===== KEYBOARD =====
String keyboard =
"[[\"🟢 P1 ON\",\"🔴 P1 OFF\"],"
"[\"🟢 P2 ON\",\"🔴 P2 OFF\"],"
"[\"🛠 MANUAL1\",\"⚙️ AUTO1\"],"
"[\"🛠 MANUAL2\",\"⚙️ AUTO2\"],"
"[\"📸 PHOTO\",\"📊 STATUS\"]]";

// ===== TIME FUNCTION =====
String getTimeStamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "Time Error";

  char buffer[30];
  strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S", &timeinfo);
  return String(buffer);
}

// ===== CAMERA INIT =====
void startCamera() {
  camera_config_t config;

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = 5;
  config.pin_d1 = 18;
  config.pin_d2 = 19;
  config.pin_d3 = 21;
  config.pin_d4 = 36;
  config.pin_d5 = 39;
  config.pin_d6 = 34;
  config.pin_d7 = 35;

  config.pin_xclk = 0;
  config.pin_pclk = 22;
  config.pin_vsync = 25;
  config.pin_href = 23;

  config.pin_sccb_sda = 26;
  config.pin_sccb_scl = 27;

  config.pin_pwdn = 32;
  config.pin_reset = -1;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;

  esp_camera_init(&config);
}

// ===== PHOTO =====
void sendPhotoTelegram() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (fb) esp_camera_fb_return(fb);

  delay(100);
  fb = esp_camera_fb_get();
  if (!fb) return;

  WiFiClientSecure client_tcp;
  client_tcp.setInsecure();

  if (!client_tcp.connect("api.telegram.org", 443)) return;

  String head = "--boundary\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" + CHAT_ID +
                "\r\n--boundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"img.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--boundary--\r\n";

  uint32_t totalLen = head.length() + fb->len + tail.length();

  client_tcp.println("POST /bot" + BOT_TOKEN + "/sendPhoto HTTP/1.1");
  client_tcp.println("Host: api.telegram.org");
  client_tcp.println("Content-Type: multipart/form-data; boundary=boundary");
  client_tcp.println("Content-Length: " + String(totalLen));
  client_tcp.println();

  client_tcp.print(head);

  for (size_t i = 0; i < fb->len; i += 1024) {
    client_tcp.write(fb->buf + i, min((size_t)1024, fb->len - i));
  }

  client_tcp.print(tail);

  esp_camera_fb_return(fb);
}

// ===== PARSER =====
int getValue(String data, String key) {
  int start = data.indexOf(key);
  if (start == -1) return 0;
  start += key.length();
  int end = data.indexOf(",", start);
  if (end == -1) end = data.length();
  return data.substring(start, end).toInt();
}

// ===== PROCESS SERIAL =====
void processData(String data) {

  if (data.startsWith("F1:")) {

    moisture1 = getValue(data, "F1:");
    moisture2 = getValue(data, "F2:");

    pump1State = getValue(data, "P1:");
    pump2State = getValue(data, "P2:");

    mode1Manual = getValue(data, "M1:");
    mode2Manual = getValue(data, "M2:");

    temperature = getValue(data, "TEMP:");
    humidity = getValue(data, "HUM:");
    isRaining = getValue(data, "RAIN:");

    lastDataReceived = millis();
    alertSent = false;
  }

  else if (data.startsWith("ALERT")) {

    String msg = "⚠️ " + data + "\n";
    msg += "🕒 " + getTimeStamp();

    bot.sendMessage(CHAT_ID, msg, "");
  }
}

// ===== SERIAL READ =====
void readSerial() {
  while (mySerial.available()) {
    char c = mySerial.read();
    if (c == '\n') {
      buffer.trim();
      processData(buffer);
      buffer = "";
    } else {
      buffer += c;
    }
  }
}

// ===== STATUS =====
String buildStatus() {

  String msg = "🌱 Smart Farm Status\n\n";

  msg += "💧 Moisture:\n";
  msg += "Farm1: " + String(moisture1) + "%\n";
  msg += "Farm2: " + String(moisture2) + "%\n\n";

  msg += "🚿 Pump:\n";
  msg += "P1: " + String(pump1State ? "🟢 ON" : "🔴 OFF") + "\n";
  msg += "P2: " + String(pump2State ? "🟢 ON" : "🔴 OFF") + "\n\n";

  msg += "🌡️ Temp: " + String(temperature) + "°C\n";
  msg += "💧 Hum: " + String(humidity) + "%\n";
  msg += "🌧️ Rain: " + String(isRaining ? "YES" : "NO") + "\n\n";

  msg += "⚙️ Mode:\n";
  msg += "F1: " + String(mode1Manual ? "🛠 MANUAL" : "⚙️ AUTO") + "\n";
  msg += "F2: " + String(mode2Manual ? "🛠 MANUAL" : "⚙️ AUTO");

  msg += "\n\n🕒 " + getTimeStamp();

  return msg;
}

// ===== TELEGRAM =====
void handleMessages(int n) {

  for (int i = 0; i < n; i++) {

    String text = bot.messages[i].text;

    if (text == "/start") {
      bot.sendMessageWithReplyKeyboard(CHAT_ID, "✅ Controller Ready", "", keyboard, true);
    }

    else if (text == "🟢 P1 ON") mySerial.println("P1ON");
    else if (text == "🔴 P1 OFF") mySerial.println("P1OFF");

    else if (text == "🟢 P2 ON") mySerial.println("P2ON");
    else if (text == "🔴 P2 OFF") mySerial.println("P2OFF");

    else if (text == "⚙️ AUTO1") mySerial.println("AUTO1");
    else if (text == "🛠 MANUAL1") mySerial.println("P1OFF");

    else if (text == "⚙️ AUTO2") mySerial.println("AUTO2");
    else if (text == "🛠 MANUAL2") mySerial.println("P2OFF");

    else if (text == "📸 PHOTO") sendPhotoTelegram();

    else if (text == "📊 STATUS") {
      bot.sendMessage(CHAT_ID, buildStatus(), "");
    }
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  mySerial.begin(9600, SERIAL_8N1, 13, 15);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  client.setInsecure();
  startCamera();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

// ===== LOOP =====
void loop() {

  readSerial();

  if (millis() - lastTelegram > 2000) {
    int n = bot.getUpdates(bot.last_message_received + 1);
    if (n) handleMessages(n);
    lastTelegram = millis();
  }

  if (millis() - lastDataReceived > 15000 && !alertSent) {
    String msg = "⚠️ No data from farm!\n";
    msg += "🕒 " + getTimeStamp();

    bot.sendMessage(CHAT_ID, msg, "");
    alertSent = true;
  }
}