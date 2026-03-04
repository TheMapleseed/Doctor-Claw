#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

int hardware_init(hardware_manager_t *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(hardware_manager_t));
    return 0;
}

static void hardware_add_device(hardware_manager_t *mgr, const char *name, const char *path, device_type_t type) {
    if (mgr->device_count >= 32) return;
    hardware_device_t *dev = &mgr->devices[mgr->device_count];
    snprintf(dev->name, sizeof(dev->name), "%s", name);
    snprintf(dev->path, sizeof(dev->path), "%s", path);
    dev->type = type;
    dev->connected = true;
    mgr->device_count++;
}

int hardware_scan_usb(hardware_manager_t *mgr) {
    if (!mgr) return -1;
    DIR *dir = opendir("/dev");
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "usb", 3) == 0) {
            char path[512];
            snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
            hardware_add_device(mgr, entry->d_name, path, DEVICE_TYPE_USB);
        }
    }
    closedir(dir);
    return 0;
}

int hardware_scan_serial(hardware_manager_t *mgr) {
    if (!mgr) return -1;
    DIR *dir = opendir("/dev");
    if (!dir) return -1;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "cu.", 3) == 0) {
            char path[512];
            snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
            hardware_add_device(mgr, entry->d_name, path, DEVICE_TYPE_SERIAL);
        }
    }
    closedir(dir);
    return 0;
}

int hardware_scan(hardware_manager_t *mgr) {
    if (!mgr) return -1;
    mgr->device_count = 0;
    hardware_scan_usb(mgr);
    hardware_scan_serial(mgr);
    return 0;
}

int hardware_list(hardware_manager_t *mgr, hardware_device_t **out_devices, size_t *out_count) {
    if (!mgr || !out_devices || !out_count) return -1;
    *out_devices = mgr->devices;
    *out_count = mgr->device_count;
    return 0;
}

int hardware_find(hardware_manager_t *mgr, const char *name, hardware_device_t *out_device) {
    if (!mgr || !name || !out_device) return -1;
    for (size_t i = 0; i < mgr->device_count; i++) {
        if (strcmp(mgr->devices[i].name, name) == 0) {
            *out_device = mgr->devices[i];
            return 0;
        }
    }
    return -1;
}

void hardware_free(hardware_manager_t *mgr) {
    if (mgr) {
        memset(mgr, 0, sizeof(hardware_manager_t));
    }
}
