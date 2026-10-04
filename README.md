##Core system 

             RIDESHIELD SEMI-FINAL CORE

                   ESP32-S3
                       │
       ┌───────────────┼────────────────┐
       │               │                │
    MPU6050        Helmet-worn          GPS
                   ToF/IR + strap
       │               │                │
       └───────────────┼────────────────┘
                       ↓
                 SAFETY ENGINE
                       │
               Possible crash
                       ↓
             Verify crash conditions
                       ↓
             Buzzer / vibration
                       ↓
                Cancel countdown
                  │           │
                CANCEL      NO CANCEL
                  │           ↓
                  │      Event severity
                  │        LOW/MED/HIGH
                  │           ↓
                  │       GPS location
                  │           ↓
                  │       LTE → SMS
                  │
                  └────→ microSD log




                         RIDESHIELD

                ┌────────────────────────┐
                │ MAIN SAFETY CONTROLLER │
                │      ESP32-S3          │
                └───────────┬────────────┘
                            │
        ┌───────────────────┼────────────────────┐
        ↓                   ↓                    ↓
     MPU6050            HELMET WORN             GPS
                    ToF/IR + strap               │
        │                   │                    │
        └───────────────────┼────────────────────┘
                            ↓
                    CRASH STATE MACHINE
                            │
                    ┌───────┼─────────┐
                    ↓       ↓         ↓
                 Warning  microSD    LTE
                                     │
                                     ↓
                               Emergency SMS








              FRONT VISION NODE
              ESP32-S3 + Camera
                      │
             ┌────────┴─────────┐
             ↓                  ↓
         Pothole             Wet-road
         detection           detection
             │                  │
             └────────┬─────────┘
                      ↓
                Road-risk data
                      ↓
                Main controller


              REAR VISION NODE
              ESP32-S3 + Camera
                      │
             vehicle detection
                      ↓
             approach awareness
                      ↓
                Main controller
