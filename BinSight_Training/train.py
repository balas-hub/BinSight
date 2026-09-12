from ultralytics import YOLO

print("Initializing BinSight Local AI Training...")

# 1. Load the base YOLOv8 model (Nano version, best for ESP32 speed)
model = YOLO('yolov8n.pt') 

# 2. Start the training process
# Make sure data.yaml is in the same folder as this script!
results = model.train(
    data='data.yaml', 
    epochs=1,       # How many times it loops through your photos (25 is a good start)
    imgsz=640,       # The size to compress images to
    plots=True       # Generates cool graphs of your accuracy!
)

print("\n--- TRAINING COMPLETE! ---")
print("Check the 'runs/detect/train/weights/' folder for your best.pt file!")