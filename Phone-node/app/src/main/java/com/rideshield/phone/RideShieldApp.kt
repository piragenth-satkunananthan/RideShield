package com.rideshield.phone

import android.app.Application
import android.util.Log

class RideShieldApp : Application() {
    override fun onCreate() {
        super.onCreate()
        Log.i("RideShieldApp", "RideShield Android Application Initialized")
    }
}
