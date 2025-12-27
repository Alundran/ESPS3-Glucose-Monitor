# ESP32-S3-BOX-3 Glucose Monitor

A real-time continuous glucose monitoring display device that connects to LibreLinkUp to display glucose readings on the ESP32-S3-BOX-3 touchscreen. Built by Spalding for Stephen "Supreme" Higgins.

**🎭 Features an American Horror Story (AHS) theme** with random AHS quotes on swipe-up gesture and hidden surprise screens!

## Hardware Setup

- **Device**: ESP32-S3-BOX-3
- **Display**: 320x240 ILI9341 LCD with LVGL interface and capacitive touch
- **IR Transmitter Pin**: GPIO39 (for Moon Lamp control)
- **WiFi**: Built-in ESP32-S3 WiFi for LibreLinkUp connectivity

## Features

### Core Glucose Monitoring
- **Real-time Glucose Display**: Fetches and displays current glucose readings from LibreLinkUp API
- **Trend Indicators**: Shows glucose trend with stacked double arrows (rising quickly ↑↑, falling quickly ↓↓, etc.)
- **Status Alerts**: Visual indicators with color-coded status (HYPO/NORMAL/HIGH GLUCOSE)
- **Configurable Thresholds**: Customizable low (default 3.9 mmol/L) and high (default 13.3 mmol/L) thresholds
- **Glucose History Graph**: Swipe left to view 24-hour glucose trend graph with min/mid/max labels
- **Stale Data Detection**: Alerts when glucose data is older than 5 minutes

### 🎭 American Horror Story Theme
- **Random AHS Quotes**: Swipe up from glucose screen to see random quotes from AHS seasons
- **Episode Attribution**: Each quote displays character and episode information
- **Hidden Surprise Screen**: Triple-tap the glucose screen for a special AHS-themed surprise
- **Themed UI**: Custom splash screen and interface elements

### Interactive Gestures
- **Swipe Down**: Show date/time and moon lamp status
- **Swipe Up**: Display random American Horror Story quote
- **Swipe Left**: View 24-hour glucose history graph
- **Triple Tap**: Reveal hidden AHS surprise screen
- **Tap to Return**: Tap any alternate screen to return to glucose display

### Moon Lamp Integration
- **Automatic Color Control**: Changes moon lamp color via IR based on glucose levels
  - **Red**: High or low glucose (out of range)
  - **Green**: Normal glucose (within target range)
- **Manual IR Testing**: Web interface includes IR command testing with customizable address/command codes
- **Smart Control**: Only transmits when glucose state changes (prevents unnecessary IR spam)
- **Enable/Disable**: Toggle moon lamp control via web settings

### Web Interface & Configuration
- **WiFi Provisioning**: Easy captive portal setup for WiFi credentials
- **Global Settings Page**: Configure update interval, glucose thresholds, and moon lamp
- **IR Command Testing**: Send custom IR commands to test moon lamp colors (ON/OFF/RED/GREEN/WHITE/SMOOTH)
- **OTA Update Management**: Check for and install firmware updates with progress bar
- **LibreLink Setup**: Web-based credential configuration with server region selection (US/EU)
- **QR Code Configuration**: Tap "Configure" button in settings to display QR code for easy mobile access

### Network & Updates
- **DNS Retry Logic**: Automatic retry with exponential backoff for network failures after OTA updates
- **Network Stabilization**: 2-second delay after WiFi connection to ensure DNS is ready
- **OTA Firmware Updates**: GitHub-based automatic updates with version checking
- **Update Safety**: Progress screen, error handling, and memory optimization for reliable updates
- **Version Management**: NVS versioning prevents corruption after OTA structure changes

### Device Management
- **Red Button Control**: Single press toggles between glucose display and settings menu
- **Settings Menu**: 
  - **About**: Device information and build details
  - **Configure**: QR code for easy web access to settings
  - **Reset Device**: Clear all credentials and restart
- **Graceful Error Handling**: Connection failures show retry options
- **Demo Mode**: Optional demo mode for testing without LibreLink credentials


## How It Works

### First Boot & Setup
1. **Splash Screen**: Device displays custom Supreme Glucose splash screen for 2 seconds
2. **About Screen**: Shows device information with "Next" button to proceed
3. **WiFi Access Point**: Device creates "GlucoseMonitor" AP for initial configuration
4. **Captive Portal**: Connect to AP and web portal automatically opens for WiFi setup
5. **LibreLink Setup**: After WiFi connects, scan QR code to configure LibreLinkUp credentials

### Normal Operation
1. **Startup Checks**: 
   - Connects to WiFi (20-second timeout with retry option)
   - Checks for OTA firmware updates (non-blocking)
   - Waits for OTA check to complete before glucose fetch
2. **Glucose Display**: Shows current reading with trend arrows and color-coded status
3. **Automatic Updates**: Fetches new data at configured interval (default 5 minutes)
4. **Moon Lamp Control**: Automatically adjusts IR lamp color based on glucose state
5. **Interactive Gestures**: Swipe in different directions for alternate screens

### Gesture Navigation
- **Main Glucose Screen**:
  - Swipe ↓ = Date/time + moon lamp status
  - Swipe ↑ = Random AHS quote
  - Swipe ← = 24-hour glucose graph
  - Triple tap = Hidden surprise
- **Alternate Screens**: Tap anywhere to return to glucose display
- **Graph Screen**: Swipe or tap to return to main screen

### Settings & Configuration
1. **Red Button Press**: Opens settings menu with three options
2. **About**: View device info, version, and build details
3. **Configure**: Display QR code pointing to web interface (http://device-ip)
4. **Reset Device**: Clear all stored credentials and restart

### Web Interface Access
1. **Via QR Code**: Tap "Configure" in settings menu, scan QR code with phone
2. **Via Browser**: Navigate to device IP address while on same WiFi network
3. **Available Pages**:
   - Home menu with navigation links
   - Global Settings: Update interval, glucose thresholds, moon lamp toggle
   - IR Command Testing: Send custom IR commands to test moon lamp
   - WiFi Configuration: Change WiFi credentials
   - LibreLink Setup: Update email, password, server region
   - OTA Updates: Check for and install firmware updates

### OTA Update Process
1. **Automatic Check**: Device checks GitHub releases 5 seconds after WiFi connects
2. **Update Notification**: If newer version found, shows "Update Now" / "Later" buttons
3. **Update Progress**: 
   - Shows 0% immediately when "Update Now" is clicked
   - Displays progress bar during download (buffer optimized for ESP32-S3 memory)
   - "DO NOT DISCONNECT POWER!" warning displayed throughout
4. **Installation**: Device writes firmware to OTA partition and verifies
5. **Reboot**: Automatic restart when update completes
6. **Network Stability**: 2-second delay + retry logic ensures DNS is ready after reboot

## Configuration

### WiFi Setup
- **Access Point**: Device creates AP named "GlucoseMonitor" (password in config.h)
- **Captive Portal**: Automatically redirects to setup page when connected
- **WiFi Credentials**: Enter SSID and password via web interface
- **Connection Timeout**: 20 seconds with retry/restart options on failure

### LibreLink Credentials
- **Access Web Interface**: 
  - Navigate to `http://<device-ip>` after WiFi connection, OR
  - Press red button → Configure → Scan QR code
- **Required Information**:
  - LibreLinkUp email address
  - LibreLinkUp password
  - Server region (US or EU)
  - Patient ID (optional - will be auto-detected)
- **Authentication**: Uses JWT tokens stored in NVS for ~6 months
- **Regional Redirect**: Automatically redirects to correct regional server

### Global Settings (Web Interface)
- **Update Interval**: 
  - Default: 5 minutes
  - Range: 1-60 minutes
  - Minimum 1 minute to avoid API rate limits
- **Glucose Thresholds**:
  - Low threshold: Default 3.9 mmol/L (adjustable 1.0-20.0)
  - High threshold: Default 13.3 mmol/L (adjustable 5.0-30.0)
  - Used for status calculation (HYPO/NORMAL/HIGH GLUCOSE)
  - Triggers moon lamp color changes
- **Moon Lamp Control**:
  - Toggle enable/disable
  - IR commands sent only when glucose state changes
  - Prevents spamming IR transmissions

### IR Command Testing
- **Web Interface Tool**: Test IR commands directly from settings page
- **Parameters**:
  - Address: Pre-populated with 0xFF00 (NEC protocol)
  - Command: Hex codes for different functions
- **Common Commands**:
  - `40` = ON
  - `5C` = OFF
  - `58` = RED
  - `59` = GREEN
  - `44` = WHITE
  - `17` = SMOOTH (color cycling)
  - `5D` = Increase brightness
  - `41` = Decrease brightness
- **Use Case**: Test moon lamp compatibility and find color codes before automating

### Red Button Functions
- **Single Press**: Toggle between glucose display and settings screen
- **Settings Menu Options**:
  - **About**: Device version, manufacturer, build info
  - **Configure**: QR code for web interface access
  - **Reset Device**: Clears WiFi + LibreLink credentials, restarts device

### Firmware Updates (OTA)
- **Automatic Check**: 
  - Runs 5 seconds after WiFi connects on boot
  - Non-blocking - glucose fetch waits for OTA check to complete
  - Volatile flags prevent race conditions between tasks
- **Manual Check**: 
  - Navigate to Settings page in web interface
  - Click "Check for Updates" button
  - Shows current version → new version if available
- **Update Source**: 
  - GitHub releases at `https://github.com/Alundran/ESPS3-Glucose-Monitor`
  - Compares semantic versions (e.g., 1.0.11 vs 1.0.12)
  - Downloads `.bin` file from release assets
- **Update Process**:
  1. User clicks "Update Now" (or "Later" to postpone)
  2. Progress screen shows 0% immediately
  3. 500ms delay for UI rendering
  4. Downloads firmware with progress updates
  5. Flashes to OTA partition with verification
  6. **⚠️ WARNING: Do NOT disconnect power during update!**
  7. Automatic reboot when complete
- **Memory Optimization**:
  - Buffer sizes: 4096 bytes (receive) / 2048 bytes (transmit)
  - 60-second timeout for large downloads (~3.9MB firmware)
  - Bulk flash erase for performance
- **Network Resilience**:
  - 2-second stabilization delay after WiFi connects
  - HTTP retry logic with exponential backoff (1s, 2s, 5s)
  - Handles DNS failures (error 202) after OTA reboots
- **Safety Features**:
  - NVS version checking prevents settings corruption
  - Error callbacks with user feedback
  - Graceful fallback on failure

### NVS (Non-Volatile Storage) Management
- **Versioned Settings**: GLOBAL_SETTINGS_VERSION prevents corruption
- **Stored Data**:
  - WiFi credentials (SSID, password)
  - LibreLink credentials (email, password, patient ID, auth token)
  - Global settings (update interval, thresholds, moon lamp toggle)
  - Regional API URL (redirected server)
- **Migration**: Automatic reset to defaults if version mismatch detected
- **Namespace**: `storage` for settings, `wifi_config` for credentials

## Building and Flashing

### Using ESP-IDF Extension in VS Code

1. Press `F1` and select `ESP-IDF: Set Espressif device target`
   - Choose `esp32s3`

2. Press `F1` and select `ESP-IDF: Build your project`

3. Press `F1` and select `ESP-IDF: Flash your project`

4. Press `F1` and select `ESP-IDF: Monitor your device`

### Using Command Line

```bash
# Set target
idf.py set-target esp32s3

# Build
idf.py build

# Flash and monitor
idf.py flash monitor
```

## Creating a Release for OTA Updates

### Automatic Release (Recommended)

The project includes a GitHub Actions workflow that automatically builds and releases firmware:

1. **Update the version** in [main/config.h](main/config.h):
   ```c
   #define DEVICE_VERSION "1.0.1"  // Increment version number
   ```

2. **Commit and push to master**:
   ```bash
   git add main/config.h
   git commit -m "Bump version to 1.0.1"
   git push origin master
   ```

3. **GitHub Actions will automatically**:
   - Build the firmware for ESP32-S3
   - Create a release tagged `v1.0.1`
   - Upload `glucose-monitor-v1.0.1.bin` as a release asset
   - Generate release notes with OTA instructions

4. **Devices will auto-detect** the new version on next boot!

### Manual Release

To manually publish a firmware update:

1. **Build the firmware**:
   ```bash
   idf.py build
   ```

2. **Locate the binary**: The `.bin` file will be in:
   ```
   build/glucose-s3-idf.bin
   ```

3. **Create GitHub Release**:
   - Go to https://github.com/Alundran/ESPS3-Glucose-Monitor/releases
   - Click "Draft a new release"
   - Tag version: `v1.0.1` (must match DEVICE_VERSION in config.h)
   - Release title: `Firmware v1.0.1`
   - Upload the `.bin` file as a release asset
   - Publish release

4. **Device will auto-detect**: On next boot, devices will check for the new version and prompt users to update

### Notes
- Version must match the tag (e.g., `DEVICE_VERSION "1.0.1"` → tag `v1.0.1`)
- The workflow only creates a release if the version tag doesn't already exist
- Always increment the version number for new releases

## Configuration Files

- **config.h**: Device name, version, manufacturer info, and demo mode settings
- **libre_config.h**: LibreLinkUp API endpoints and configuration
- **ir_remote_config.h**: Moon lamp IR command codes
- **sdkconfig**: ESP-IDF build configuration
- **partitions.csv**: Flash partition layout

## Dependencies

### ESP-IDF Components
- LVGL (v8.x) - Graphics library for display
- ESP LCD ILI9341 - Display driver
- ESP LCD Touch - Touchscreen support
- ESP-BOX-3 BSP - Board support package
- FreeRTOS - Real-time operating system

### Managed Components
All dependencies are managed via ESP Component Registry (idf_component.yml):
- espressif/esp-box-3
- espressif/esp_lvgl_port
- espressif/esp_lcd_ili9341
- espressif/esp_lcd_touch_gt911
- lvgl/lvgl

## Project Structure

```
main/
├── main.c                   # Main application logic and task coordination
├── display.c/h              # LVGL display management and gesture handling
├── wifi_manager.c/h         # WiFi provisioning, web server, captive portal
├── librelinkup.c/h          # LibreLinkUp API client with retry logic
├── libre_credentials.c/h    # Credential storage in NVS
├── global_settings.c/h      # Settings management with versioning
├── ir_transmitter.c/h       # IR LED control for Moon Lamp (NEC protocol)
├── ota_update.c/h           # OTA firmware update system
├── config.h                 # Device configuration and version
├── libre_config.h           # LibreLinkUp API endpoints
└── ir_remote_config.h       # Moon lamp IR command codes (NEC)

lvgl/                        # LVGL graphics library (v8.3)
managed_components/          # ESP Component Registry dependencies
├── espressif__esp-box-3/    # Board support package
├── espressif__esp_lvgl_port/# LVGL ESP32 integration
├── espressif__esp_lcd_ili9341/  # Display driver
├── espressif__esp_lcd_touch_gt911/ # Touch driver
└── lvgl__lvgl/              # LVGL core library

build/                       # Build output directory
├── glucose-s3-idf.bin       # Main firmware binary
├── bootloader/              # Bootloader binary
└── partition_table/         # Partition table binary

partitions.csv               # OTA partition table (2x 4MB app partitions)
sdkconfig                    # ESP-IDF configuration
CMakeLists.txt               # CMake build configuration
```

## Troubleshooting

### Display Issues
- **Blank Screen**: Verify ESP32-S3-BOX-3 is powered via USB-C with sufficient current
- **Touch Not Responding**: Check serial monitor for GT911 touch driver errors
- **Graphics Glitches**: Ensure LVGL task has sufficient stack space (8KB minimum)
- **Screen Not Updating**: Check that `lv_refr_now()` is being called after screen changes

### WiFi Connection Issues
- **Cannot Connect**: 
  - Use "Retry" button on connection failed screen
  - Check 2.4GHz WiFi (ESP32-S3 doesn't support 5GHz)
  - Verify router allows ESP32 devices
- **Frequent Disconnects**: 
  - Check signal strength (display shows IP in settings)
  - Router may have aggressive power saving
- **Captive Portal Not Opening**:
  - Manually navigate to 192.168.4.1
  - Try different device/browser
- **DNS Errors After OTA**:
  - Wait 2-3 seconds after reboot
  - Retry logic should handle automatically
  - Error 202 (EAI_FAIL) will retry with backoff

### LibreLink Issues
- **401 Unauthorized**: 
  - Verify email/password are correct
  - Check server region (US vs EU)
  - Token may have expired (re-login)
- **403 Forbidden**:
  - Patient hasn't granted permissions in LibreLink app
  - Check patient ID is correct
- **API Timeouts**:
  - Increase timeout in librelinkup.c (default 10s)
  - Check internet connection stability
- **No Glucose Data**:
  - View serial monitor for API error details
  - Ensure patient has active CGM readings
  - Check "Last Update" timestamp isn't stale

### Moon Lamp Not Responding
- **No IR Signal**: 
  - Verify GPIO39 is connected to IR LED
  - Check IR LED polarity (anode to GPIO via resistor)
  - Test with IR camera/phone camera (should see LED flash)
- **Wrong Colors**: 
  - Use web IR testing tool to find correct codes
  - Update ir_remote_config.h with your lamp's codes
  - Verify NEC protocol compatibility (38kHz carrier)
- **Inconsistent Control**:
  - Ensure moon lamp is in range and line-of-sight
  - Check that moon lamp is enabled in settings
  - IR only transmits on glucose state change (by design)

### OTA Update Issues
- **"ESP_ERR_NO_MEM" Error**:
  - This is now fixed with optimized buffer sizes (4096/2048)
  - If still occurs, check free heap with `esp_get_free_heap_size()`
- **Progress Screen Not Showing**:
  - Fixed with explicit screen refresh (`lv_refr_now()`)
  - 500ms delay ensures UI renders before network ops
- **Update Fails to Download**:
  - Check internet connection (not just WiFi)
  - Verify GitHub releases URL is accessible
  - Check firmware size < 4MB (partition limit)
- **Update Completes but Device Won't Boot**:
  - May need to erase flash and re-flash via USB
  - Check partition table matches (partitions.csv)

### Graph Display Issues
- **Graph Not Showing**: 
  - Swipe left from glucose screen
  - Requires valid glucose data with graphData from API
  - Check serial monitor for parsing errors
- **Graph Cut Off**: 
  - Y-axis scales to min/max of data
  - Some values may be outside visible range
- **No Historical Data**: 
  - LibreLink API returns up to 24 hours
  - New patients may have limited history

### AHS Quote Issues
- **Quotes Not Changing**: 
  - Random selection from ahs_quotes.h (51 quotes)
  - Swipe up again for different quote
- **Quote Display Errors**: 
  - Check ahs_quotes.h formatting
  - Ensure proper JSON structure

### General Debugging
- **Serial Monitor**: 115200 baud for detailed logs
- **Log Levels**: Set in sdkconfig (INFO, DEBUG, VERBOSE)
- **Heap Memory**: Monitor with `ESP_LOGI` - should have >100KB free
- **Task Stack**: Increase stack size if seeing overflow warnings
- **Watchdog Resets**: Long-running operations should use `vTaskDelay()`

## Demo Mode

For testing without LibreLink credentials, enable demo mode in config.h:
```c
#define DEMO_MODE_ENABLED 1
```
This will display simulated glucose readings without API calls.

## Serial Monitor Output

Monitor the device at 115200 baud to see:
- WiFi connection status
- LibreLink login attempts
- Glucose data fetch results
- Display state changes
- Button press events
- IR transmission confirmations

## Technical Details

### Hardware Specifications
- **Microcontroller**: ESP32-S3 (Xtensa dual-core LX7 @ 240MHz)
- **RAM**: 512KB SRAM + 2MB PSRAM (QSPI)
- **Flash**: 16MB QSPI Flash
- **Display**: 320x240 ILI9341 LCD (SPI interface)
- **Touch**: GT911 capacitive touch controller (I2C)
- **WiFi**: 802.11 b/g/n (2.4GHz only)
- **Bluetooth**: BLE 5.0 (not used in this project)

### Software Stack
- **Framework**: ESP-IDF v5.5.1
- **Graphics**: LVGL v8.3 with ESP LVGL Port
- **RTOS**: FreeRTOS (dual-core task scheduling)
- **HTTP Client**: ESP HTTP Client with TLS 1.2
- **Certificates**: ESP x509 Certificate Bundle for HTTPS

### IR Transmission
- **Protocol**: NEC (standard IR remote protocol)
- **Carrier Frequency**: 38kHz
- **Peripheral**: ESP32 RMT (Remote Control Transceiver)
- **GPIO**: 39 (configurable in ir_transmitter.c)
- **Commands**: Defined in ir_remote_config.h

### Network & API
- **LibreLinkUp API**: RESTful JSON API with JWT authentication
- **Authentication**: Bearer token (valid ~6 months)
- **Account-Id**: SHA256 hash of user ID
- **Servers**: api.libreview.io (US), api-eu.libreview.io (EU), api-eu2.libreview.io (EU2)
- **Regional Redirect**: Auto-detects correct server on login
- **HTTP Retry**: Exponential backoff (1s, 2s, 5s) for DNS/connection failures
- **Timeout**: 10s for API calls, 60s for OTA downloads

### Storage & Memory
- **NVS Partitions**: 
  - `wifi_config` namespace for WiFi credentials
  - `storage` namespace for LibreLink + settings
- **Settings Version**: GLOBAL_SETTINGS_VERSION = 2
- **OTA Partitions**: 2x 4MB app partitions (factory + ota_0)
- **Heap Management**: 
  - ~200KB used by LVGL + WiFi + HTTPS
  - OTA buffers: 4096 RX / 2048 TX
  - Graph data: Up to 144 points cached

### Task Architecture
- **Main Task**: Initialization and setup flow
- **Display Task**: LVGL timer handler (10ms tick)
- **Glucose Fetch Task**: Periodic API calls (configurable interval)
- **OTA Check Task**: Background update checking
- **HTTP Server Task**: Web interface and captive portal
- **WiFi Task**: Connection management and callbacks

### Update Frequency & Intervals
- **Glucose Fetch**: Configurable 1-60 minutes (default 5)
- **Display Refresh**: LVGL automatic (on content change)
- **OTA Check**: Once on boot, manual via web UI
- **Graph Updates**: Every glucose fetch (if graphData available)
- **Moon Lamp**: Only on glucose state change (prevents spam)

### Power Consumption
- **Active Mode**: ~500mA @ 5V (display on, WiFi connected)
- **Sleep Mode**: Not implemented (always-on display)
- **IR Transmission**: Brief spike to ~100mA additional during transmit

## Credits

**Built by**: Spalding  
**For**: Stephen "Supreme" Higgins  
**Purpose**: Personal glucose monitoring device with ambient lighting integration  
**Theme**: 🎭 American Horror Story - Because monitoring glucose levels should be dramatically entertaining!

### Special Features
- 51 handpicked quotes from various AHS seasons
- Custom Supreme Glucose splash screen
- Hidden surprise screens for triple-tap enthusiasts
- Color-coded status alerts (because subtlety is overrated)

## License

This is a personal project for Stephen Higgins. Not licensed for commercial use.

---

*"You are the sum of all your choices." - American Horror Story*
