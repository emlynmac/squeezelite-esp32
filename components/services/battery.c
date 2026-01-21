/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "battery.h"
#include "platform_config.h"

/* 
 There is a bug in esp32 which causes a spurious interrupt on gpio 36/39 when
 using ADC, AMP and HALL sensor. Rather than making battery aware, we just ignore
 if as the interrupt lasts 80ns and should be debounced (and the ADC read does not
 happen very often)
*/ 

#define BATTERY_TIMER	(10*1000)

static const char *TAG = "battery";

static struct {
	int channel;
	float sum, avg, scale;
	int count;
	int cells, attenuation;
	TimerHandle_t timer;
	adc_oneshot_unit_handle_t adc_handle;
} battery = { 
	.channel = -1,
	.cells = 2,
	.adc_handle = NULL,
};	

void (*battery_handler_svc)(float value, int cells);

/****************************************************************************************
 * 
 */
float battery_value_svc(void) {
	return battery.avg;
 }
 
/****************************************************************************************
 * 
 */
uint8_t battery_level_svc(void) {
	// TODO: this is vastly incorrect
	int level = battery.avg ? (battery.avg - (3.0 * battery.cells)) / ((4.2 - 3.0) * battery.cells) * 100 : 0;
	return level < 100 ? level : 100;
}

/****************************************************************************************
 * 
 */
static void battery_callback(TimerHandle_t xTimer) {
	int adc_raw = 0;
	if (battery.adc_handle != NULL) {
		adc_oneshot_read(battery.adc_handle, battery.channel, &adc_raw);
		battery.sum += adc_raw * battery.scale / 4095.0;
		if (++battery.count == 30) {
			battery.avg = battery.sum / battery.count;
			battery.sum = battery.count = 0;
			if (battery_handler_svc) (battery_handler_svc)(battery.avg, battery.cells);
			ESP_LOGI(TAG, "Voltage %.2fV", battery.avg);
		}	
	}
}

/****************************************************************************************
 * 
 */
void battery_svc_init(void) {
	char *nvs_item = config_alloc_get_default(NVS_TYPE_STR, "bat_config", "", 0);
	
#ifdef CONFIG_BAT_LOCKED
	char *p = nvs_item;
	asprintf(&nvs_item, CONFIG_BAT_CONFIG ",%s", p);
	free(p);
#endif		

	if (nvs_item) {
		PARSE_PARAM(nvs_item, "channel", '=', battery.channel);
		PARSE_PARAM_FLOAT(nvs_item, "scale", '=', battery.scale);
		PARSE_PARAM(nvs_item, "atten", '=', battery.attenuation);
		PARSE_PARAM(nvs_item, "cells", '=', battery.cells);
		free(nvs_item);
	}	

	if (battery.channel != -1) {
		// Initialize ADC oneshot unit
		adc_oneshot_unit_init_cfg_t init_config = {
			.unit_id = ADC_UNIT_1,
			.ulp_mode = ADC_ULP_MODE_DISABLE,
		};
		ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &battery.adc_handle));

		// Configure ADC channel
		adc_oneshot_chan_cfg_t config = {
			.bitwidth = ADC_BITWIDTH_12,
			.atten = battery.attenuation,
		};
		ESP_ERROR_CHECK(adc_oneshot_config_channel(battery.adc_handle, battery.channel, &config));

		int adc_raw = 0;
		adc_oneshot_read(battery.adc_handle, battery.channel, &adc_raw);
		battery.avg = adc_raw * battery.scale / 4095.0;    
		battery.timer = xTimerCreate("battery", BATTERY_TIMER / portTICK_PERIOD_MS, pdTRUE, NULL, battery_callback);
		xTimerStart(battery.timer, portMAX_DELAY);
		
		ESP_LOGI(TAG, "Battery measure channel: %u, scale %f, atten %d, cells %u, avg %.2fV", battery.channel, battery.scale, battery.attenuation, battery.cells, battery.avg);		
	} else {
		ESP_LOGI(TAG, "No battery");
	}	
}
