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
#include "driver/adc.h"
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
} battery = { 
	.channel = -1,
	.cells = 2,
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
	battery.sum += adc1_get_raw(battery.channel) * battery.scale / 4095.0;
	if (++battery.count == 30) {
		battery.avg = battery.sum / battery.count;
		battery.sum = battery.count = 0;
		if (battery_handler_svc) (battery_handler_svc)(battery.avg, battery.cells);
		ESP_LOGI(TAG, "Voltage %.2fV", battery.avg);
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
		adc1_config_width(ADC_WIDTH_BIT_12);
		adc1_config_channel_atten(battery.channel, battery.attenuation);

		battery.avg = adc1_get_raw(battery.channel) * battery.scale / 4095.0;    
		battery.timer = xTimerCreate("battery", BATTERY_TIMER / portTICK_PERIOD_MS, pdTRUE, NULL, battery_callback);
		xTimerStart(battery.timer, portMAX_DELAY);
		
		ESP_LOGI(TAG, "Battery measure channel: %u, scale %f, atten %d, cells %u, avg %.2fV", battery.channel, battery.scale, battery.attenuation, battery.cells, battery.avg);		
	} else {
		ESP_LOGI(TAG, "No battery");
	}	
}
