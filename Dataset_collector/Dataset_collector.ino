#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

// ==========================================
// 1. WI-FI CREDENTIALS (CHANGE THESE)
// ==========================================
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// ==========================================
// 2. CAMERA PINOUT (AI-THINKER MODEL)
// ==========================================
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

// Flash LED is connected to GPIO 4
#define FLASH_GPIO_NUM     4

WebServer server(80);
bool flashState = false;

// ==========================================
// 3. WEBPAGE HTML & JAVASCRIPT
// ==========================================
// Minimalist, human-readable HTML with live preview logic
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>BinSight Data Collector</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background: #e9ecef; margin: 0; padding: 20px; }
    .container { background: white; max-width: 500px; margin: 0 auto; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
    h1 { color: #333; margin-top: 0; }
    #preview { width: 100%; max-width: 400px; background: #000; border-radius: 8px; margin-bottom: 15px; border: 3px solid #ccc; min-height: 250px; }
    button { font-size: 16px; padding: 12px 10px; margin: 5px; cursor: pointer; border: none; border-radius: 5px; color: white; width: 30%; font-weight: bold; }
    #captureBtn { background: #28a745; }
    #rapidBtn { background: #007bff; }
    #flashBtn { background: #ffc107; color: #333; }
    .stop { background: #dc3545 !important; }
    #status { margin-top: 15px; font-size: 16px; color: #666; }
  </style>
</head>
<body>
  <div class="container">
    <h1>📷 BinSight Collector</h1>
    
    <!-- Live Camera Preview -->
    <img id="preview" src="" alt="Camera Loading...">
    <br>
    
    <!-- Controls -->
    <button id="captureBtn" onclick="takeSinglePicture()">📸 Capture 1</button>
    <button id="rapidBtn" onclick="toggleRapidFire()">🚀 Rapid Fire</button>
    <button id="flashBtn" onclick="toggleFlash()">💡 Toggle Flash</button>
    
    <div id="status">Starting camera...</div>
  </div>

  <script>
    let imgCount = 0;
    let rapidFiring = false;
    let currentObjectUrl = null;

    const previewImg = document.getElementById('preview');
    const statusText = document.getElementById('status');

    // 1. Function to continuously fetch frames for the live preview
    async function streamCamera() {
      try {
        const response = await fetch('/capture');
        if (response.ok) {
          const blob = await response.blob();
          
          // Free old memory before creating a new image URL
          if (currentObjectUrl) {
            URL.revokeObjectURL(currentObjectUrl); 
          }
          
          currentObjectUrl = URL.createObjectURL(blob);
          previewImg.src = currentObjectUrl; // Update image on screen

          // If rapid firing is ON, automatically save this frame
          if (rapidFiring) {
            saveImage(currentObjectUrl);
          }
          
          if (!rapidFiring) {
            statusText.innerText = "Camera Active. Ready to capture.";
          }
        }
      } catch (err) {
        statusText.innerText = "Camera disconnected. Retrying...";
      }
      
      // Wait 100ms, then grab the next frame (Creates a video effect)
      setTimeout(streamCamera, 100); 
    }

    // 2. Function to trigger the browser to download the image
    function saveImage(url) {
      imgCount++;
      const a = document.createElement('a');
      a.href = url;
      a.download = 'binsight_sample_' + Date.now() + '.jpg';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
      statusText.innerText = "Saved image #" + imgCount;
    }

    // 3. Button Click Events
    function takeSinglePicture() {
      if (currentObjectUrl) {
         saveImage(currentObjectUrl);
      }
    }

    function toggleRapidFire() {
      rapidFiring = !rapidFiring;
      const btn = document.getElementById('rapidBtn');
      
      if (rapidFiring) {
        btn.innerText = "🛑 Stop Rapid";
        btn.classList.add('stop');
      } else {
        btn.innerText = "🚀 Rapid Fire";
        btn.classList.remove('stop');
        statusText.innerText = "Rapid Fire Stopped.";
      }
    }
    
    async function toggleFlash() {
      try {
        const response = await fetch('/flash');
        if (response.ok) {
          const text = await response.text();
          statusText.innerText = "Flash is now " + text;
        }
      } catch (err) {
        statusText.innerText = "Failed to toggle flash.";
      }
    }

    // Start streaming as soon as the page loads!
    window.onload = streamCamera;
  </script>
</body>
</html>
)rawliteral";

// ==========================================
// 4. SERVER ROUTING FUNCTIONS
// ==========================================

// Serve the HTML webpage
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

// Serve the raw JPEG image bytes when requested
void handleCapture() {
  camera_fb_t * fb = NULL;
  
  // Take a picture
  fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Camera capture failed");
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  // Send the image to the browser
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  
  // Clear the camera buffer to free memory
  esp_camera_fb_return(fb);
}

// Toggle the ESP32-CAM's built in LED flash
void handleFlash() {
  flashState = !flashState;
  digitalWrite(FLASH_GPIO_NUM, flashState ? HIGH : LOW);
  server.send(200, "text/plain", flashState ? "ON" : "OFF");
}

// ==========================================
// 5. MAIN SETUP AND LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial.println();

  // Initialize Flash LED pin
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);

  // Configure Camera Settings
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Resolution: SVGA is 800x600. Good balance of quality and speed for dataset training.
  if(psramFound()){
    config.frame_size = FRAMESIZE_SVGA; 
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Initialize Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! Open this URL in your browser: http://");
  Serial.println(WiFi.localIP());

  // Setup Web Server Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/flash", HTTP_GET, handleFlash);
  
  // Start the server
  server.begin();
  Serial.println("Web server started.");
}

void loop() {
  // Listen for incoming requests from the browser
  server.handleClient();
}