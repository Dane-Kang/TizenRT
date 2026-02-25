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
    g_gpio_led_h = iotbus_gpio_open(GPIO_LED);
    ST_ASSERT_CLEANUP("iotbus_gpio_open", g_gpio_led_h, NULL, ((void)0));

    ret = iotbus_gpio_write(g_gpio_led_h, switch_state);
    printf("iotbus_gpio_write ret : %d\n", ret);

    ret = iotbus_gpio_close(g_gpio_led_h);
}