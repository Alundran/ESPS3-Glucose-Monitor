/**
 * Global Configuration for Glucose Monitor
 * Device-wide settings and constants
 */

#ifndef CONFIG_H
#define CONFIG_H

// Device Information
#define DEVICE_NAME "The Supreme's Glucose Monitor"
#define DEVICE_NAME_SHORT "Supreme-GM"  // Used for WiFi AP SSID (no spaces)
#define DEVICE_VERSION "1.0.13"
#define DEVICE_MANUFACTURER "Spalding (Derek Marr)"
#define DEVICE_OWNER "The Supreme (Stephen Higgins)"

// Display Configuration
#define DEVICE_TITLE "The Supreme's\nGlucose Monitoring"

// WiFi Configuration
#define WIFI_AP_SSID DEVICE_NAME_SHORT
#define WIFI_AP_PASSWORD "CatGotYourTongue"

// Glucose Thresholds (mmol/L)
#define GLUCOSE_LOW_THRESHOLD 3.9
#define GLUCOSE_HIGH_THRESHOLD 13.3

// Demo Mode - uses dummy data instead of real API calls
#define DEMO_MODE_ENABLED false

// Moon Lamp Configuration
// Note: Moon Lamp is now controlled via Global Settings in web interface
// Default value is set in global_settings.h (DEFAULT_MOON_LAMP_ENABLED)

// Demo Mode Configuration
// Change these values to test different glucose scenarios
#define DEMO_GLUCOSE_MMOL 6.5    // Normal: 4.0-7.8, Low: <3.9, High: >10.0
#define DEMO_GLUCOSE_LOW false   // Set to true to test low glucose display
#define DEMO_GLUCOSE_HIGH false  // Set to true to test high glucose display
#define DEMO_TREND "*"            // Trend arrow (currently using * as placeholder)

#endif // CONFIG_H
