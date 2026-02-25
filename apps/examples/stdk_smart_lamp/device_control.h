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
//#include "ameba_soc.h"
//#include "board_pins.h"
#include <stdio.h>
#include <string.h>

#define GPIO_LED 52
#define GPIO_INPUT_BUTTON ( _PA_25 )

enum switch_onoff_state {
    SWITCH_OFF = 0,
    SWITCH_ON = 1,
};

enum main_led_gpio_state {
    MAINLED_GPIO_ON = 1,
    MAINLED_GPIO_OFF = 0,
};

enum led_animation_mode_list {
    LED_ANIMATION_MODE_IDLE = 0,
    LED_ANIMATION_MODE_FAST,
    LED_ANIMATION_MODE_SLOW,
};

enum button_gpio_state {
    BUTTON_GPIO_RELEASED = 1,
    BUTTON_GPIO_PRESSED = 0,
};

#define BUTTON_GPIO_RELEASED    true
#define BUTTON_GPIO_PRESSED     false

#define BUTTON_DEBOUNCE_TIME_MS 20
#define BUTTON_LONG_THRESHOLD_MS 5000
#define BUTTON_DELAY_MS 300

enum button_event_type {
    BUTTON_LONG_PRESS = 0,
    BUTTON_SHORT_PRESS = 1,
};

#define ST_ASSERT_CLEANUP(api_name, var, ref, freeResource) \
{\
	if ((var) == (ref)) {\
		printf("\n[%s] FAIL [Line : %d] %s : Values (%s == 0x%x) and (%s == 0x%x) are not equal\n", __func__, __LINE__, api_name, #var, (int)(var), #ref, (int)(ref)); \
		freeResource; \
		return; \
	} \
}

void change_switch_state(int switch_state);
