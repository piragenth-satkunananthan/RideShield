package com.rideshield.phone.ui
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.viewModels
import com.rideshield.phone.ui.screens.DashboardScreen
import com.rideshield.phone.ui.theme.RideShieldTheme
class MainActivity : ComponentActivity() {
    private val viewModel: MainViewModel by viewModels()
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent { RideShieldTheme { DashboardScreen(viewModel) } }
    }
}
