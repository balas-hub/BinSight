#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <UniversalTelegramBot.h>
#include "esp_http_server.h"

// ==========================================
// 1. WI-FI & NETWORK CREDENTIALS
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// REPLACE with the EXACT IP Address of your PC running the Python Server
const String serverUrl = "http://YOUR_SERVER_IP:5000/detect"; 

// Telegram Bot Credentials
const String telegramToken = "YOUR_TELEGRAM_BOT_TOKEN";
const String chatId = "YOUR_TELEGRAM_CHAT_ID";

// Initialize Telegram Bot Client
WiFiClientSecure secured_client;
UniversalTelegramBot bot(telegramToken, secured_client);

int botRequestDelay = 2000;
unsigned long lastTimeBotRan;
bool flashState = false;

// ==========================================
// 2. HARDWARE PINOUTS
// ==========================================
#define BUTTON_PIN     12  // Mode Switch Button (Connect to GND)
#define PIR_PIN        13  // PIR Motion Sensor (Security Mode)
#define SERVO_PIN      14  // PWM Output for Servo Motor
#define FLASH_LED_PIN  4   // Built-in high power LED flash

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
// 2.5 LIVE STREAM SERVER SETUP
// ==========================================
httpd_handle_t stream_httpd = NULL;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK) return res;

  while(true){
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed for stream");
      res = ESP_FAIL;
    } else {
      _jpg_buf_len = fb->len;
      _jpg_buf = fb->buf;
    }
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } 
    if(res != ESP_OK){
      break;
    }
  }
  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  
  httpd_uri_t stream_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("Web server started for Live Stream");
  }
}

// ==========================================
// 2.8 TELEGRAM COMMAND HANDLER
// ==========================================
void sendTelegramAlert(); // Forward declaration so compiler knows it exists below

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != chatId) continue; 

    String text = bot.messages[i].text;
    Serial.println("Received Telegram command: " + text);

    if (text == "/start") {
      String welcome = "Welcome to the BinSight Security Panel.\nSelect a command below:";
      String keyboardJson = "[[\"📷 Live Stream\", \"💡 Toggle Flash\"], [\"📸 Capture Image\"]]";
      bot.sendMessageWithReplyKeyboard(chat_id, welcome, "", keyboardJson, true);
    } 
    else if (text == "📷 Live Stream") {
      String streamLink = "📡 Live stream is running!\n\nOpen this link in your browser (same Wi-Fi):\nhttp://" + WiFi.localIP().toString() + "/";
      bot.sendMessage(chat_id, streamLink, "");
    } 
    else if (text == "💡 Toggle Flash") {
      flashState = !flashState;
      digitalWrite(FLASH_LED_PIN, flashState ? HIGH : LOW);
      String statusMsg = flashState ? "Flashlight is now ON 🔦" : "Flashlight is now OFF 🌑";
      bot.sendMessage(chat_id, statusMsg, "");
    } 
    else if (text == "📸 Capture Image") {
      bot.sendMessage(chat_id, "Capturing image...", "");
      sendTelegramAlert(); 
    }
  }
}

// ==========================================
// 3. FSM (FINITE STATE MACHINE) CONFIG
// ==========================================
enum SystemState { MODE_BIN, MODE_SURVEILLANCE };
volatile SystemState currentState = MODE_BIN; // Starts in Smart Bin Mode
volatile bool modeChanged = false;

// Hardware Debouncing variables
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 350; 

// ==========================================
// 4. SERVO MOTOR & LED HELPERS
// ==========================================
#define SERVO_FREQ 50
#define SERVO_RES 16

// ISR: Hardware Debounced Button Interrupt
void IRAM_ATTR handleButtonPress() {
  if ((millis() - lastDebounceTime) > debounceDelay) {
    currentState = (currentState == MODE_BIN) ? MODE_SURVEILLANCE : MODE_BIN;
    modeChanged = true;
    lastDebounceTime = millis();
  }
}

// Function to translate Degrees (0-180) to ESP32 PWM Duty Cycle
void moveServo(int angle) {
  int duty = map(angle, 0, 180, 3276, 6553);
  ledcWrite(SERVO_PIN, duty); // ESP32 Core v3 syntax
}

// Function to blink the built-in flash LED a specific number of times
void blinkFlash(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(FLASH_LED_PIN, HIGH);
    delay(150); // Flash ON duration
    digitalWrite(FLASH_LED_PIN, LOW);
    if (i < times - 1) {
      delay(200); // Delay between consecutive blinks
    }
  }
}

// ==========================================
// 5. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  // Initialize Pins
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);
  
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW); // Ensure flash is OFF by default
  
  // Attach the FSM Interrupt
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);

  // Initialize Servo (ESP32 Core v3 API)
  ledcAttach(SERVO_PIN, SERVO_FREQ, SERVO_RES);
  moveServo(90); // Set to default NEUTRAL FLAT position (90 degrees)

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

  startCameraServer();
  secured_client.setInsecure(); 
  bot.sendMessage(chatId, "✅ BinSight Firmware Updated!\nSend /start to view the control menu.", "");
}

// ==========================================
// 6. MAIN LOOP (FSM CONTROLLER)
// ==========================================
void loop() {
  if (millis() - lastTimeBotRan > botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    while (numNewMessages) {
      handleNewMessages(numNewMessages);
      numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    }
    lastTimeBotRan = millis();
  }

  if (modeChanged) {
    Serial.println("\n====================================");
    Serial.print("Switched to: ");
    
    // Check state, print to Serial, and blink LED accordingly
    if (currentState == MODE_BIN) {
      Serial.println("SMART BIN MODE (AI Active)");
      blinkFlash(1); // 1 Blink for Bin Mode
    } else {
      Serial.println("SURVEILLANCE MODE (PIR Active)");
      blinkFlash(2); // 2 Blinks for Surveillance Mode
    }
    
    Serial.println("====================================\n");
    modeChanged = false;
  }

  if (currentState == MODE_BIN) {
    classifyWasteLocally(); // Stream to local Python Server
    delay(50);
  } else if (currentState == MODE_SURVEILLANCE) {
    if (digitalRead(PIR_PIN) == HIGH) {
      Serial.println("Motion detected! Transmitting photo to Telegram...");
      sendTelegramAlert();
      delay(8000); // 8-second cooldown to avoid spamming
    }
    delay(100); // Small delay to keep loop stable
  }
}

// ==========================================
// 7. EDGE AI FUNCTION (HTTP POST to FLASK)
// ==========================================
void classifyWasteLocally() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  WiFiClient client; 
  HTTPClient http;
  
  http.begin(client, serverUrl);
  http.addHeader("Content-Type", "image/jpeg");
  
  int httpResponseCode = http.POST(fb->buf, fb->len);
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    
    StaticJsonDocument<256> doc;
    deserializeJson(doc, response);
    int servoAngle = doc["servo_angle"];
    
    // Actuate ONLY if angle is not 90 (Neutral) and greater than 0
    if (servoAngle != 90 && servoAngle > 0) {
      Serial.print("Waste recognized! Tilting Servo to "); 
      Serial.print(servoAngle); 
      Serial.println(" degrees.");
      
      moveServo(servoAngle); // Tilt Left (30) or Right (150)
      delay(3000);   
      moveServo(90);         // Return to flat rest (90)
      delay(1000);   
    }
  }
  
  http.end();
  esp_camera_fb_return(fb);
}

// ==========================================
// 8. TELEGRAM SECURE UPLOAD
// ==========================================
void sendTelegramAlert() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed for Telegram");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Bypass SSL cert validation to save SRAM
  
  // Construct Multi-Part Form Data manually to save memory
  String head = "--BinSightBoundary\r\nContent-Disposition: form-data; name=\"chat_id\"; \r\n\r\n" + chatId + "\r\n--BinSightBoundary\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"intruder.jpg\"\r\nContent-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--BinSightBoundary--\r\n";
  
  uint32_t totalLen = head.length() + fb->len + tail.length();
  
  if (client.connect("api.telegram.org", 443)) {
    client.println("POST /bot" + telegramToken + "/sendPhoto HTTP/1.1");
    client.println("Host: api.telegram.org");
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=BinSightBoundary");
    client.println();
    
    // Write headers
    client.print(head);
    
    // Stream image buffer in 1024-byte chunks (Prevents ESP32 crashes!)
    uint8_t *fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n += 1024) {
      if (n + 1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      } else if (fbLen % 1024 > 0) {
        size_t remainder = fbLen % 1024;
        client.write(fbBuf, remainder);
      }
    }
    
    // Write tail and end
    client.print(tail);
    
    // Clear response buffer
    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }
    client.readString(); // Consume body
    client.stop();
    
    Serial.println("Telegram Alert Sent Successfully!");
  } else {
    Serial.println("Connection to Telegram failed.");
  }
  
  esp_camera_fb_return(fb);
}