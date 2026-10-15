package com.rideshield.phone.network

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.wifi.WifiManager
import android.os.Build
import android.util.Log

/**
 * Manages Wi-Fi Access Point connection routing on Android.
 * Binds network sockets to Wi-Fi even when the ESP32-S3 AP has no internet access,
 * preventing Android from routing packets through cellular data.
 */
class WifiApManager(private val context: Context) {
    private val tag = "WifiApManager"
    private val connectivityManager =
        context.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager
    private val wifiManager =
        context.applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager

    private var networkCallback: ConnectivityManager.NetworkCallback? = null

    /**
     * Requests Android to route app socket traffic through Wi-Fi transport.
     */
    fun bindToHelmetWifi(onConnected: () -> Unit = {}, onError: (String) -> Unit = {}) {
        val cm = connectivityManager ?: run {
            onError("ConnectivityManager unavailable")
            return
        }

        val request = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) // ESP32 AP has no internet
            .build()

        networkCallback = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) {
                Log.i(tag, "Helmet Wi-Fi network available. Binding process to network.")
                val success = cm.bindProcessToNetwork(network)
                if (success) {
                    Log.i(tag, "Successfully bound process to Helmet Wi-Fi.")
                    onConnected()
                } else {
                    Log.w(tag, "Failed to bind process to network.")
                    onError("Failed to bind process to Wi-Fi")
                }
            }

            override fun onLost(network: Network) {
                Log.w(tag, "Helmet Wi-Fi network lost.")
                cm.bindProcessToNetwork(null)
            }

            override fun onUnavailable() {
                Log.w(tag, "Helmet Wi-Fi network request unavailable.")
                onError("Wi-Fi network unavailable")
            }
        }

        try {
            cm.requestNetwork(request, networkCallback!!)
        } catch (e: Exception) {
            Log.e(tag, "Error requesting Wi-Fi network: ${e.message}")
            onError(e.message ?: "Network request failed")
        }
    }

    /**
     * Unbinds process from specific network.
     */
    fun unbind() {
        networkCallback?.let {
            try {
                connectivityManager?.unregisterNetworkCallback(it)
                connectivityManager?.bindProcessToNetwork(null)
            } catch (e: Exception) {
                Log.w(tag, "Error unregistering callback: ${e.message}")
            }
            networkCallback = null
        }
    }

    /**
     * Reads current connected Wi-Fi SSID for diagnostics.
     */
    fun getCurrentSsid(): String {
        return try {
            val info = wifiManager?.connectionInfo
            val ssid = info?.ssid?.replace("\"", "") ?: "UNKNOWN"
            if (ssid == "<unknown ssid>") "Connected (Hidden/AP)" else ssid
        } catch (e: Exception) {
            "UNKNOWN"
        }
    }
}
