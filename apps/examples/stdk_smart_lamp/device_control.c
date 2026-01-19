/* ***************************************************************************
 *
 * Copyright 2019 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/

#include "device_control.h"
#include <iotbus/iotbus_gpio.h>
#include <iotbus/iotbus_error.h>

static iotbus_gpio_context_h g_gpio_led_h;
uint32_t button_press_time;
uint32_t long_press_tick = BUTTON_LONG_THRESHOLD_MS;
uint32_t button_delay_tick = BUTTON_DELAY_MS;

void change_switch_state(int switch_state)
{
    int ret;
    g_gpio_led_h = NULL;
    g_gpio_led_h = iotbus_gpio_open(GPIO_INPUT_BUTTON);
    ST_ASSERT_CLEANUP("iotbus_gpio_open", g_gpio_led_h, NULL, ((void)0));

    ret = iotbus_gpio_write(g_gpio_led_h, switch_state);
    ST_ASSERT_CLEANUP("iotbus_gpio_write", (ret < 0), false, iotbus_gpio_close(g_gpio_led_h));

    ret = iotbus_gpio_close(g_gpio_led_h);
    ST_ASSERT_CLEANUP("iotbus_gpio_close", ret , 0, ((void)0));
}

int get_button_event(int* button_event_type, int* button_event_count)
{
#if 1
    static uint32_t button_count = 0;
    bool button_last_state = BUTTON_GPIO_RELEASED;

    uint32_t gpio_level = 0;

    g_gpio_led_h = NULL;
    g_gpio_led_h = iotbus_gpio_open(GPIO_INPUT_BUTTON);
    ST_ASSERT_CLEANUP("iotbus_gpio_open", g_gpio_led_h, NULL, ((void)0));

    gpio_level = iotbus_gpio_read(g_gpio_led_h);
    ST_ASSERT_CLEANUP("iotbus_gpio_read", gpio_level, 0, iotbus_gpio_close(g_gpio_led_h));

    if (button_last_state != gpio_level) {
        /* wait debounce time to ignore small ripple of currunt */
        usleep(BUTTON_DEBOUNCE_TIME_MS*1000);
        gpio_level = iotbus_gpio_read(g_gpio_led_h);
        ST_ASSERT_CLEANUP("iotbus_gpio_read", gpio_level, 0, iotbus_gpio_close(g_gpio_led_h));
        if (button_last_state != gpio_level) {
            printf("Button event, val: %ld, tick: %lu\n", gpio_level, (uint32_t)TICK2MSEC(clock_systimer()));
            button_last_state = gpio_level;
            if (gpio_level == BUTTON_GPIO_PRESSED) {
                button_count++;
                button_press_time = TICK2MSEC(clock_systimer());
            }
        }
    } else if (button_count > 0) {
        uint32_t timeout = TICK2MSEC(clock_systimer()) - button_press_time;
        if ((gpio_level == BUTTON_GPIO_PRESSED) && (timeout >= long_press_tick)) {
            *button_event_type = BUTTON_LONG_PRESS;
            *button_event_count = 1;
            button_count = 0;
            return true;
        } else if ((gpio_level == BUTTON_GPIO_RELEASED) && (timeout >= button_delay_tick)) {
            *button_event_type = BUTTON_SHORT_PRESS;
            *button_event_count = button_count;
            button_count = 0;
            return true;
        }
    }

    return false;
#else
    return true;
#endif
}

void iot_gpio_init(void)
{
    // struct gpio_lowerhalf_s *lower;
    // lower = amebasmart_gpio_lowerhalf(PB_20, PIN_OUTPUT, PullDown);
	// gpio_register(PB_20, lower);
}


