#ifndef DOCTORCLAW_PERIPHERALS_H
#define DOCTORCLAW_PERIPHERALS_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_BOARD_NAME 64
#define MAX_DEVICE_PATH 256
#define MAX_GPIO_PINS 32

typedef enum {
    PERIPHERAL_NUCLEO_F401RE,
    PERIPHERAL_RPI_GPIO,
    PERIPHERAL_ARDUINO,
    PERIPHERAL_ESP32,
    PERIPHERAL_UNKNOWN
} peripheral_type_t;

typedef enum {
    PIN_MODE_INPUT,
    PIN_MODE_OUTPUT,
    PIN_MODE_PWM
} pin_mode_t;

typedef struct {
    uint8_t pin;
    pin_mode_t mode;
    bool value;
} gpio_pin_t;

typedef struct {
    char name[MAX_BOARD_NAME];
    peripheral_type_t type;
    char device_path[MAX_DEVICE_PATH];
    gpio_pin_t pins[MAX_GPIO_PINS];
    size_t pin_count;
    bool connected;
} peripheral_t;

int peripheral_init(peripheral_t *peri, peripheral_type_t type, const char *device_path);
int peripheral_connect(peripheral_t *peri);
int peripheral_disconnect(peripheral_t *peri);
int peripheral_gpio_set_mode(peripheral_t *peri, uint8_t pin, pin_mode_t mode);
int peripheral_gpio_write(peripheral_t *peri, uint8_t pin, bool value);
int peripheral_gpio_read(peripheral_t *peri, uint8_t pin, bool *out_value);
int peripheral_flash_firmware(peripheral_t *peri, const char *firmware_path);
void peripheral_free(peripheral_t *peri);

int peripheral_list_configured(peripheral_t **out_peripherals, size_t *out_count);
int peripheral_hardware_discover(void);

int arduino_upload(const char *port, const char *fqbn, const char *sketch_path);
int arduino_compile(const char *fqbn, const char *sketch_path, const char *output_path);
int nucleo_flash(const char *port, const char *firmware_path);
int uno_q_setup(const char *port);
int uno_q_bridge(const char *port);
int capabilities_discover(void);

#endif
