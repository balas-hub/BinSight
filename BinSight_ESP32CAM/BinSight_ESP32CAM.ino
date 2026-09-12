#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ==========================================
// 1. WI-FI & NETWORK CREDENTIALS
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// Must match the IP printed by binsight_server.py when it starts
String serverIP = "YOUR_SERVER_IP";
const int serverPort = 5000;
String serverUrl;  // built in setup()

// ==========================================
// 2. HARDWARE PINOUTS
// ==========================================
#define SERVO_PIN      15  // PWM Output for Servo Motor

// Standard AI-Thinker Camera Pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==========================================
// 3. SERVO MOTOR (PWM) CONFIG
// ==========================================
#define SERVO_FREQ 50
#define SERVO_RES 16

void moveServo(int angle) {
  int duty = map(angle, 0, 180, 3276, 6553);
  ledcWrite(SERVO_PIN, duty);
}

// ==========================================
// 4. WI-FI CONNECT (with reconnect support)
// ==========================================
bool connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected!");
    Serial.print("ESP32 IP Address: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("\nWi-Fi connection FAILED.");
  return false;
}

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  ledcAttach(SERVO_PIN, SERVO_FREQ, SERVO_RES);
  moveServo(0); // Default closed position

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM; config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM; config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
  }

  esp_err_t camErr = esp_camera_init(&config);
  if (camErr != ESP_OK) {
    Serial.printf("Camera init FAILED with error 0x%x\n", camErr);
    while (true) delay(1000);
  }
  Serial.println("Camera initialized successfully.");

  if (!connectWiFi()) {
    Serial.println("Restarting in 5 seconds...");
    delay(5000);
    ESP.restart();
  }

  serverUrl = "http://" + serverIP + ":" + String(serverPort) + "/detect";
  Serial.print("Target server URL: ");
  Serial.println(serverUrl);
  Serial.println("System Online. Hardware Ready!\n");
}

// ==========================================
// 6. MAIN LOOP (EDGE AI INFERENCE)
// ==========================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected! Reconnecting...");
    connectWiFi();
    delay(1000);
    return;
  }

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    delay(500);
    return;
  }

  WiFiClient client;
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);

  if (!http.begin(client, serverUrl)) {
    Serial.println("http.begin() failed — malformed URL?");
    esp_camera_fb_return(fb);
    delay(1000);
    return;
  }

  http.addHeader("Content-Type", "image/jpeg");
  int httpResponseCode = http.POST(fb->buf, fb->len);

  if (httpResponseCode == 200) {
    String response = http.getString();

    StaticJsonDocument<256> doc;
    DeserializationError jsonErr = deserializeJson(doc, response);

    if (jsonErr) {
      Serial.print("JSON parse failed: ");
      Serial.println(jsonErr.c_str());
    } else {
      int servoAngle = doc["servo_angle"] | 0;
      const char* item = doc["item"] | "unknown";

      if (servoAngle > 0) {
        Serial.print("Detected: ");
        Serial.print(item);
        Serial.print(" -> Actuating Servo to ");
        Serial.print(servoAngle);
        Serial.println(" degrees.");

        moveServo(servoAngle);
        delay(3000);   // Allow waste to fall through
        moveServo(0);  // Return to default closed position
        delay(1000);   // Let mechanics stabilize
      }
    }
  } else if (httpResponseCode > 0) {
    Serial.print("Server returned HTTP error: ");
    Serial.println(httpResponseCode);
    Serial.println(http.getString());
  } else {
    Serial.print("Connection failed. HTTP Code: ");
    Serial.print(httpResponseCode);
    Serial.print(" (");
    Serial.print(http.errorToString(httpResponseCode));
    Serial.println(")");
  }

  http.end();
  esp_camera_fb_return(fb);

  delay(100); // Small delay to prevent spamming the network
}
