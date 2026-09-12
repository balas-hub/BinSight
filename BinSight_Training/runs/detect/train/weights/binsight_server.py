import cv2
import numpy as np
from flask import Flask, request, jsonify
from ultralytics import YOLO
import threading
import socket

# ==========================================
# Print local IP so it's easy to copy into the ESP32 sketch
# ==========================================
def get_local_ip():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
    except Exception:
        ip = "127.0.0.1"
    finally:
        s.close()
    return ip

local_ip = get_local_ip()
print("=" * 50)
print(f"This PC's local IP address: {local_ip}")
print("Set this exact value as serverIP in the ESP32 sketch.")
print("=" * 50)

# ==========================================
# Load YOLO model
# ==========================================
app = Flask(__name__)
print("Loading Local YOLOv8 Engine...")
model = YOLO('best.pt')
print("Model loaded successfully! Ready for edge-processing.")

# ==========================================
# Servo angle mapping per waste class
# ==========================================
SERVO_ANGLES = {
    "biodegradable": 45,          # Bin 1
    "non biodegradable": 135,     # Bin 2
    "unknown": 0                  # Default/Reject position
}

latest_frame = None
frame_lock = threading.Lock()


def generate_reply_payload(detected_class, confidence):
    normalized_class = detected_class.replace("_", " ").replace("-", " ")

    angle = 0
    # Check "non biodegradable" before "biodegradable" since one is a substring of the other
    for key in sorted(SERVO_ANGLES.keys(), key=len, reverse=True):
        if key != "unknown" and key in normalized_class:
            angle = SERVO_ANGLES[key]
            detected_class = key
            break

    if angle > 0:
        action_performed = f"Sort into {detected_class.capitalize()} bin"
    else:
        action_performed = "Do not sort (Unknown item)"

    payload = {
        "status": "success",
        "item": detected_class,
        "confidence": confidence,
        "servo_angle": angle,
        "action": action_performed
    }
    return payload, angle, action_performed


@app.route('/detect', methods=['POST'])
def detect_waste():
    global latest_frame

    file_bytes = request.data
    if not file_bytes:
        return jsonify({"error": "No image data received"}), 400

    npimg = np.frombuffer(file_bytes, np.uint8)
    img = cv2.imdecode(npimg, cv2.IMREAD_COLOR)
    if img is None:
        return jsonify({"error": "Invalid image data"}), 400

    results = model.predict(source=img, conf=0.6, save=False, verbose=False)

    with frame_lock:
        latest_frame = results[0].plot()

    detected_class = "unknown"
    confidence = 0.0

    if len(results) > 0 and len(results[0].boxes) > 0:
        first_box = results[0].boxes[0]
        confidence = float(first_box.conf[0])
        class_id = int(first_box.cls[0])
        detected_class = model.names[class_id].lower()
        print(f"[DEBUG] YOLO spotted: '{detected_class}' at {confidence*100:.1f}% confidence")

    reply_payload, angle, action_performed = generate_reply_payload(detected_class, confidence)

    if angle > 0:
        print(f"[ACTION] {action_performed} -> Sending Servo to {angle}°\n")
    elif detected_class != "unknown":
        print(f"[WARNING] '{detected_class}' has no assigned angle!\n")

    return jsonify(reply_payload)


@app.route('/ping', methods=['GET'])
def ping():
    return jsonify({"status": "alive", "server_ip": local_ip})


def start_flask():
    app.run(host='0.0.0.0', port=5000, use_reloader=False)


if __name__ == '__main__':
    flask_thread = threading.Thread(target=start_flask)
    flask_thread.daemon = True
    flask_thread.start()

    print("\nStarting BinSight Edge AI Server...")
    print("Listening for ESP32-CAM stream on port 5000...")
    print(f"Test from a phone/PC on the same Wi-Fi: http://{local_ip}:5000/ping")
    print("Press 'q' inside the video window to quit.\n")

    while True:
        with frame_lock:
            frame_to_show = latest_frame.copy() if latest_frame is not None else None

        if frame_to_show is not None:
            cv2.imshow("BinSight ESP32 Live Stream", frame_to_show)

        if cv2.waitKey(1) & 0xFF == ord('q'):
            break

    cv2.destroyAllWindows()
