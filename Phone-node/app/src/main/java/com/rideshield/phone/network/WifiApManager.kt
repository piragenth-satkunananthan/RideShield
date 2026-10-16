package com.rideshield.phone.network

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest

/** Route this app over manually joined Wi-Fi, including an AP without Internet. */
class WifiApManager(context: Context) {
    private val cm = context.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
    private var callback: ConnectivityManager.NetworkCallback? = null
    private var generation = 0L
    private var bound: Network? = null
    @Synchronized fun bindToHelmetWifi(onConnected: () -> Unit, onError: (String) -> Unit, onLost: () -> Unit = {}) {
        unbind(); val attempt = generation
        val request = NetworkRequest.Builder().addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET).build()
        val listener = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) = synchronized(this@WifiApManager) {
                if (attempt != generation) return@synchronized
                if (cm.bindProcessToNetwork(network)) { bound = network; onConnected() }
                else { unbind(); onError("Could not route to Wi-Fi") }
            }
            override fun onLost(network: Network) = synchronized(this@WifiApManager) {
                if (attempt != generation || bound != network) return@synchronized
                bound = null; cm.bindProcessToNetwork(null); onLost()
            }
            override fun onUnavailable() = synchronized(this@WifiApManager) {
                if (attempt == generation) {
                    unbind()
                    onError("Join RideShield Wi-Fi in Android settings, then connect again")
                }
            }
        }
        callback = listener
        try { cm.requestNetwork(request, listener, 10000) }
        catch (e: RuntimeException) { unbind(); onError("Wi-Fi request failed: ${e.javaClass.simpleName}") }
    }
    @Synchronized fun unbind() {
        generation++; callback?.let { runCatching { cm.unregisterNetworkCallback(it) } }
        callback = null; bound = null; cm.bindProcessToNetwork(null)
    }
}
