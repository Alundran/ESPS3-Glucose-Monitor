/**
 * IR Transmitter Module
 * 
 * Sends NEC protocol IR commands to control Moon Lamp
 * Uses GPIO39 for IR LED transmission
 */

#include "ir_transmitter.h"
#include "ir_remote_config.h"
#include "global_settings.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "IR_TX";

// IR Transmitter GPIO
#define IR_TX_GPIO              GPIO_NUM_39
#define IR_CTRL_GPIO            GPIO_NUM_44  // Power control for IR transmitter

// NEC Protocol Timing (in microseconds)
#define NEC_LEADING_CODE_HIGH   9000
#define NEC_LEADING_CODE_LOW    4500
#define NEC_PAYLOAD_ONE_HIGH    560
#define NEC_PAYLOAD_ONE_LOW     1690
#define NEC_PAYLOAD_ZERO_HIGH   560
#define NEC_PAYLOAD_ZERO_LOW    560
#define NEC_REPEAT_CODE_HIGH    9000
#define NEC_REPEAT_CODE_LOW     2250

// RMT carrier configuration for 38kHz IR carrier
#define IR_CARRIER_FREQ_HZ      38000
#define IR_CARRIER_DUTY_CYCLE   0.33  // 33% duty cycle

// RMT handles
static rmt_channel_handle_t tx_channel = NULL;
static rmt_encoder_handle_t nec_encoder = NULL;

// NEC IR encoder structure
typedef struct {
    rmt_encoder_t base;
    rmt_encoder_t *copy_encoder;
    rmt_encoder_t *bytes_encoder;
    rmt_symbol_word_t nec_leading_symbol;
    rmt_symbol_word_t nec_ending_symbol;
    int state;
} rmt_nec_encoder_t;

// NEC encoder states
enum {
    NEC_STATE_IDLE,
    NEC_STATE_LEADING,
    NEC_STATE_DATA,
    NEC_STATE_ENDING
};

// NEC frame structure
typedef struct {
    uint16_t address;
    uint8_t command;
} nec_frame_t;

// Encode function for NEC protocol
static size_t nec_encode(rmt_encoder_t *encoder, rmt_channel_handle_t channel,
                         const void *primary_data, size_t data_size,
                         rmt_encode_state_t *ret_state)
{
    rmt_nec_encoder_t *nec_encoder = __containerof(encoder, rmt_nec_encoder_t, base);
    rmt_encode_state_t session_state = RMT_ENCODING_RESET;
    size_t encoded_symbols = 0;
    nec_frame_t *frame = (nec_frame_t *)primary_data;

    switch (nec_encoder->state) {
    case NEC_STATE_IDLE:
        nec_encoder->state = NEC_STATE_LEADING;
        // Fall through
    case NEC_STATE_LEADING:
        encoded_symbols += nec_encoder->copy_encoder->encode(nec_encoder->copy_encoder, channel,
                                                             &nec_encoder->nec_leading_symbol, sizeof(rmt_symbol_word_t),
                                                             &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = NEC_STATE_DATA;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            goto out;
        }
        // Fall through
    case NEC_STATE_DATA:
        // Encode address (16 bits) and command (8 bits) + inverted command (8 bits)
        uint8_t data_to_encode[4];
        data_to_encode[0] = frame->address & 0xFF;         // Address low byte
        data_to_encode[1] = (frame->address >> 8) & 0xFF;  // Address high byte
        data_to_encode[2] = frame->command;                 // Command
        data_to_encode[3] = ~frame->command;                // Inverted command
        
        encoded_symbols += nec_encoder->bytes_encoder->encode(nec_encoder->bytes_encoder, channel,
                                                               data_to_encode, sizeof(data_to_encode),
                                                               &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = NEC_STATE_ENDING;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            goto out;
        }
        // Fall through
    case NEC_STATE_ENDING:
        encoded_symbols += nec_encoder->copy_encoder->encode(nec_encoder->copy_encoder, channel,
                                                             &nec_encoder->nec_ending_symbol, sizeof(rmt_symbol_word_t),
                                                             &session_state);
        if (session_state & RMT_ENCODING_COMPLETE) {
            nec_encoder->state = NEC_STATE_IDLE;
        }
        if (session_state & RMT_ENCODING_MEM_FULL) {
            goto out;
        }
    }
out:
    *ret_state = session_state;
    return encoded_symbols;
}

// Reset encoder state
static esp_err_t nec_encoder_reset(rmt_encoder_t *encoder)
{
    rmt_nec_encoder_t *nec_encoder = __containerof(encoder, rmt_nec_encoder_t, base);
    rmt_encoder_reset(nec_encoder->copy_encoder);
    rmt_encoder_reset(nec_encoder->bytes_encoder);
    nec_encoder->state = NEC_STATE_IDLE;
    return ESP_OK;
}

// Delete encoder
static esp_err_t nec_encoder_del(rmt_encoder_t *encoder)
{
    rmt_nec_encoder_t *nec_encoder = __containerof(encoder, rmt_nec_encoder_t, base);
    rmt_del_encoder(nec_encoder->copy_encoder);
    rmt_del_encoder(nec_encoder->bytes_encoder);
    free(nec_encoder);
    return ESP_OK;
}

// Create NEC encoder
static esp_err_t nec_encoder_create(rmt_encoder_handle_t *ret_encoder)
{
    esp_err_t ret = ESP_OK;
    rmt_nec_encoder_t *nec_encoder = calloc(1, sizeof(rmt_nec_encoder_t));
    if (!nec_encoder) {
        return ESP_ERR_NO_MEM;
    }
    
    nec_encoder->base.encode = nec_encode;
    nec_encoder->base.del = nec_encoder_del;
    nec_encoder->base.reset = nec_encoder_reset;
    
    // Create copy encoder for leading/ending codes
    rmt_copy_encoder_config_t copy_encoder_config = {};
    ret = rmt_new_copy_encoder(&copy_encoder_config, &nec_encoder->copy_encoder);
    if (ret != ESP_OK) {
        goto err;
    }
    
    // Create bytes encoder for data
    rmt_bytes_encoder_config_t bytes_encoder_config = {
        .bit0 = {
            .level0 = 1,
            .duration0 = NEC_PAYLOAD_ZERO_HIGH,
            .level1 = 0,
            .duration1 = NEC_PAYLOAD_ZERO_LOW,
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = NEC_PAYLOAD_ONE_HIGH,
            .level1 = 0,
            .duration1 = NEC_PAYLOAD_ONE_LOW,
        },
        .flags.msb_first = 0  // LSB first for NEC
    };
    ret = rmt_new_bytes_encoder(&bytes_encoder_config, &nec_encoder->bytes_encoder);
    if (ret != ESP_OK) {
        goto err;
    }
    
    // Leading code: 9ms high + 4.5ms low
    nec_encoder->nec_leading_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = NEC_LEADING_CODE_HIGH,
        .level1 = 0,
        .duration1 = NEC_LEADING_CODE_LOW,
    };
    
    // Ending code: 560us high
    nec_encoder->nec_ending_symbol = (rmt_symbol_word_t) {
        .level0 = 1,
        .duration0 = NEC_PAYLOAD_ZERO_HIGH,
        .level1 = 0,
        .duration1 = 0x7FFF,  // Large duration for ending
    };
    
    *ret_encoder = &nec_encoder->base;
    return ESP_OK;

err:
    if (nec_encoder) {
        if (nec_encoder->copy_encoder) {
            rmt_del_encoder(nec_encoder->copy_encoder);
        }
        if (nec_encoder->bytes_encoder) {
            rmt_del_encoder(nec_encoder->bytes_encoder);
        }
        free(nec_encoder);
    }
    return ret;
}

esp_err_t ir_transmitter_init(void)
{
    ESP_LOGI(TAG, "Initializing IR transmitter on GPIO%d", IR_TX_GPIO);
    
    // Configure IR control GPIO (GPIO44) - enables power to IR transmitter
    gpio_config_t ctrl_conf = {
        .pin_bit_mask = (1ULL << IR_CTRL_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&ctrl_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure IR control GPIO%d: %s", IR_CTRL_GPIO, esp_err_to_name(ret));
        return ret;
    }
    
    // Set GPIO44 to LOW to enable IR transmitter power
    gpio_set_level(IR_CTRL_GPIO, 0);
    ESP_LOGI(TAG, "GPIO%d set to LOW (IR transmitter powered ON)", IR_CTRL_GPIO);
    
    // Configure GPIO for IR TX output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << IR_TX_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO%d: %s", IR_TX_GPIO, esp_err_to_name(ret));
        return ret;
    }
    
    // Set GPIO to low initially
    gpio_set_level(IR_TX_GPIO, 0);
    
    ESP_LOGI(TAG, "GPIO%d configured for IR transmission", IR_TX_GPIO);
    
    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_channel_config = {
        .gpio_num = IR_TX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1MHz resolution = 1us per tick
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
        .flags.with_dma = false,
    };
    
    ret = rmt_new_tx_channel(&tx_channel_config, &tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "RMT TX channel created successfully");
    
    // Configure 38kHz carrier
    rmt_carrier_config_t carrier_config = {
        .frequency_hz = IR_CARRIER_FREQ_HZ,
        .duty_cycle = IR_CARRIER_DUTY_CYCLE,
        .flags.polarity_active_low = false,
    };
    
    ret = rmt_apply_carrier(tx_channel, &carrier_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply carrier: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "38kHz carrier configured");
    
    // Create NEC encoder
    ret = nec_encoder_create(&nec_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create NEC encoder: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "NEC encoder created successfully");
    
    // Enable RMT TX channel
    ret = rmt_enable(tx_channel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "IR transmitter initialized successfully");
    return ESP_OK;
}

esp_err_t ir_transmitter_send_command(uint16_t address, uint8_t command)
{
    if (!tx_channel || !nec_encoder) {
        ESP_LOGE(TAG, "IR transmitter not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    nec_frame_t frame = {
        .address = address,
        .command = command,
    };
    
    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,  // No loop
    };
    
    ESP_LOGI(TAG, "Sending IR command - Address: 0x%04X, Command: 0x%02X (%s)", 
             address, command, ir_get_command_name(command));
    
    esp_err_t ret = rmt_transmit(tx_channel, nec_encoder, &frame, sizeof(frame), &transmit_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to transmit IR command: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Wait for transmission to complete
    ret = rmt_tx_wait_all_done(tx_channel, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to wait for transmission: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "IR command sent successfully");
    return ESP_OK;
}

esp_err_t ir_transmitter_set_moon_lamp_color(bool is_low, bool is_high, bool is_normal)
{
    if (!global_settings_is_moon_lamp_enabled()) {
        ESP_LOGD(TAG, "Moon Lamp control is disabled");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Setting Moon Lamp color - Low: %d, High: %d, Normal: %d", is_low, is_high, is_normal);
    
    // Always send ON command first
    esp_err_t ret = ir_transmitter_send_command(IR_REMOTE_ADDRESS, IR_CMD_ON);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send ON command");
        return ret;
    }
    
    // Wait a bit between commands
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Determine color based on glucose state
    uint8_t color_cmd;
    if (is_low || is_high) {
        // Low or high glucose -> RED
        color_cmd = IR_CMD_RED;
        ESP_LOGI(TAG, "Setting Moon Lamp to RED (glucose alert)");
    } else if (is_normal) {
        // Normal glucose -> GREEN
        color_cmd = IR_CMD_GREEN;
        ESP_LOGI(TAG, "Setting Moon Lamp to GREEN (normal glucose)");
    } else {
        // Unknown state -> WHITE
        color_cmd = IR_CMD_WHITE;
        ESP_LOGI(TAG, "Setting Moon Lamp to WHITE (unknown state)");
    }
    
    // Send color command
    ret = ir_transmitter_send_command(IR_REMOTE_ADDRESS, color_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send color command");
        return ret;
    }
    
    ESP_LOGI(TAG, "Moon Lamp color set successfully");
    return ESP_OK;
}
