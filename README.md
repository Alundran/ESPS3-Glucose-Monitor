# ESP32-S3-BOX-3 IR Receiver Example

This project demonstrates how to use the IR receiver on GPIO38 of the ESP32-S3-BOX-3 to capture and decode infrared remote control signals.

## Hardware Setup

- **Device**: ESP32-S3-BOX-3
- **IR Receiver Pin**: GPIO38
- **Protocol Supported**: NEC (most common IR remote protocol)

The ESP32-S3-BOX-3 has a built-in IR receiver connected to GPIO38.

## Features

- Captures IR signals using the ESP32 RMT (Remote Control) peripheral
- Decodes NEC protocol IR commands
- Displays received address and command codes
- Detects repeat codes (when button is held)
- Outputs raw timing data for unsupported protocols

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

## Usage

1. Flash the firmware to your ESP32-S3-BOX-3
2. Open the serial monitor (115200 baud)
3. Point any IR remote control at the device
4. Press buttons on the remote
5. Observe the decoded IR commands in the console

## Output Example

When you press a button on your IR remote, you'll see output like:

```
I (12345) IR_RECEIVER: Received 34 RMT symbols
I (12346) IR_RECEIVER: ╔═══════════════════════════════════════╗
I (12347) IR_RECEIVER: ║     IR COMMAND RECEIVED               ║
I (12348) IR_RECEIVER: ╠═══════════════════════════════════════╣
I (12349) IR_RECEIVER: ║ Address: 0x00FF                      ║
I (12350) IR_RECEIVER: ║ Command: 0x45                        ║
I (12351) IR_RECEIVER: ╚═══════════════════════════════════════╝

>>> IR Remote Button Pressed: Address=0x00FF, Command=0x45 <<<
```

## Supported Protocols

Currently, this example supports the **NEC protocol**, which is used by most consumer IR remotes including:
- TV remotes
- Air conditioner remotes
- Generic IR remotes

If you need to support other protocols (RC5, RC6, Sony SIRC, etc.), you can extend the decoding logic in the `nec_parse_frame()` function.

## Troubleshooting

- **No output when pressing remote buttons**: 
  - Verify the IR remote is working (use phone camera to check if IR LED lights up)
  - Ensure you're pointing the remote at the ESP32-S3-BOX-3
  - Check that GPIO38 is correctly configured

- **"Could not decode IR frame" messages**: 
  - Your remote may use a different protocol
  - Check the raw timing data printed in the console
  - You may need to implement a different protocol decoder

## Technical Details

- Uses ESP32's RMT (Remote Control) peripheral for precise timing capture
- 1MHz resolution (1μs per tick)
- Configurable timing tolerances for reliable decoding
- Non-blocking design using FreeRTOS tasks and queues
