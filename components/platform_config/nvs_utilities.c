#include "nvs_utilities.h"

#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"

#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_utilities.h"
#include "platform_config.h"
#include "tools.h"

const char current_namespace[] = "config";
const char settings_partition[] = "settings";
static const char * TAG = "nvs_utilities";

typedef struct {
    nvs_type_t type;
    const char *str;
} type_str_pair_t;

static const type_str_pair_t type_str_pair[] = {
    { NVS_TYPE_I8, "i8" },
    { NVS_TYPE_U8, "u8" },
    { NVS_TYPE_U16, "u16" },
    { NVS_TYPE_I16, "i16" },
    { NVS_TYPE_U32, "u32" },
    { NVS_TYPE_I32, "i32" },
    { NVS_TYPE_U64, "u64" },
    { NVS_TYPE_I64, "i64" },
    { NVS_TYPE_STR, "str" },
    { NVS_TYPE_BLOB, "blob" },
    { NVS_TYPE_ANY, "any" },
};

static const size_t TYPE_STR_PAIR_SIZE = sizeof(type_str_pair) / sizeof(type_str_pair[0]);
void print_blob(const char *blob, size_t len)
{
    for (int i = 0; i < len; i++) {
        printf("%02x", blob[i]);
    }
    printf("\n");
}
nvs_type_t str_to_type(const char *type)
{
    for (int i = 0; i < TYPE_STR_PAIR_SIZE; i++) {
        const type_str_pair_t *p = &type_str_pair[i];
        if (strcmp(type, p->str) == 0) {
            return  p->type;
        }
    }

    return NVS_TYPE_ANY;
}
const char *type_to_str(nvs_type_t type)
{
    for (int i = 0; i < TYPE_STR_PAIR_SIZE; i++) {
        const type_str_pair_t *p = &type_str_pair[i];
        if (p->type == type) {
            return  p->str;
        }
    }

    return "Unknown";
}
void erase_settings_partition(){
	ESP_LOGW(TAG,  "Erasing nvs on partition %s",settings_partition);
	ESP_ERROR_CHECK(nvs_flash_erase_partition(settings_partition));
	nvs_flash_init_partition(settings_partition);
}
void initialize_nvs() {
	ESP_LOGI(TAG,  "Initializing flash nvs ");
	esp_err_t err = nvs_flash_init();
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_LOGW(TAG,  "%s. Erasing nvs flash", esp_err_to_name(err));
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	if(err != ESP_OK){
		ESP_LOGE(TAG,  "nvs_flash_init failed. %s.", esp_err_to_name(err));
	}
	ESP_ERROR_CHECK(err);
	ESP_LOGI(TAG,  "Initializing nvs partition %s",settings_partition);
	err = nvs_flash_init_partition(settings_partition);
	if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_LOGW(TAG,  "%s. Erasing nvs on partition %s",esp_err_to_name(err),settings_partition);
		ESP_ERROR_CHECK(nvs_flash_erase_partition(settings_partition));
		err = nvs_flash_init_partition(settings_partition);
	}
	if(err!=ESP_OK){
		ESP_LOGE(TAG,  "nvs_flash_init_partition failed. %s",esp_err_to_name(err));
	}
	ESP_ERROR_CHECK(err);
	ESP_LOGD(TAG,  "nvs init completed");
}

esp_err_t nvs_load_config() {
    nvs_entry_info_t info;
    esp_err_t err = ESP_OK;
    size_t malloc_int = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t malloc_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    nvs_iterator_t it = NULL;
    err = nvs_entry_find(settings_partition, NULL, NVS_TYPE_ANY, &it);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "empty nvs partition %s, namespace %s", settings_partition, current_namespace);
    }
    while (err == ESP_OK && it != NULL) {
        nvs_entry_info(it, &info);

        if (strstr(info.namespace_name, current_namespace)) {
            if (strlen(info.key) == 0) {
                ESP_LOGW(TAG, "empty key name in namespace %s. Removing it.", current_namespace);
                nvs_handle_t nvs_handle;
                err = nvs_open(settings_partition, NVS_READWRITE, &nvs_handle);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "nvs_open failed. %s", esp_err_to_name(err));
                } else {
                    if ((err = nvs_erase_key(nvs_handle, info.key)) != ESP_OK) {
                        ESP_LOGE(TAG, "nvs_erase_key failed. %s", esp_err_to_name(err));
                    } else {
                        nvs_commit(nvs_handle);
                    }
                    nvs_close(nvs_handle);
                    if (err == ESP_OK) {
                        ESP_LOGW(TAG, "nvs_erase_key completed on empty key. Restarting system to apply changes.");
                        esp_restart();
                    }
                }
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "nvs_erase_key failed on empty key. Configuration partition should be erased.  %s", esp_err_to_name(err));
                    err = ESP_OK;
                }
            }
			else {
				void* value = get_nvs_value_alloc(info.type, info.key);
				if (value == NULL) {
					ESP_LOGE(TAG, "nvs read failed.");
					nvs_release_iterator(it);
					return ESP_FAIL;
				}
				config_set_value(info.type, info.key, value);
				free(value);
			}
        }
        err = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    char* json_string = config_alloc_get_json(false);
    if (json_string != NULL) {
        ESP_LOGD(TAG, "config json : %s\n", json_string);
        free(json_string);
    }

    ESP_LOGW(TAG, "Configuration memory usage.  Heap internal:%zu (min:%zu) (used:%zu) external:%zu (min:%zu) (used:%zd)",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             malloc_int - heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
             malloc_spiram - heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return err;
}

esp_err_t store_nvs_value(nvs_type_t type, const char *key, void * data) {
	if (type == NVS_TYPE_BLOB)
		return ESP_ERR_NVS_TYPE_MISMATCH;
	return store_nvs_value_len(type, key, data,0);
}
esp_err_t store_nvs_value_len_for_partition(const char * partition,const char * namespace,nvs_type_t type, const char *key, const void * data,size_t data_len) {
	esp_err_t err;
	nvs_handle nvs;
	if(!key || key[0]=='\0'){
		ESP_LOGE(TAG,  "Cannot store value to nvs: key is empty");
		return ESP_ERR_INVALID_ARG;
	}
	
	if (type == NVS_TYPE_ANY) {
		return ESP_ERR_NVS_TYPE_MISMATCH;
	}

	err = nvs_open_from_partition(partition, namespace, NVS_READWRITE, &nvs);
	if (err != ESP_OK) {
		return err;
	}

	if (type == NVS_TYPE_I8) {
		err = nvs_set_i8(nvs, key, *(int8_t *) data);
	} else if (type == NVS_TYPE_U8) {
		err = nvs_set_u8(nvs, key, *(uint8_t *) data);
	} else if (type == NVS_TYPE_I16) {
		err = nvs_set_i16(nvs, key, *(int16_t *) data);
	} else if (type == NVS_TYPE_U16) {
		err = nvs_set_u16(nvs, key, *(uint16_t *) data);
	} else if (type == NVS_TYPE_I32) {
		err = nvs_set_i32(nvs, key, *(int32_t *) data);
	} else if (type == NVS_TYPE_U32) {
		err = nvs_set_u32(nvs, key, *(uint32_t *) data);
	} else if (type == NVS_TYPE_I64) {
		err = nvs_set_i64(nvs, key, *(int64_t *) data);
	} else if (type == NVS_TYPE_U64) {
		err = nvs_set_u64(nvs, key, *(uint64_t *) data);
	} else if (type == NVS_TYPE_STR) {
		err = nvs_set_str(nvs, key, data);
	} else if (type == NVS_TYPE_BLOB) {
		err = nvs_set_blob(nvs, key, (void *) data, data_len);
	}
	if (err == ESP_OK) {
		err = nvs_commit(nvs);
		if (err == ESP_OK) {
			ESP_LOGI(TAG,   "Value stored under key '%s'", key);
		}
	}
	nvs_close(nvs);
	return err;
}
esp_err_t store_nvs_value_len(nvs_type_t type, const char *key, void * data,
		size_t data_len) {
	return store_nvs_value_len_for_partition(settings_partition,current_namespace,type,key,data,data_len);
}
void * get_nvs_value_alloc_for_partition(const char * partition,const char * namespace,nvs_type_t type, const char *key, size_t * size){
	nvs_handle nvs;
	esp_err_t err;
	void * value=NULL;
	if(size){
		*size=0;
	}
	err = nvs_open_from_partition(partition, namespace, NVS_READONLY, &nvs);
	if (err != ESP_OK) {
		ESP_LOGE(TAG,  "Could not open the nvs storage.");
		return NULL;
	}

	if (type == NVS_TYPE_I8) {
		value=malloc_init_external(sizeof(int8_t));
		err = nvs_get_i8(nvs, key, (int8_t *) value);
	} else if (type == NVS_TYPE_U8) {
		value=malloc_init_external(sizeof(uint8_t));
		err = nvs_get_u8(nvs, key, (uint8_t *) value);
	} else if (type == NVS_TYPE_I16) {
		value=malloc_init_external(sizeof(int16_t));
		err = nvs_get_i16(nvs, key, (int16_t *) value);
	} else if (type == NVS_TYPE_U16) {
		value=malloc_init_external(sizeof(uint16_t));
		err = nvs_get_u16(nvs, key, (uint16_t *) value);
	} else if (type == NVS_TYPE_I32) {
		value=malloc_init_external(sizeof(int32_t));
		err = nvs_get_i32(nvs, key, (int32_t *) value);
	} else if (type == NVS_TYPE_U32) {
		value=malloc_init_external(sizeof(uint32_t));
		err = nvs_get_u32(nvs, key, (uint32_t *) value);
	} else if (type == NVS_TYPE_I64) {
		value=malloc_init_external(sizeof(int64_t));
		err = nvs_get_i64(nvs, key, (int64_t *) value);
	} else if (type == NVS_TYPE_U64) {
		value=malloc_init_external(sizeof(uint64_t));
		err = nvs_get_u64(nvs, key, (uint64_t *) value);
	} else if (type == NVS_TYPE_STR) {
		size_t len=0;
		err = nvs_get_str(nvs, key, NULL, &len);
		if (err == ESP_OK) {
			value=malloc_init_external(len+1);
			err = nvs_get_str(nvs, key, value, &len);
			if(size){
				*size=len;
			}			
		}
	} else if (type == NVS_TYPE_BLOB) {
		size_t len;
		err = nvs_get_blob(nvs, key, NULL, &len);
		if (err == ESP_OK) {
			value=malloc_init_external(len+1);
			if(size){
				*size=len;
			}
			err = nvs_get_blob(nvs, key, value, &len);
		}
	}
	if(err!=ESP_OK){
		ESP_LOGD(TAG,  "Value not found for key %s",key);
		if(value!=NULL)
			free(value);
		value=NULL;
	}
	nvs_close(nvs);
	return value;
}
void * get_nvs_value_alloc(nvs_type_t type, const char *key) {
		return get_nvs_value_alloc_for_partition(settings_partition, current_namespace,type,key,NULL);
}
esp_err_t get_nvs_value(nvs_type_t type, const char *key, void*value, const uint8_t buf_size) {
	nvs_handle nvs;
	esp_err_t err;

	err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READONLY, &nvs);
	if (err != ESP_OK) {
		return err;
	}

	if (type == NVS_TYPE_I8) {
		err = nvs_get_i8(nvs, key, (int8_t *) value);
	} else if (type == NVS_TYPE_U8) {
		err = nvs_get_u8(nvs, key, (uint8_t *) value);
	} else if (type == NVS_TYPE_I16) {
		err = nvs_get_i16(nvs, key, (int16_t *) value);
	} else if (type == NVS_TYPE_U16) {
		err = nvs_get_u16(nvs, key, (uint16_t *) value);
	} else if (type == NVS_TYPE_I32) {
		err = nvs_get_i32(nvs, key, (int32_t *) value);
	} else if (type == NVS_TYPE_U32) {
		err = nvs_get_u32(nvs, key, (uint32_t *) value);
	} else if (type == NVS_TYPE_I64) {
		err = nvs_get_i64(nvs, key, (int64_t *) value);
	} else if (type == NVS_TYPE_U64) {
		err = nvs_get_u64(nvs, key, (uint64_t *) value);
	} else if (type == NVS_TYPE_STR) {
		size_t len;
		if ((err = nvs_get_str(nvs, key, NULL, &len)) == ESP_OK) {
			if (len > buf_size) {
				//ESP_LOGE("Error reading value for %s.  Buffer size: %d, Value Length: %d", key, buf_size, len);
				err = ESP_FAIL;
			} else {
				err = nvs_get_str(nvs, key, value, &len);
			}
		}
	} else if (type == NVS_TYPE_BLOB) {
		size_t len;
		if ((err = nvs_get_blob(nvs, key, NULL, &len)) == ESP_OK) {

			if (len > buf_size) {
				//ESP_LOGE("Error reading value for %s.  Buffer size: %d, Value Length: %d",
				//		key, buf_size, len);
				err = ESP_FAIL;
			} else {
				err = nvs_get_blob(nvs, key, value, &len);
			}
		}
	}

	nvs_close(nvs);
	return err;
}
esp_err_t erase_nvs_for_partition(const char * partition, const char * namespace,const char *key)
{
    nvs_handle nvs;
    esp_err_t err = nvs_open_from_partition(partition,namespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, key);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
            if (err == ESP_OK) {
                ESP_LOGI(TAG,   "Value with key '%s' erased", key);
            }
        }
        nvs_close(nvs);
    }
	else {
		ESP_LOGE(TAG,"Could not erase key %s from partition %s namespace %s : %s", key,partition,namespace, esp_err_to_name(err));
	}
    return err;
}
esp_err_t erase_nvs(const char *key)
{
	return erase_nvs_for_partition(NVS_DEFAULT_PART_NAME, current_namespace,key);
}

esp_err_t erase_nvs_partition(const char * partition, const char * namespace){
    nvs_handle nvs;
	const char * step = "Opening";
    ESP_LOGD(TAG,"%s partition %s, namespace %s ",step,partition,namespace);
	esp_err_t err = nvs_open_from_partition(partition,namespace, NVS_READWRITE, &nvs);
	if (err == ESP_OK) {
		step = "Erasing";
        ESP_LOGD(TAG,"%s namespace %s ",step,partition);
		err = nvs_erase_all(nvs);
		if (err == ESP_OK) {
			step = "Committing";
            ESP_LOGD(TAG,"%s",step);
			err = nvs_commit(nvs);
		}
	}
	if(err !=ESP_OK){
		ESP_LOGE(TAG,"%s partition %s, name space %s : %s",step,partition,namespace,esp_err_to_name(err));
	}
    ESP_LOGD(TAG,"Closing %s ",namespace);
	nvs_close(nvs);
	return err;
}