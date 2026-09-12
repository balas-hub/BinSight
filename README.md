# BinSight: Edge-AI Powered Smart Waste Segregation and Surveillance System

[![Python](https://img.shields.io/badge/Python-3.10%2B-blue.svg)](https://www.python.org/)
[![YOLOv8](https://img.shields.io/badge/YOLOv8-Ultralytics-orange.svg)](https://docs.ultralytics.com/)
[![ESP32-CAM](https://img.shields.io/badge/Hardware-ESP32--CAM-red.svg)](https://www.espressif.com/)
[![License](https://img.shields.io/badge/License-CC%20BY%204.0-green.svg)](https://creativecommons.org/licenses/by/4.0/)

**BinSight** is a decentralized Edge-AI Internet of Things (IoT) system that bridges automated waste management and perimeter security into a single dual-purpose smart device.

1. **Automated Point-of-Source Segregation**: Identifies deposited waste items in real-time as **Biodegradable** or **Non-Biodegradable** using a custom-trained YOLOv8 computer vision model, mechanically actuating a trapdoor mechanism via PWM servo control.
2. **Autonomous Surveillance & Security**: Repurposes the sensor suite during off-hours into an active security device using an onboard PIR motion sensor. Upon detecting movement, the system captures an intruder snapshot, dispatches instant photo alerts to a **Telegram Bot**, and provides interactive remote commands and a live MJPEG stream over local Wi-Fi.

---

## System Architecture

```
                 +---------------------------------------------+
                 |               ESP32-CAM Node                |
                 |                                             |
                 |   [OV2640 Camera]        [PIR Sensor]       |
                 |          |                     |            |
                 |          v                     v            |
                 |     FSM Controller (GPIO 12 Interrupt)      |
                 |          |                     |            |
                 |   (Smart Bin Mode)     (Surveillance Mode)  |
                 +----------|---------------------|------------+
                            |                     |
             Raw JPEG Stream|                     | Chunked HTTPS Upload
             over HTTP POST |                     | (api.telegram.org:443)
                            v                     v
                 +--------------------+   +--------------------+
                 | Local Edge AI Host |   |  Telegram Bot API  |
                 | (Python/Flask:5000)|   +--------------------+
                 |                    |             |
                 | - YOLOv8 Inference |             v
                 | - Decision Logic   |   +--------------------+
                 | - OpenCV Desktop UI|   |  User Smartphone   |
                 +----------|---------+   |  (Alerts + Remote  |
                            |             |   Menu Controls)   |
                  JSON Command Payload    +--------------------+
                  {"servo_angle": 30/150}
                            |
                            v
                 +--------------------+
                 | Hardware Actuator  |
                 | SG90/MG996R Servo  |
                 | (Trapdoor Sorting) |
                 +--------------------+
```

---

## Hardware Pinout (AI-Thinker ESP32-CAM)

| Peripheral / Component | ESP32 GPIO Pin | Function / Description |
|---|---|---|
| **Mode Switch Button** | `GPIO 12` | Active LOW (`INPUT_PULLUP`), Hardware-debounced interrupt |
| **PIR Motion Sensor** | `GPIO 13` | Motion detection trigger in Surveillance Mode |
| **Servo Motor (PWM)** | `GPIO 14` / `GPIO 15` | Flap actuation (30° Bio, 150° Non-Bio, 90° Neutral) |
| **High-Power Flash LED** | `GPIO 4` | State indicator & nighttime illumination |
| **Camera Sensor (OV2640)** | Standard AI-Thinker bus | D0-D7, XCLK, PCLK, VSYNC, HREF, SIOD, SIOC |

---

## Machine Learning Pipeline

- **Dataset**: 2,333 images managed on [Roboflow Universe](https://universe.roboflow.com/balavigneshs-workspace/biodegradable-nonbiodegradable-fkrfj).
  - *In-Situ Collection*: Captured directly through the ESP32-CAM lens via a custom embedded web tool (`Dataset_collector.ino`) to match the exact focal length, distortion, and sensor noise characteristics.
- **Model Architecture**: YOLOv8 Nano (`yolov8n.pt`) fine-tuned for 25 epochs.
- **Performance**:
  - **Precision**: 98.27%
  - **Recall**: 96.46%
  - **mAP@50**: 96.93%
  - **mAP@50-95**: 93.43%
  - Final weights saved as `best.pt`.

---

## Project Structure

```
BinSight/
├── BinSight_Final_Report.docx      # Complete B.Tech final university report
├── BinSight_Final_Report.pdf       # Compiled final project report
├── FIRST REVIEW PPT.pptx           # Phase 1 presentation deck
├── Final reveiw ppt.pptx           # Final review presentation deck
├── binsight_complete_firmware/     # Master ESP32-CAM firmware (FSM, Telegram, MJPEG, AI)
│   └── binsight_complete_firmware.ino
├── BinSight_ESP32CAM/              # Dedicated smart bin client firmware
│   └── BinSight_ESP32CAM.ino
├── Dataset_collector/              # Embedded ESP32 web app for training image capture
│   └── Dataset_collector.ino
├── basic_server_esp/               # Minimalist prototype firmware
│   └── basic_server_esp.ino
├── BinSight_Training/              # ML training scripts and Roboflow configuration
│   ├── train.py
│   ├── data.yaml
│   ├── yolov8n.pt
│   └── runs/detect/train/weights/
│       ├── best.pt
│       └── binsight_server.py      # Edge AI Flask server (auto-IP discovery)
├── dataset/                        # Training, validation, and test image sets
└── model/                          # Confusion matrices, PR curves, and training logs
```

---

## Getting Started

### 1. Edge AI Server Setup (Local PC)

```bash
# Clone the repository
git clone https://github.com/qbit-soltions/BinSight.git
cd BinSight/BinSight_Training/runs/detect/train/weights

# Install dependencies
pip install flask opencv-python ultralytics numpy

# Start the edge server
python binsight_server.py
```
*The server will print its local LAN IP address (e.g., `192.168.1.100`). Update this IP in your ESP32 sketch.*

### 2. Microcontroller Firmware Setup (ESP32-CAM)

1. Open `binsight_complete_firmware/binsight_complete_firmware.ino` in **Arduino IDE**.
2. Select Board: **AI Thinker ESP32-CAM** (with PSRAM enabled).
3. Install required Arduino libraries:
   - `ArduinoJson` (v6.x or v7.x)
   - `UniversalTelegramBot`
4. Configure credentials in the sketch:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   const String serverUrl = "http://YOUR_SERVER_IP:5000/detect";
   const String telegramToken = "YOUR_TELEGRAM_BOT_TOKEN";
   const String chatId = "YOUR_TELEGRAM_CHAT_ID";
   ```
5. Connect your ESP32-CAM using an FTDI programmer (GPIO 0 grounded during upload) and flash the code.

---

## Authors & Acknowledgments

Developed by:
- **Balavignesh K** (E0225002)
- **Ashwanth Kumar R S** (E0225007)
- **Daniel** (E0225003)

Under the supervision of **Dr. Jayanthi G**, Assistant Professor,  
**Department of Cyber Security and Internet of Things**,  
Sri Ramachandra Faculty of Engineering and Technology,  
Sri Ramachandra Institute of Higher Education and Research, Porur, Chennai - 600116.
