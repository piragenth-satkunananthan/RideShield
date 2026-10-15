# RideShield Model Conversion Guide: PyTorch `best.pt` to On-Device Formats

This guide documents the exact conversion pipeline for transforming the custom-trained pothole model (`best.pt`) in `Pothole-Computer-Vision-Project` into phone-compatible formats: **ONNX** and **TensorFlow Lite (TFLite)**.

---

## 1. Model Architecture & Compatibility Patch

The `best.pt` model in the repository is a **YOLOv12s** object detection model with Area Attention (`AAttn`). 

### The `AAttn` Compatibility Issue:
Recent Ultralytics releases introduce architectural shifts in attention heads. Attempting to export directly with standard `yolo export` produces:
```text
AttributeError: 'AAttn' object has no attribute 'qkv'
```
To resolve this, the forward method of `ultralytics.nn.modules.block.AAttn` must be patched before loading the checkpoint weights, as implemented in `Pothole-Computer-Vision-Project/app.py`.

---

## 2. Step-by-Step Conversion to ONNX

### Step 1: Environment Setup
Using the existing Python 3.12 environment in `Pothole-Computer-Vision-Project/.venv`:
```bash
cd /home/piragenth/Documents/rideshield/Phone-node
../Pothole-Computer-Vision-Project/.venv/bin/pip install onnx onnxruntime onnxslim
```

### Step 2: Run Automated Conversion Script
Execute the provided converter script:
```bash
../Pothole-Computer-Vision-Project/.venv/bin/python scripts/convert_best_pt.py \
    --input ../Pothole-Computer-Vision-Project/best.pt \
    --output app/src/main/assets/models/best.onnx \
    --imgsz 640 \
    --opset 12
```

### Step 3: Verify ONNX Model Graph
Inspect the exported model using Netron (https://netron.app) or Python:
- **Input Name**: `images`
- **Input Shape**: `[1, 3, 640, 640]` (Float32, normalized RGB `[0.0, 1.0]`)
- **Output Name**: `output0`
- **Output Shape**: `[1, 5, 8400]` (5 channels: `cx, cy, w, h, confidence` across 8,400 anchor-free candidate grid cells)
- **Class Mapping**: Index `0` $\to$ `Pothole`

---

## 3. TensorFlow Lite (TFLite) Export Option

If using TensorFlow Lite instead of ONNX Runtime:
```bash
../Pothole-Computer-Vision-Project/.venv/bin/python -c "
import ultralytics.nn.modules.block as ultralytics_block
# Apply AAttn patch...
from ultralytics import YOLO
model = YOLO('../Pothole-Computer-Vision-Project/best.pt')
model.export(format='tflite', imgsz=640, int8=False)
"
```
The resulting `best_saved_model/best_float32.tflite` can be placed into `app/src/main/assets/models/best.tflite`.

---

## 4. Mobile Execution & Hardware Acceleration

### ONNX Runtime Mobile Features:
1. **CPU Execution Provider (Default)**:
   Utilizes multi-threaded ARM NEON / XNNPACK intrinsics. Average inference latency on modern ARM processors is **40–75 ms** per 640x640 frame.
2. **NNAPI Execution Provider**:
   Leverages mobile NPUs (Qualcomm Hexagon, MediaTek APU, Google Tensor) for sub-25 ms latency.
3. **Pre-processing (Letterboxing)**:
   Images must be scaled to 640x640 preserving aspect ratio, with 114/255 grey padding, in planar NCHW format.

---

## 5. Unavailable Models Notice

- **Wet-Road Classification**: No model weights exist in repository. The Android app implements `UnavailableWetRoadClassifier`, which reports status `UNAVAILABLE` and strictly emits **zero** invented hazards.
- **Rear Vehicle Detection**: No model weights exist in repository. The Android app implements `UnavailableRearVehicleDetector`, which reports status `UNAVAILABLE` and strictly emits **zero** invented vehicles or tracks.
