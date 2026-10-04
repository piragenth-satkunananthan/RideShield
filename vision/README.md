# Computer Vision

## Front vision
Targets:
1. pothole detection
2. wet-road classification / risk estimation

Recommended workflow:
- public dataset pretraining
- own Sri Lankan road/helmet-camera data
- train and validate on laptop first
- keep held-out rides/locations for testing
- later quantize/deploy a lightweight model to ESP32-S3 if feasible

Important: avoid data leakage by splitting video data by ride/day/location rather than adjacent frames.

## Rear vision
Target:
- detect vehicles
- track them between frames
- estimate approach trend
- output compact metadata to the main safety node

Known limitation: a helmet-mounted rear camera rotates with the rider's head, so it is rear-view awareness rather than precise motorcycle-fixed blind-spot geometry.
