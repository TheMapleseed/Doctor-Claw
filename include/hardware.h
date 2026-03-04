#ifndef DOCTORCLAW_HARDWARE_H
#define DOCTORCLAW_HARDWARE_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_DEVICE_NAME 128
#define MAX_DEVICE_PATH 256

typedef enum {
    DEVICE_TYPE_USB,
    DEVICE_TYPE_SERIAL,
    DEVICE_TYPE_BLUETOOTH,
    DEVICE_TYPE_NETWORK,
    DEVICE_TYPE_UNKNOWN
} device_type_t;

typedef struct {
    char name[MAX_DEVICE_NAME];
    char path[MAX_DEVICE_PATH];
    device_type_t type;
    uint16_t vendor_id;
    uint16_t product_id;
    bool connected;
} hardware_device_t;

typedef struct {
    hardware_device_t devices[32];
    size_t device_count;
} hardware_manager_t;

int hardware_init(hardware_manager_t *mgr);
int hardware_scan(hardware_manager_t *mgr);
int hardware_scan_usb(hardware_manager_t *mgr);
int hardware_scan_serial(hardware_manager_t *mgr);
int hardware_list(hardware_manager_t *mgr, hardware_device_t **out_devices, size_t *out_count);
int hardware_find(hardware_manager_t *mgr, const char *name, hardware_device_t *out_device);
void hardware_free(hardware_manager_t *mgr);

#endif
