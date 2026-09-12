#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ==========================================
// 1. WI-FI & NETWORK CREDENTIALS
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// REPLACE with the EXACT IP Address of your PC running the Python Server
const String serverUrl = "http://YOUR_SERVER_IP:5000/detect"; 

// ==========================================
// 2. HARDWARE PINOUTS
// ==========================================
#define SERVO_PIN      14  // PWM Output (Moved to GPIO 14 for safety)

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

// Function to translate Degrees (0-180) to ESP32 PWM Duty Cycle
void moveServo(int angle) {
  int duty = map(angle, 0, 180, 3276, 6553);
  // ESP32 Core v3 syntax: Write directly to the pin
  ledcWrite(SERVO_PIN, duty); 
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Initialize Servo (ESP32 Core v3.0 API)
  ledcAttach(SERVO_PIN, SERVO_FREQ, SERVO_RES);
  moveServo(90); // Set to default neutral flat position (90 degrees)

  // Initialize Camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM; config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM; config.xclk_freq_hz = 20000000; config.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
  }
  
  esp_camera_init(&config);

  // Connect to Wi-Fi
  Serial.print("\nConnecting to Wi-Fi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nSystem Online. Hardware Ready!");
}

// ==========================================
// 5. MAIN LOOP (EDGE AI INFERENCE)
// ==========================================
void loop() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  WiFiClient client; 
  HTTPClient http;
  
  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "image/jpeg");
  
  // Transmit raw frame buffer directly to Flask
  int httpResponseCode = http.POST(fb->buf, fb->len);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    
    // Parse the JSON payload returned by the Python Server
    StaticJsonDocument<256> doc;
    deserializeJson(doc, response);
    int servoAngle = doc["servo_angle"];
    
    // Actuate electromechanical trapdoor if an item is identified
    if (servoAngle > 0) {
      Serial.print("Waste detected! Actuating Servo to "); 
      Serial.print(servoAngle); 
      Serial.println(" degrees.");
      
      moveServo(servoAngle); // Tilt Left (30) or Right (150)
      delay(3000);   // Allow waste to fall
      moveServo(90); // Return to neutral flat position (90 degrees)
      delay(1000);   // Brief pause to let mechanics stabilize
    }
  } else {
    Serial.print("Server error/disconnected. HTTP Code: ");
    Serial.println(httpResponseCode);
  }
  
  http.end();
  esp_camera_fb_return(fb); // Free memory buffer
  
  delay(100); // Small delay to prevent spamming the network
}