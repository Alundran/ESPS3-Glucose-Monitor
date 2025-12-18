/**
 * IR Transmitter Module
 * 
 * Sends NEC protocol IR commands to control Moon Lamp
 * Uses GPIO39 for IR LED transmission
 */

#ifndef IR_TRANSMITTER_H
#define IR_TRANSMITTER_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize the IR transmitter
 * Sets up GPIO39 and RMT peripheral for IR transmission
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ir_transmitter_init(void);

/**
 * Send an IR command using NEC protocol
 * 
 * @param address 16-bit address code (e.g., 0xFF00)
 * @param command 8-bit command code (e.g., IR_CMD_RED)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ir_transmitter_send_command(uint16_t address, uint8_t command);

/**
 * Send Moon Lamp color based on glucose state
 * Automatically sends ON command followed by color command
 * 
 * @param is_low True if glucose is low (will send RED)
 * @param is_high True if glucose is high (will send RED)
 * @param is_normal True if glucose is normal (will send GREEN)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ir_transmitter_set_moon_lamp_color(bool is_low, bool is_high, bool is_normal);

#endif // IR_TRANSMITTER_H
