import cv2
import numpy as np
from flask import Flask, request, jsonify
from ultralytics import YOLO
import threading
import time

# Initialize Server and Load Model
app = Flask(__name__)
print("Loading Local YOLOv8 Engine...")
model = YOLO('best.pt')
print("Model loaded successfully! Ready for edge-processing.")

# --- ELECTROMECHANICAL CONFIGURATION ---
# Map the waste types to specific Servo Motor angles (0 to 180 degrees)
# 90 degrees is the neutral flat resting position.
SERVO_ANGLES = {
    "biodegradable": 30,          # Tilt Left to drop Bio waste
    "non biodegradable": 150,     # Tilt Right to drop Non-Bio waste
    "unknown": 0                  # 0 means "Do nothing"
}

# Global variable to store the latest video frame securely
latest_frame = None

def generate_reply_payload(detected_class, confidence):
    """
    Function to determine the action to be performed based on the kind of waste 
    detected and generate the formatted reply for the ESP32.
    """
    # Normalize the detected class to handle underscores or hyphens
    normalized_class = detected_class.replace("_", " ").replace("-", " ")
    
    angle = 0
    # Sort keys by length (longest first) to ensure "non biodegradable" is checked BEFORE "biodegradable"
    for key in sorted(SERVO_ANGLES.keys(), key=len, reverse=True):
        if key != "unknown" and key in normalized_class:
            angle = SERVO_ANGLES[key]
            detected_class = key # Clean up the name for the ESP32 reply
            break
    
    # Determine the human-readable action description
    if angle > 0:
        action_performed = f"Sort into {detected_class.capitalize()} bin"
    else:
        action_performed = "Do not sort (Unknown item)"
        
    # Build the response dictionary
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
    
    # 1. Read the raw JPEG bytes directly from the ESP32
    file_bytes = request.data
    
    if not file_bytes:
        return jsonify({"error": "No image data received"}), 400
    
    # 2. Convert the raw bytes to an OpenCV image
    npimg = np.frombuffer(file_bytes, np.uint8)
    img = cv2.imdecode(npimg, cv2.IMREAD_COLOR)

    # 3. Ultra-Low Latency Inference
    results = model.predict(source=img, conf=0.6, save=False, verbose=False)
    
    # --- UPDATE GLOBAL FRAME FOR THE LIVE STREAM ---
    latest_frame = results[0].plot()
    # -------------------------------------------------

    detected_class = "unknown"
    confidence = 0.0

    # 4. Parse the Results
    if len(results) > 0 and len(results[0].boxes) > 0:
        first_box = results[0].boxes[0]
        confidence = float(first_box.conf[0])
        class_id = int(first_box.cls[0])
        detected_class = model.names[class_id].lower()
        
        # Debugging step: Show exactly what YOLO found
        print(f"[DEBUG] YOLO spotted: '{detected_class}' at {confidence*100:.1f}% confidence")

    # 5. Use our function to generate the reply and determine the action
    reply_payload, angle, action_performed = generate_reply_payload(detected_class, confidence)

    # Print action if successful, or warn if the item wasn't in the dictionary
    if angle > 0:
        print(f"[ACTION] {action_performed} -> Sending Servo to {angle}°\n")
    elif detected_class != "unknown":
        print(f"[WARNING] '{detected_class}' is not assigned an angle in SERVO_ANGLES! ESP32 will ignore this.\n")

    # 6. Return JSON command payload to the ESP32
    return jsonify(reply_payload)

def start_flask():
    # use_reloader=False prevents the background thread from crashing
    app.run(host='0.0.0.0', port=5000, use_reloader=False)

if __name__ == '__main__':
    # 1. Start the Flask web server in a background thread
    flask_thread = threading.Thread(target=start_flask)
    flask_thread.daemon = True
    flask_thread.start()

    print("\nStarting BinSight Edge AI Server...")
    print("Listening for ESP32-CAM stream on port 5000...")
    print("Press 'q' inside the video window to quit.\n")

    # 2. Main thread strictly handles the OpenCV Live Stream window
    while True:
        if latest_frame is not None:
            cv2.imshow("BinSight ESP32 Live Stream", latest_frame)
        
        # This keeps the video window refreshing and responsive
        if cv2.waitKey(1) & 0xFF == ord('q'):
            break
            
    cv2.destroyAllWindows()