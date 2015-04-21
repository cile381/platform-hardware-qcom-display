/*
 * Copyright (C) 2008 The Android Open Source Project.
 * Copyright (C) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


// #define LOG_NDEBUG 0

#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/ioctl.h>
#include <sys/types.h>

#include <hardware/lights.h>

#include <cutils/properties.h>

/******************************************************************************/

static pthread_once_t g_init = PTHREAD_ONCE_INIT;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static struct light_state_t g_notification;
static struct light_state_t g_battery;
static int g_attention = 0;

char const*const RED_LED_FILE
        = "/sys/class/leds/red/brightness";

char const*const GREEN_LED_FILE
        = "/sys/class/leds/green/brightness";

char const*const BLUE_LED_FILE
        = "/sys/class/leds/blue/brightness";

char const*const LCD_FILE
        = "/sys/class/leds/lcd-backlight/brightness";

char const*const BUTTON_FILE
        = "/sys/class/leds/button-backlight/brightness";

char const*const RED_BLINK_FILE
        = "/sys/class/leds/red/blink";

char const*const GREEN_BLINK_FILE
        = "/sys/class/leds/green/blink";

char const*const BLUE_BLINK_FILE
        = "/sys/class/leds/blue/blink";

/* QM8626 LED Devices */
char const*const QM8626_RED_LED1_BRIGHT
        = "/sys/class/leds/red/brightness";
char const*const QM8626_GREEN_LED1_BRIGHT
        = "/sys/class/leds/green/brightness";
char const*const QM8626_BLUE_LED1_BRIGHT
        = "/sys/class/leds/blue/brightness";

char const*const QM8626_RED_LED2_BRIGHT
        = "/sys/class/leds/lp5523:channel0/brightness";
char const*const QM8626_GREEN_LED2_BRIGHT
        = "/sys/class/leds/lp5523:channel1/brightness";
char const*const QM8626_BLUE_LED2_BRIGHT
        = "/sys/class/leds/lp5523:channel2/brightness";

char const*const QM8626_LED1_RED_ENG1_MODE
        = "/sys/class/leds/red/device/engine1_mode";
char const*const QM8626_LED1_GREEN_ENG2_MODE
        = "/sys/class/leds/green/device/engine2_mode";
char const*const QM8626_LED1_BLUE_ENG3_MODE
        = "/sys/class/leds/blue/device/engine3_mode";

char const*const QM8626_LED2_RED_CHAN0_ENG1_MODE
        = "/sys/class/leds/lp5523:channel0/device/engine1_mode";
char const*const QM8626_LED2_GREEN_CHAN1_ENG2_MODE
        = "/sys/class/leds/lp5523:channel1/device/engine2_mode";
char const*const QM8626_LED2_BLUE_CHAN2_ENG3_MODE
        = "/sys/class/leds/lp5523:channel2/device/engine3_mode";

char const*const QM8626_LED1_RED_ENG1_LOAD
        = "/sys/class/leds/red/device/engine1_load";
char const*const QM8626_LED1_GREEN_ENG2_LOAD
        = "/sys/class/leds/green/device/engine2_load";
char const*const QM8626_LED1_BLUE_ENG3_LOAD
        = "/sys/class/leds/blue/device/engine3_load";

char const*const QM8626_LED2_RED_CHAN0_ENG1_LOAD
        = "/sys/class/leds/lp5523:channel0/device/engine1_load";
char const*const QM8626_LED2_GREEN_CHAN1_ENG2_LOAD
        = "/sys/class/leds/lp5523:channel1/device/engine2_load";
char const*const QM8626_LED2_BLUE_CHAN2_ENG3_LOAD
        = "/sys/class/leds/lp5523:channel2/device/engine3_load";

char const*const QM8626_LED_ENG_MODE_DISABLE = "disabled";
char const*const QM8626_LED_ENG_MODE_LOAD = "load";
char const*const QM8626_LED_ENG_MODE_RUN = "run";


char prop_value[PROPERTY_VALUE_MAX];

enum {
    QM8626_LED_DEV_1,
    QM8626_LED_DEV_2
};

enum {
    QM8626_LED_RED,
    QM8626_LED_GREEN,
    QM8626_LED_BLUE
};

/**
 * device methods
 */

void init_globals(void)
{
    // init the mutex
    pthread_mutex_init(&g_lock, NULL);
}

static unsigned int
is_qm8626(void)
{
    int fd;
    static int already_warned_open = 0;
    static int already_warned_read = 0;
    char buffer[10];
    ssize_t n;
    unsigned int ret = 0;

    fd = open("/sys/devices/soc0/hw_platform",O_RDONLY);

    if (fd >= 0) {
        n = read(fd, buffer, (sizeof("QM8626")-1));

        if(n >= 0) {
            if(strncmp(buffer, "QM8626", (sizeof("QM8626")-1)) == 0)
                ret = 1;
        } else {
            if (already_warned_read == 0) {
                ALOGE("is_qm8626 failed to read %s errno:%d\n",
                       "/sys/devices/soc0/hw_platform",errno);
              already_warned_read = 1;
            }
        }
        close(fd);
    } else {
        if (already_warned_open == 0) {
            ALOGE("is_qm8626 failed to open %s errno:%d\n",
                   "/sys/devices/soc0/hw_platform", errno);
            already_warned_open = 1;
        }
    }
    return(ret);
}

static unsigned int
is_qm8626_subtype_1(void)
{
    int fd;
    static int already_warned_open = 0;
    static int already_warned_read = 0;
    char buffer[2];
    ssize_t n;
    unsigned int ret = 0;

    fd = open("/sys/devices/soc0/platform_subtype_id",O_RDONLY);

    if (fd >= 0) {
        n = read(fd, buffer, (sizeof("1")-1));
        if(n >= 0) {
            if(strncmp(buffer, "1", (sizeof("1")-1)) == 0)
                ret = 1;
        } else {
            if (already_warned_read == 0) {
                ALOGE("is_qm8626_subtype_1 failed to read %s errno:%d\n",
                       "/sys/devices/soc0/platform_subtype_id", errno);
               already_warned_read = 1;
            }
        }
        close(fd);
    } else {
        if (already_warned_open == 0) {
            ALOGE("is_qm8626_subtype_1 failed to open %s errno:%d\n",
                   "/sys/devices/soc0/platform_subtype_id", errno);
            already_warned_open = 1;
        }
    }

    return(ret);
}

/* Checks if we can use the LP5521 device on Qm8626 for Android lights */
static unsigned int
is_qm8626_led1_enabled(void)
{
    unsigned int ret=0;

    if (property_get("persist.qm8626.andled_1.enable", prop_value, NULL)) {
        if (strcmp(prop_value, "0") == 0)
            ret = 0;
        else
            ret = 1;
    } else {
        ALOGE("is_qm8626_led1_enabled failed to read property -\
               persist.qm8626.andled_1.enable\n");
    }

    return(ret);
}

/* Checks if we can use the LP55231 device on Qm8626 for Android lights */
static unsigned int
is_qm8626_led2_enabled(void)
{
    unsigned int ret=0;

    if (property_get("persist.qm8626.andled_2.enable", prop_value, NULL)) {
        if (strcmp(prop_value, "0") == 0)
            ret = 0;
        else
            ret = 1;
    } else {
        ALOGE("is_qm8626_led2_enabled failed to read property -\
               persist.qm8626.andled_2.enable\n");
    }

    return(ret);
}


static void
qm8626_set_led_engine_mode(unsigned int ledDevice, unsigned int led,
                                     char const* mode)
{
    int fd;
    static int already_warned_open;

    /* Disable any programs running in the selected LED devices engine */

    if (ledDevice == QM8626_LED_DEV_1) {
        if (led == QM8626_LED_RED)
            fd = open(QM8626_LED1_RED_ENG1_MODE,O_RDWR);
        else if (led == QM8626_LED_GREEN)
            fd = open(QM8626_LED1_GREEN_ENG2_MODE,O_RDWR);
        else
            fd = open(QM8626_LED1_BLUE_ENG3_MODE,O_RDWR);
    } else {
        if (led == QM8626_LED_RED)
            fd = open(QM8626_LED2_RED_CHAN0_ENG1_MODE,O_RDWR);
        else if (led == QM8626_LED_GREEN)
            fd = open(QM8626_LED2_GREEN_CHAN1_ENG2_MODE,O_RDWR);
        else
            fd = open(QM8626_LED2_BLUE_CHAN2_ENG3_MODE,O_RDWR);
    }

    if (fd >= 0) {
        if (write(fd, mode, strlen(mode)) <= 0)
            ALOGE("qm8626_set_led_engine_mode - write mode failed.\
                   ledDevice:%d led:%d mode:%s\n",ledDevice,led,mode);
        close(fd);
    } else {
        if (already_warned_open == 0) {
            ALOGE("qm8626_set_led_engine_mode - open failed %s ledDev:%d\
                  led:%d\n", "/sys/class/leds/<color | lp5523:channelN>/\
                  device/engineN_mode", ledDevice,led);
            already_warned_open = 1;
        }
    }

}

static void
qm8626_set_brightness(unsigned int ledDevice, unsigned int led, int brightness)
{
    char buffer[20];
    int bytes;
    int fd;
    static int already_warned_open;

    if (ledDevice == QM8626_LED_DEV_1) {
        if (led == QM8626_LED_RED)
            fd = open(QM8626_RED_LED1_BRIGHT,O_RDWR);
        else if (led == QM8626_LED_GREEN)
            fd = open(QM8626_GREEN_LED1_BRIGHT,O_RDWR);
        else
            fd = open(QM8626_BLUE_LED1_BRIGHT,O_RDWR);
    } else {
        if (led == QM8626_LED_RED)
            fd = open(QM8626_RED_LED2_BRIGHT,O_RDWR);
        else if (led == QM8626_LED_GREEN)
            fd = open(QM8626_GREEN_LED2_BRIGHT,O_RDWR);
        else
            fd = open(QM8626_BLUE_LED2_BRIGHT,O_RDWR);
    }

    if (fd >= 0) {
        bytes = sprintf(buffer, "%d", brightness);
        if(write(fd, buffer, bytes) <= 0)
            ALOGE("qm8626_set_brightness - write failed\
             ledDevice:%d led:%d bright:%d\n",ledDevice,led, brightness);
        close(fd);
    } else {
        if (already_warned_open == 0) {
            ALOGE("qm8626_set_brightness - open failed %s ledDev:%d led:%d\n",
                  "/sys/class/leds/<color | lp5523:channelN>/brightness",
                  ledDevice,led);
            already_warned_open = 1;
        }
    }

}

static void
qm8626_turn_on_blink_led(unsigned int ledDevice, unsigned int led,
                                 int brightness, int onMS, int offMS)
{
    int fd;
    static int already_warned_open;
    char program[50];
    unsigned int num_484_ms, num_999_ms = 0;
    unsigned int num_15_625_ms = 0;
    unsigned int prog_count = 0;

    /* Ensure LED is off currently */
    qm8626_set_led_engine_mode(ledDevice, led, QM8626_LED_ENG_MODE_DISABLE);
    qm8626_set_brightness(ledDevice, led, 0);

    /* Put engine in LOAD mode ready for program load */
    qm8626_set_led_engine_mode(ledDevice, led, QM8626_LED_ENG_MODE_LOAD);

    if (ledDevice == QM8626_LED_DEV_1) {
        if (led == QM8626_LED_RED)
            fd = open(QM8626_LED1_RED_ENG1_LOAD,O_WRONLY);
        else if (led == QM8626_LED_GREEN)
            fd = open(QM8626_LED1_GREEN_ENG2_LOAD,O_WRONLY);
        else
            fd = open(QM8626_LED1_BLUE_ENG3_LOAD,O_WRONLY);
    } else {
        if (led == QM8626_LED_RED)
            fd = open(QM8626_LED2_RED_CHAN0_ENG1_LOAD,O_WRONLY);
        else if (led == QM8626_LED_GREEN)
            fd = open(QM8626_LED2_GREEN_CHAN1_ENG2_LOAD,O_WRONLY);
        else
            fd = open(QM8626_LED2_BLUE_CHAN2_ENG3_LOAD,O_WRONLY);
    }

    if (fd < 0) {
       if (already_warned_open == 0) {
           ALOGE("qm8626_turn_on_blink_led - failed open %s ledDev:%d\
                  led:%d\n","/sys/class/leds/<color | lp5523:channelN>/\
                  device/engineN_load", ledDevice,led);
           already_warned_open = 1;
       }
       return;
    }

    /* Prepare program to load */

    if ( ledDevice == QM8626_LED_DEV_2)
        goto load_led2_prog;

    num_999_ms = onMS/999;
    /* Limit time to max of 63x999ms = 60secs */
    num_999_ms = (num_999_ms > 63)?63:num_999_ms;
    num_15_625_ms = (onMS%999)/15.625;

    /* Set minimum time to 15.625ms */
    if(num_999_ms == 0 && num_15_625_ms == 0)
        num_15_625_ms = 1;

    /* Add code to select PWM for this LED */
    prog_count = sprintf(((&program[0])+prog_count), "40%02X", brightness);

    /* Add code to loop through N 999ms periods if required */
    if(num_999_ms)
        prog_count += sprintf(((&program[0])+prog_count), "7F00%04X",
                               (0xA001 | (num_999_ms<<7)));

    /* Add code for any additional ms upto 998ms in 15.6ms increments */
    if(num_15_625_ms)
        prog_count += sprintf(((&program[0])+prog_count), "%04X",
                                (0x4000 | (num_15_625_ms<<8)));

    /* Add PWM off code */
    prog_count += sprintf(((&program[0])+prog_count), "4000");

    /* Add code for off period */

    num_999_ms = offMS/999;
    /* Limit time to max of 63x999ms = 60secs */
    num_999_ms = (num_999_ms > 63)?63:num_999_ms;
    num_15_625_ms = (offMS%999)/15.625;

    /* This sets minimum time to 15.625ms */
    if(num_999_ms == 0 && num_15_625_ms == 0)
        num_15_625_ms = 1;

    /* Add code to loop through N 999ms periods if required */
    if(num_999_ms)
        prog_count += sprintf(((&program[0])+prog_count), "7E00%04X",
                               (0xA000 | (prog_count/4) | (num_999_ms<<7)));

    /* Add code for any additional ms up to 999ms in 15.625ms increments */
    if(num_15_625_ms)
        prog_count += sprintf(((&program[0])+prog_count), "%04X",
                                 (0x4000 | (num_15_625_ms<<8)));

    goto load_program;


load_led2_prog:

    num_484_ms = onMS/484;
    /* Limit time to max of 63x484ms = 30secs */
    num_484_ms = (num_484_ms > 63)?63:num_484_ms;
    num_15_625_ms = (onMS%484)/15.625;

    /* Set minimum time to 15.625ms */
    if(num_484_ms == 0 && num_15_625_ms == 0)
        num_15_625_ms = 1;

    /* Add code to select LED and the PWM for this LED */
    prog_count = sprintf(((&program[0])+prog_count), "9D%02X40%02X",
                           (led+1),brightness);

    /* Add code to loop through N 484ms periods if required */
    if(num_484_ms)
        prog_count += sprintf(((&program[0])+prog_count), "7E00%04X",
                                (0xA002 | (num_484_ms<<7)));

    /* Add code for any additional ms upto 483ms in 15.625 ms increments*/
    if(num_15_625_ms)
        prog_count += sprintf(((&program[0])+prog_count), "%04X",
                                (0x4000 | (num_15_625_ms<<9)));

    /* Add PWM off code */
    prog_count += sprintf(((&program[0])+prog_count), "4000");

    /* Add code for off period */

    num_484_ms = offMS/484;
    /* Limit time to max of 63x484ms = 30secs */
    num_484_ms = (num_484_ms > 63)?63:num_484_ms;
    num_15_625_ms = (offMS%484)/15.625;

    /* This sets minimum time to 15.625ms */
    if(num_484_ms == 0 && num_15_625_ms == 0)
        num_15_625_ms = 1;

    /* Add code to loop through N 484ms periods if required */
    if(num_484_ms)
        prog_count += sprintf(((&program[0])+prog_count), "7E00%04X",
                                (0xA000 | (prog_count/4) | (num_484_ms<<7)));

    /* Add code for any additional ms upto 483ms */
    if(num_15_625_ms)
        prog_count += sprintf(((&program[0])+prog_count), "%04X",
                                (0x4000 | (num_15_625_ms<<9)));

    /* Add Loop back to start code */
    prog_count += sprintf(((&program[0])+prog_count), "A001");

load_program:

    if(write(fd, program, prog_count) <= 0)
        ALOGE("qm8626_turn_on_blink_led - write program failed.\
               ledDevice:%d led:%d\n",ledDevice,led);
    close(fd);

    qm8626_set_led_engine_mode(ledDevice, led,QM8626_LED_ENG_MODE_RUN);


}

static int
qm8626_set_speaker_light_locked(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int len;
    int alpha, brightness;
    int blink;
    int onMS, offMS;
    unsigned int colorARGB = state->color;
    unsigned int ledDevice, led;

    /* If not HWID:1 and no other LED device enabled for qm8626 do nothing */
    if (!(is_qm8626_subtype_1()) && !(is_qm8626_led1_enabled())
            && !(is_qm8626_led2_enabled()))
        return(0);

    /* Ignore request if no RGB LED value provided */
    if ((state->color & 0x00ffffff) == 0)
        return(0);

    /* Select the device to be used */
    if (is_qm8626_subtype_1() || is_qm8626_led1_enabled())
        ledDevice = QM8626_LED_DEV_1;
    else
        ledDevice = QM8626_LED_DEV_2;

    /* Only one LED can be changed */
    if ((colorARGB >> 16) & 0xFF) {
        led = QM8626_LED_RED;
        brightness = (colorARGB >> 16) & 0xFF;
    }
    else if ((colorARGB >> 8) & 0xFF) {
        led = QM8626_LED_GREEN;
        brightness = (colorARGB >> 8) & 0xFF;
    } else {
        led = QM8626_LED_BLUE;
        brightness = colorARGB & 0xFF;
    }

    alpha = (colorARGB & 0xFF000000)?1:0;

    onMS = state->flashOnMS;
    offMS = state->flashOffMS;

    if ((alpha == 0) || (onMS == 0 && offMS == 0)) {
        /* Disable any programs running in the LED devices */
        qm8626_set_led_engine_mode(ledDevice, led, QM8626_LED_ENG_MODE_DISABLE);
        /* Set brightness to 0 for selected device/led */
        qm8626_set_brightness(ledDevice, led, 0);
    }
    else if (alpha == 1 && onMS == 1 && offMS == 0) {
        /* Disable any programs running in the device */
        qm8626_set_led_engine_mode(ledDevice, led, QM8626_LED_ENG_MODE_DISABLE);
        qm8626_set_brightness(ledDevice, led, brightness);
    }
    else if (alpha == 1 && onMS > 0 && offMS > 0) {
        qm8626_turn_on_blink_led(ledDevice, led, brightness, onMS, offMS);
    }
    else {
        ALOGE("QM8626 set speaker light locked, bad params. alpha:%d\
               onMS:%d offMS:%d\n",alpha,onMS, offMS);
    }

    return 0;
}

static int
write_int(char const* path, int value)
{
    int fd;
    static int already_warned = 0;

    fd = open(path, O_RDWR);
    if (fd >= 0) {
        char buffer[20];
        int bytes = snprintf(buffer, sizeof(buffer), "%d\n", value);
        ssize_t amt = write(fd, buffer, (size_t)bytes);
        close(fd);
        return amt == -1 ? -errno : 0;
    } else {
        if (already_warned == 0) {
            ALOGE("write_int failed to open %s\n", path);
            already_warned = 1;
        }
        return -errno;
    }
}

static int
is_lit(struct light_state_t const* state)
{
    return state->color & 0x00ffffff;
}

static int
rgb_to_brightness(struct light_state_t const* state)
{
    int color = state->color & 0x00ffffff;
    return ((77*((color>>16)&0x00ff))
            + (150*((color>>8)&0x00ff)) + (29*(color&0x00ff))) >> 8;
}

static int
set_light_backlight(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    int brightness = rgb_to_brightness(state);
    pthread_mutex_lock(&g_lock);
    err = write_int(LCD_FILE, brightness);
    pthread_mutex_unlock(&g_lock);
    return err;
}

static int
set_speaker_light_locked(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int len;
    int alpha, red, green, blue;
    int blink;
    int onMS, offMS;
    unsigned int colorRGB;

    switch (state->flashMode) {
        case LIGHT_FLASH_TIMED:
            onMS = state->flashOnMS;
            offMS = state->flashOffMS;
            break;
        case LIGHT_FLASH_NONE:
        default:
            onMS = 0;
            offMS = 0;
            break;
    }

    colorRGB = state->color;

#if 0
    ALOGD("set_speaker_light_locked mode %d, colorRGB=%08X, onMS=%d, offMS=%d\n",
            state->flashMode, colorRGB, onMS, offMS);
#endif

    red = (colorRGB >> 16) & 0xFF;
    green = (colorRGB >> 8) & 0xFF;
    blue = colorRGB & 0xFF;

    if (onMS > 0 && offMS > 0) {
        blink = 1;
    } else {
        blink = 0;
    }

    if (blink) {
        if (red)
            write_int(RED_BLINK_FILE, blink);
        if (green)
            write_int(GREEN_BLINK_FILE, blink);
        if (blue)
            write_int(BLUE_BLINK_FILE, blink);
    } else {
        write_int(RED_LED_FILE, red);
        write_int(GREEN_LED_FILE, green);
        write_int(BLUE_LED_FILE, blue);
    }

    return 0;
}

static void
handle_speaker_battery_locked(struct light_device_t* dev)
{
    if (is_lit(&g_battery)) {
        if (is_qm8626())
            qm8626_set_speaker_light_locked(dev, &g_battery);
        else
            set_speaker_light_locked(dev, &g_battery);
    } else {
        if (is_qm8626())
            qm8626_set_speaker_light_locked(dev, &g_notification);
        else
            set_speaker_light_locked(dev, &g_notification);
    }
}

static int
set_light_notifications(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    g_notification = *state;
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_attention(struct light_device_t* dev,
        struct light_state_t const* state)
{
    pthread_mutex_lock(&g_lock);
    if (state->flashMode == LIGHT_FLASH_HARDWARE) {
        g_attention = state->flashOnMS;
    } else if (state->flashMode == LIGHT_FLASH_NONE) {
        g_attention = 0;
    }
    handle_speaker_battery_locked(dev);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static int
set_light_buttons(struct light_device_t* dev,
        struct light_state_t const* state)
{
    int err = 0;
    pthread_mutex_lock(&g_lock);
    err = write_int(BUTTON_FILE, state->color & 0xFF);
    pthread_mutex_unlock(&g_lock);
    return err;
}

/** Close the lights device */
static int
close_lights(struct light_device_t *dev)
{
    if (dev) {
        free(dev);
    }
    return 0;
}


/******************************************************************************/

/**
 * module methods
 */

/** Open a new instance of a lights device using name */
static int open_lights(const struct hw_module_t* module, char const* name,
        struct hw_device_t** device)
{
    int (*set_light)(struct light_device_t* dev,
            struct light_state_t const* state);

    if (0 == strcmp(LIGHT_ID_BACKLIGHT, name))
        set_light = set_light_backlight;
    else if (0 == strcmp(LIGHT_ID_NOTIFICATIONS, name))
        set_light = set_light_notifications;
    else if (0 == strcmp(LIGHT_ID_BUTTONS, name))
        set_light = set_light_buttons;
    else if (0 == strcmp(LIGHT_ID_ATTENTION, name))
        set_light = set_light_attention;
    else
        return -EINVAL;

    pthread_once(&g_init, init_globals);

    struct light_device_t *dev = malloc(sizeof(struct light_device_t));

    if(!dev)
        return -ENOMEM;

    memset(dev, 0, sizeof(*dev));

    dev->common.tag = HARDWARE_DEVICE_TAG;
    dev->common.version = 0;
    dev->common.module = (struct hw_module_t*)module;
    dev->common.close = (int (*)(struct hw_device_t*))close_lights;
    dev->set_light = set_light;

    *device = (struct hw_device_t*)dev;
    return 0;
}

static struct hw_module_methods_t lights_module_methods = {
    .open =  open_lights,
};

/*
 * The lights Module
 */
struct hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 1,
    .version_minor = 0,
    .id = LIGHTS_HARDWARE_MODULE_ID,
    .name = "lights Module",
    .author = "Google, Inc.",
    .methods = &lights_module_methods,
};
