/**
 * LibreLinkUp Configuration
 * 
 * Store your LibreLinkUp credentials here.
 * In a production system, these should be stored in NVS or obtained via provisioning.
 */

#ifndef LIBRE_CONFIG_H
#define LIBRE_CONFIG_H

// LibreLinkUp credentials
// IMPORTANT: Replace these with your actual LibreLinkUp credentials
#define LIBRE_EMAIL    "your-email@example.com"
#define LIBRE_PASSWORD "your-password"

// Use EU server (true) or global server (false)
#define LIBRE_USE_EU_SERVER true

// Refresh interval in seconds (default: 5 minutes)
#define LIBRE_REFRESH_INTERVAL_SEC 300

#endif // LIBRE_CONFIG_H
