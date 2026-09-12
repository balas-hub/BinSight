from ultralytics import YOLO

# Load your custom trained model
model = YOLO('best.pt')

# Run prediction on your webcam (source=0)
model.predict(source=0, show=True)