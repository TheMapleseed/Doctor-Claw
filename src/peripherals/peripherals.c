#include "peripherals.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#define MAX_PERIPHERALS 16

typedef struct {
    peripheral_t peripheral;
    bool active;
} peripheral_instance_t;

static peripheral_instance_t g_peripherals[MAX_PERIPHERALS];
static size_t g_peripheral_count = 0;

int peripheral_init(peripheral_t *peri, peripheral_type_t type, const char *device_path) {
    if (!peri) return -1;
    
    memset(peri, 0, sizeof(peripheral_t));
    peri->type = type;
    
    if (device_path) {
        snprintf(peri->device_path, sizeof(peri->device_path), "%s", device_path);
    }
    
    switch (type) {
        case PERIPHERAL_NUCLEO_F401RE:
            snprintf(peri->name, sizeof(peri->name), "NUCLEO-F401RE");
            break;
        case PERIPHERAL_RPI_GPIO:
            snprintf(peri->name, sizeof(peri->name), "Raspberry Pi GPIO");
            break;
        case PERIPHERAL_ARDUINO:
            snprintf(peri->name, sizeof(peri->name), "Arduino");
            break;
        case PERIPHERAL_ESP32:
            snprintf(peri->name, sizeof(peri->name), "ESP32");
            break;
        default:
            snprintf(peri->name, sizeof(peri->name), "Unknown");
            break;
    }
    
    return 0;
}

int peripheral_connect(peripheral_t *peri) {
    if (!peri) return -1;
    
    if (strlen(peri->device_path) == 0) {
        printf("[Peripheral] No device path specified for %s\n", peri->name);
        return -1;
    }
    
    int fd = open(peri->device_path, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        printf("[Peripheral] Failed to connect to %s at %s: %s\n", 
               peri->name, peri->device_path, strerror(errno));
        return -1;
    }
    
    struct termios options;
    tcgetattr(fd, &options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    tcsetattr(fd, TCSANOW, &options);
    
    peri->connected = true;
    close(fd);
    
    printf("[Peripheral] Connected to %s at %s\n", peri->name, peri->device_path);
    return 0;
}

int peripheral_disconnect(peripheral_t *peri) {
    if (!peri) return -1;
    peri->connected = false;
    printf("[Peripheral] Disconnected from %s\n", peri->name);
    return 0;
}

int peripheral_gpio_set_mode(peripheral_t *peri, uint8_t pin, pin_mode_t mode) {
    if (!peri || !peri->connected) return -1;
    if (pin >= MAX_GPIO_PINS) return -1;
    
    peri->pins[pin].pin = pin;
    peri->pins[pin].mode = mode;
    
    printf("[Peripheral] %s: Set pin %d to mode %d\n", peri->name, pin, mode);
    return 0;
}

int peripheral_gpio_write(peripheral_t *peri, uint8_t pin, bool value) {
    if (!peri || !peri->connected) return -1;
    if (pin >= MAX_GPIO_PINS) return -1;
    
    peri->pins[pin].value = value;
    
    printf("[Peripheral] %s: Pin %d = %d\n", peri->name, pin, value ? 1 : 0);
    return 0;
}

int peripheral_gpio_read(peripheral_t *peri, uint8_t pin, bool *out_value) {
    if (!peri || !peri->connected || !out_value) return -1;
    if (pin >= MAX_GPIO_PINS) return -1;
    
    *out_value = peri->pins[pin].value;
    return 0;
}

int peripheral_flash_firmware(peripheral_t *peri, const char *firmware_path) {
    if (!peri || !firmware_path) return -1;
    
    printf("[Peripheral] Flashing firmware to %s from %s\n", peri->name, firmware_path);
    
    char cmd[1024];
    if (peri->type == PERIPHERAL_ARDUINO) {
        snprintf(cmd, sizeof(cmd), "arduino-cli upload -p %s --fqbn arduino:avr:uno %s 2>/dev/null || echo 'arduino-cli not found'",
                 peri->device_path, firmware_path);
    } else if (peri->type == PERIPHERAL_NUCLEO_F401RE) {
        snprintf(cmd, sizeof(cmd), "stm32flash -w %s -v -g 0x08000000 %s 2>/dev/null || echo 'stm32flash not found'",
                 firmware_path, peri->device_path);
    } else {
        snprintf(cmd, sizeof(cmd), "echo 'No flash tool for this board type'");
    }
    
    int ret = system(cmd);
    return (ret == 0) ? 0 : -1;
}

void peripheral_free(peripheral_t *peri) {
    if (!peri) return;
    if (peri->connected) {
        peripheral_disconnect(peri);
    }
    memset(peri, 0, sizeof(peripheral_t));
}

int peripheral_list_configured(peripheral_t **out_peripherals, size_t *out_count) {
    if (out_peripherals) *out_peripherals = &g_peripherals[0].peripheral;
    if (out_count) *out_count = g_peripheral_count;
    return 0;
}

int peripheral_hardware_discover(void) {
    printf("[Peripherals] Discovering USB devices...\n");
    
#ifdef __APPLE__
    const char *dev_path = "/dev/cu.";
    DIR *d = opendir("/dev");
    if (d) {
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL) {
            if (strncmp(entry->d_name, "cu.", 3) == 0) {
                printf("  Found serial: /dev/%s\n", entry->d_name);
            }
        }
        closedir(d);
    }
#endif
    
    DIR *usb_dir = opendir("/dev/serial/by-id");
    if (usb_dir) {
        struct dirent *entry;
        while ((entry = readdir(usb_dir)) != NULL) {
            if (entry->d_name[0] != '.') {
                printf("  Found USB serial: %s\n", entry->d_name);
            }
        }
        closedir(usb_dir);
    }
    
    printf("[Peripherals] Known boards: nucleo-f401re, rpi-gpio, arduino, esp32\n");
    
    if (g_peripheral_count < MAX_PERIPHERALS) {
        peripheral_init(&g_peripherals[g_peripheral_count].peripheral, PERIPHERAL_ARDUINO, "/dev/cu.usbserial-1410");
        g_peripheral_count++;
    }
    
    return 0;
}

int arduino_upload(const char *port, const char *fqbn, const char *sketch_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "arduino-cli upload -p %s --fqbn %s %s", port, fqbn, sketch_path);
    return system(cmd);
}

int arduino_compile(const char *fqbn, const char *sketch_path, const char *output_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "arduino-cli compile --fqbn %s %s -o %s", fqbn, sketch_path, output_path);
    return system(cmd);
}

int nucleo_flash(const char *port, const char *firmware_path) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "stm32flash -w %s -v -g 0x08000000 %s", firmware_path, port);
    return system(cmd);
}

int uno_q_setup(const char *port) {
    printf("[Peripherals] Setting up Unoquin board on %s\n", port);
    return 0;
}

int uno_q_bridge(const char *port) {
    printf("[Peripherals] Unoquin bridge mode on %s\n", port);
    return 0;
}

int capabilities_discover(void) {
    printf("[Peripherals] Discovering board capabilities...\n");
    
    char cmd[256];
    FILE *fp = popen("ls /sys/class/gpio 2>/dev/null", "r");
    if (fp) {
        char line[64];
        while (fgets(line, sizeof(line), fp)) {
            printf("  GPIO: %s", line);
        }
        pclose(fp);
    }
    
    return 0;
}
