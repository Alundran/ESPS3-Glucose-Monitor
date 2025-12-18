/**
 * IR Remote Configuration
 * 
 * This file contains the IR remote control button mappings
 * for the NEC protocol remote.
 */

#ifndef IR_REMOTE_CONFIG_H
#define IR_REMOTE_CONFIG_H

#include <stdint.h>

// Remote address
#define IR_REMOTE_ADDRESS           0xFF00

// Command codes
#define IR_CMD_ON                   0x40
#define IR_CMD_OFF                  0x5C
#define IR_CMD_RED                  0x58
#define IR_CMD_GREEN                0x59
#define IR_CMD_WHITE                0x44
#define IR_CMD_SMOOTH               0x17
#define IR_CMD_INCREASE_BRIGHTNESS  0x5D
#define IR_CMD_DECREASE_BRIGHTNESS  0x41

/**
 * Get button name from command code
 * Returns a human-readable string for the command
 */
static inline const char* ir_get_command_name(uint8_t command) {
    switch (command) {
        case IR_CMD_ON:                     return "ON";
        case IR_CMD_OFF:                    return "OFF";
        case IR_CMD_RED:                    return "RED";
        case IR_CMD_GREEN:                  return "GREEN";
        case IR_CMD_WHITE:                  return "WHITE";
        case IR_CMD_SMOOTH:                 return "SMOOTH";
        case IR_CMD_INCREASE_BRIGHTNESS:    return "INCREASE_BRIGHTNESS";
        case IR_CMD_DECREASE_BRIGHTNESS:    return "DECREASE_BRIGHTNESS";
        default:                            return "UNKNOWN";
    }
}

/**
 * Validate if address matches expected remote
 */
static inline bool ir_is_valid_address(uint16_t address) {
    return (address == IR_REMOTE_ADDRESS);
}

#endif // IR_REMOTE_CONFIG_H
