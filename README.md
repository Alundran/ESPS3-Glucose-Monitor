# ESP32-S3-BOX-3 Glucose Monitor

A real-time continuous glucose monitoring display device that connects to LibreLinkUp to display glucose readings on the ESP32-S3-BOX-3 touchscreen. Built by Spalding for Stephen "Supreme" Higgins.

## Hardware Setup

- **Device**: ESP32-S3-BOX-3
- **Display**: 320x240 ILI9341 LCD with LVGL interface
- **IR Transmitter Pin**: GPIO39 (for Moon Lamp control)
- **WiFi**: Built-in ESP32-S3 WiFi for LibreLinkUp connectivity

## Features

- **Real-time Glucose Monitoring**: Fetches and displays current glucose readings from LibreLinkUp API
- **Trend Indicators**: Shows glucose trend arrows (rising, falling, steady)
- **High/Low Alerts**: Visual indicators for out-of-range glucose values
- **WiFi Provisioning**: Easy web-based WiFi setup via captive portal
- **LibreLink Integration**: Supports both US and EU LibreLinkUp servers
- **IR Moon Lamp Control**: Automatically changes moon lamp color based on glucose levels (red for high/low, green for normal)
- **Settings Management**: Configure update intervals and reset device via red button
- **OTA Firmware Updates**: Automatic check for updates on boot and manual check via web interface
- **Demo Mode**: Optional demo mode for testing without LibreLink credentials
- **Beautiful UI**: Custom splash screen and intuitive glucose display interface


## How It Works

1. **Initial Setup**: On first boot, device displays splash screen and shows about screen with "Next" button
2. **WiFi Configuration**: Device creates a WiFi access point for easy web-based WiFi setup
3. **LibreLink Credentials**: After WiFi is connected, navigate to the device's IP address to configure LibreLinkUp email, password, and server region
4. **Glucose Display**: Device automatically fetches and displays glucose readings at configured intervals (default: 5 minutes)
5. **Visual Feedback**: Display shows current glucose value in mmol/L, trend arrow, and highlights for high/low readings
6. **Moon Lamp Integration**: IR transmitter automatically adjusts moon lamp color based on glucose state

## Configuration

### WiFi Setup
- Device creates AP named "GlucoseMonitor" for initial setup
- Connect to the AP and follow web portal instructions
- Enter your WiFi SSID and password

### LibreLink Credentials
- Navigate to `http://<device-ip>` after WiFi connection
- Enter LibreLinkUp email and password
- Select server region (US/EU)
- Optionally enter patient ID (will be auto-detected if not provided)

### Update Interval
- Default: 5 minutes
- Adjustable via web interface
- Range: 1-60 minutes

### Red Button Functions
- **Single Press**: Toggle between glucose display and settings screen
- **Settings Screen**: Shows WiFi info, LibreLink status, and reset option
- **Reset**: Clears all credentials and restarts device for fresh setup

### Firmware Updates (OTA)
- **Automatic Check**: Device checks for updates on boot (5 seconds after WiFi connects)
- **Manual Check**: Navigate to Settings page in web interface and click "Check for Updates"
- **Update Process**: 
  1. Device checks GitHub releases at `https://github.com/Alundran/ESPS3-Glucose-Monitor`
  2. Compares current version with latest release tag
  3. Downloads `.bin` file from release assets if newer version available
  4. Shows progress bar on display during download/install
  5. **WARNING**: Do NOT disconnect power during update!
  6. Device automatically reboots when update completes
- **Safety**: Only updates when WiFi is stable and power connected

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
├── main.c                   # Main application logic
├── display.c/h              # LVGL display management
├── wifi_manager.c/h         # WiFi provisioning and connection
├── librelinkup.c/h          # LibreLinkUp API client
├── libre_credentials.c/h    # Credential storage (NVS)
├── global_settings.c/h      # Settings management
├── ir_transmitter.c/h       # IR LED control for Moon Lamp
├── ota_update.c/h           # OTA firmware update system
└── config.h                 # Device configuration

lvgl/                        # LVGL graphics library
managed_components/          # ESP Component Registry dependencies
build/                       # Build output directory
partitions.csv               # OTA partition table (2x 1.5MB app partitions)
```

## Troubleshooting

### Display Issues
- Verify ESP32-S3-BOX-3 is powered via USB-C
- Check serial monitor for display initialization errors
- Ensure LVGL task has sufficient stack space (8KB+)

### WiFi Connection Issues
- Use "Retry" button on connection failed screen
- Use "Restart Setup" to clear WiFi credentials and start fresh
- Check WiFi signal strength and router compatibility (2.4GHz only)

### LibreLink Issues
- Verify credentials are correct (email and password)
- Ensure correct server region is selected (US vs EU)
- Check that patient has given permissions in LibreLink app
- View logs via serial monitor for API error messages

### Moon Lamp Not Responding
- Verify GPIO39 is connected to IR LED
- Check that moon lamp is in range (line of sight to IR LED)
- Test manually using web interface or button press
- Verify NEC protocol codes match your moon lamp

### No Glucose Data Displayed
- Ensure WiFi is connected (check settings screen)
- Verify LibreLink credentials are configured
- Check update interval in settings
- Monitor serial output for API errors (401 = auth issue, 403 = permissions)

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

- **Microcontroller**: ESP32-S3 (Xtensa dual-core 240MHz)
- **Display Resolution**: 320x240 pixels
- **Display Interface**: SPI (ILI9341)
- **WiFi**: 802.11 b/g/n (2.4GHz)
- **IR Protocol**: NEC (38kHz carrier)
- **IR Transmitter**: RMT peripheral on GPIO39
- **Storage**: NVS (Non-Volatile Storage) for credentials and settings
- **Update Frequency**: Configurable (1-60 minutes)
- **LibreLinkUp API**: RESTful JSON API with JWT authentication

## Credits

**Built by**: Spalding  
**For**: Stephen "Supreme" Higgins  
**Purpose**: Personal glucose monitoring device with ambient lighting integration

## License

This is a personal project for Stephen Higgins. Not licensed for commercial use.
