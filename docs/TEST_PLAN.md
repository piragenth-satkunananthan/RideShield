# Semi-Final Test Plan

The semi-final requires Track A physical validation. Use safe, controlled tests only.

## 1. Helmet-worn logic
Test all combinations:
- head absent + strap open -> not worn
- head present + strap open -> not properly worn
- head absent + strap closed -> not properly worn
- head present + strap closed -> worn

Record actual results.

## 2. Normal movement rejection
Move and tilt the helmet normally. The system should not enter a confirmed crash state.

Record:
- number of trials
- false crash triggers
- maximum observed acceleration / rotation if available

## 3. Controlled impact POC
Use a fixture/dummy or bench test, never a person.

Record:
- impact candidate detected?
- orientation/rotation response
- countdown started?
- event score/class
- false/missed detections

## 4. Rider cancellation
Trigger a test event and press cancel during the countdown.

Expected:
- emergency alert suppressed
- event retained in log as cancelled/test event

## 5. Emergency alert
Where implemented:
- GPS fix available?
- SMS sent?
- delivery latency
- whether the ESP32 reset during LTE transmission

## 6. Black-box logging
Verify that event data can be recovered after a test:
- timestamp
- acceleration/gyro
- GPS data
- state transition
- event score
- cancel/confirm outcome

## 7. Pothole model
Use a held-out test set or unseen road sequence.

Report:
- test images/sequences
- true detections
- missed potholes
- false detections
- precision/recall if enough labels are available

## 8. Wet-road model
At minimum distinguish dry vs wet. If data supports it, use dry/damp/wet/standing-water.

Report a confusion matrix or simple correct/incorrect counts.

## Results rule
Do not invent performance values. Imperfect measured results are stronger semi-final evidence than unsupported claims.
