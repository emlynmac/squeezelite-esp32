/* Console example — NVS commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#ifdef __cplusplus
extern "C" {
#endif
#include "nvs_flash.h"
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_console.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "cmd_nvs.h"
#include "nvs.h"
#include "nvs_utilities.h"
#include "platform_console.h"
#include "messaging.h"
#include "tools.h"
#include "trace.h"

extern esp_err_t network_wifi_erase_legacy();
extern esp_err_t network_wifi_erase_known_ap();

static const char *ARG_TYPE_STR = "type can be: i8, u8, i16, u16 i32, u32 i64, u64, str, blob";
static const char * TAG = "cmd_nvs";

EXT_RAM_BSS_ATTR static struct {
    struct arg_str *key;
    struct arg_str *type;
    struct arg_str *value;
    struct arg_end *end;
} set_args;

EXT_RAM_BSS_ATTR static struct {
    struct arg_str *key;
    struct arg_str *type;
    struct arg_end *end;
} get_args;

EXT_RAM_BSS_ATTR static struct {
    struct arg_str *key;
    struct arg_end *end;
} erase_args;

EXT_RAM_BSS_ATTR static struct {
    struct arg_str *namespace;
    struct arg_end *end;
} erase_all_args;

EXT_RAM_BSS_ATTR static struct {
    struct arg_str *partition;
    struct arg_str *namespace;
    struct arg_str *type;
    struct arg_end *end;
} list_args;

EXT_RAM_BSS_ATTR static struct {
    struct arg_lit *legacy;
    struct arg_lit *ap_list;
    struct arg_end *end;
} wifi_erase_args;


static esp_err_t store_blob(nvs_handle nvs, const char *key, const char *str_values)
{
    uint8_t value;
    size_t str_len = strlen(str_values);
    size_t blob_len = str_len / 2;

    if (str_len % 2) {
    	log_send_messaging(MESSAGING_ERROR, "Blob data must contain even number of characters");
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    char *blob = (char *)malloc_init_external(blob_len);
    if (blob == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0, j = 0; i < str_len; i++) {
        char ch = str_values[i];
        if (ch >= '0' && ch <= '9') {
            value = ch - '0';
        } else if (ch >= 'A' && ch <= 'F') {
            value = ch - 'A' + 10;
        } else if (ch >= 'a' && ch <= 'f') {
            value = ch - 'a' + 10;
        } else {
        	log_send_messaging(MESSAGING_ERROR, "Blob data contain invalid character");
            free(blob);
            return ESP_ERR_NVS_TYPE_MISMATCH;
        }

        if (i & 1) {
            blob[j++] += value;
        } else {
            blob[j] = value << 4;
        }
    }

    esp_err_t err = nvs_set_blob(nvs, key, blob, blob_len);
    free(blob);

    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    return err;
}

static esp_err_t set_value_in_nvs(const char *key, const char *str_type, const char *str_value)
{
    esp_err_t err;
    nvs_handle nvs;
    bool range_error = false;

    nvs_type_t type = str_to_type(str_type);

    if (type == NVS_TYPE_ANY) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    if (type == NVS_TYPE_I8) {
        int32_t value = strtol(str_value, NULL, 0);
        if (value < INT8_MIN || value > INT8_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_i8(nvs, key, (int8_t)value);
        }
    } else if (type == NVS_TYPE_U8) {
        uint32_t value = strtoul(str_value, NULL, 0);
        if (value > UINT8_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_u8(nvs, key, (uint8_t)value);
        }
    } else if (type == NVS_TYPE_I16) {
        int32_t value = strtol(str_value, NULL, 0);
        if (value < INT16_MIN || value > INT16_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_i16(nvs, key, (int16_t)value);
        }
    } else if (type == NVS_TYPE_U16) {
        uint32_t value = strtoul(str_value, NULL, 0);
        if (value > UINT16_MAX || errno == ERANGE) {
            range_error = true;
        } else {
            err = nvs_set_u16(nvs, key, (uint16_t)value);
        }
    } else if (type == NVS_TYPE_I32) {
        int32_t value = strtol(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_i32(nvs, key, value);
        }
    } else if (type == NVS_TYPE_U32) {
        uint32_t value = strtoul(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_u32(nvs, key, value);
        }
    } else if (type == NVS_TYPE_I64) {
        int64_t value = strtoll(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_i64(nvs, key, value);
        }
    } else if (type == NVS_TYPE_U64) {
        uint64_t value = strtoull(str_value, NULL, 0);
        if (errno != ERANGE) {
            err = nvs_set_u64(nvs, key, value);
        }
    } else if (type == NVS_TYPE_STR) {
        err = nvs_set_str(nvs, key, str_value);
    } else if (type == NVS_TYPE_BLOB) {
        err = store_blob(nvs, key, str_value);
    }

    if (range_error || errno == ERANGE) {
        nvs_close(nvs);
        return ESP_ERR_NVS_VALUE_TOO_LONG;
    }

    if (err == ESP_OK) {
    	log_send_messaging(MESSAGING_INFO, "Set value ok. Committing '%s'", key);
        err = nvs_commit(nvs);
        if (err == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO, "Value stored under key '%s'", key);
        }
    }

    nvs_close(nvs);
    return err;
}

static esp_err_t get_value_from_nvs(const char *key, const char *str_type)
{
    nvs_handle nvs;
    esp_err_t err;

    nvs_type_t type = str_to_type(str_type);

    if (type == NVS_TYPE_ANY) {
        return ESP_ERR_NVS_TYPE_MISMATCH;
    }

    err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    if (type == NVS_TYPE_I8) {
        int8_t value;
        err = nvs_get_i8(nvs, key, &value);
        if (err == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %d \n", key, value);
        }
    } else if (type == NVS_TYPE_U8) {
        uint8_t value;
        err = nvs_get_u8(nvs, key, &value);
        if (err == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %u \n", key, value);
        }
    } else if (type == NVS_TYPE_I16) {
        int16_t value;
        err = nvs_get_i16(nvs, key, &value);
        if (err == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %d \n", key, value);
        }
    } else if (type == NVS_TYPE_U16) {
        uint16_t value;
        if ((err = nvs_get_u16(nvs, key, &value)) == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %u", key, value);
        }
    } else if (type == NVS_TYPE_I32) {
        int32_t value;
        if ((err = nvs_get_i32(nvs, key, &value)) == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %d \n", key, value);
        }
    } else if (type == NVS_TYPE_U32) {
        uint32_t value;
        if ((err = nvs_get_u32(nvs, key, &value)) == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %u \n", key, value);
        }
    } else if (type == NVS_TYPE_I64) {
        int64_t value;
        if ((err = nvs_get_i64(nvs, key, &value)) == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %lld \n", key, value);
        }
    } else if (type == NVS_TYPE_U64) {
        uint64_t value;
        if ( (err = nvs_get_u64(nvs, key, &value)) == ESP_OK) {
        	log_send_messaging(MESSAGING_INFO,"Value associated with key '%s' is %llu \n", key, value);
        }
    } else if (type == NVS_TYPE_STR) {
        size_t len=0;
        if ( (err = nvs_get_str(nvs, key, NULL, &len)) == ESP_OK) {
            char *str = (char *)malloc_init_external(len);
            if ( (err = nvs_get_str(nvs, key, str, &len)) == ESP_OK) {
            	log_send_messaging(MESSAGING_INFO,"String associated with key '%s' is %s \n", key, str);
            }
            free(str);
        }
    } else if (type == NVS_TYPE_BLOB) {
        size_t len;
        if ( (err = nvs_get_blob(nvs, key, NULL, &len)) == ESP_OK) {
            char *blob = (char *)malloc_init_external(len);
            if ( (err = nvs_get_blob(nvs, key, blob, &len)) == ESP_OK) {
            	log_send_messaging(MESSAGING_INFO,"Blob associated with key '%s' is %d bytes long: \n", key, len);
                print_blob(blob, len);
            }
            free(blob);
        }
    }

    nvs_close(nvs);
    return err;
}

static esp_err_t erase(const char *key)
{
    nvs_handle nvs;

    esp_err_t err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_key(nvs, key);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
            if (err == ESP_OK) {
            	log_send_messaging(MESSAGING_INFO, "Value with key '%s' erased", key);
            }
        }
        nvs_close(nvs);
    }

    return err;
}

static esp_err_t erase_all(const char *name)
{
    nvs_handle nvs;

    esp_err_t err = nvs_open_from_partition(settings_partition, current_namespace, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        err = nvs_erase_all(nvs);
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
    }

    log_send_messaging(MESSAGING_INFO, "Namespace '%s' was %s erased", name, (err == ESP_OK) ? "" : "not");
    nvs_close(nvs);
    return ESP_OK;
}

static int set_value(int argc, char **argv)
{
	ESP_LOGD(TAG, "%s %u - Parsing keys ",__func__,__LINE__);
    int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&set_args);
    if (nerrors != 0) {
        return 1;
    }

    const char *key = set_args.key->sval[0];
    const char *type = set_args.type->sval[0];
    const char *values = set_args.value->sval[0];
    cmd_send_messaging(argv[0],MESSAGING_INFO, "Setting '%s' (type %s)", key,type);
    esp_err_t err = set_value_in_nvs(key, type, values);

    if (err != ESP_OK) {
    	cmd_send_messaging(argv[0],MESSAGING_ERROR, "%s", esp_err_to_name(err));
        return 1;
    }

    return 0;

}

static int get_value(int argc, char **argv)
{
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&get_args);
    if (nerrors != 0) {
        return 1;
    }

    const char *key = get_args.key->sval[0];
    const char *type = get_args.type->sval[0];

    esp_err_t err = get_value_from_nvs(key, type);

    if (err != ESP_OK) {
    	cmd_send_messaging(argv[0],MESSAGING_ERROR, "%s", esp_err_to_name(err));
        return 1;
    }

    return 0;
}

static int erase_value(int argc, char **argv)
{
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&erase_args);
    if (nerrors != 0) {
        return 1;
    }

    const char *key = erase_args.key->sval[0];

    esp_err_t err = erase(key);

    if (err != ESP_OK) {
    	cmd_send_messaging(argv[0],MESSAGING_ERROR, "%s", esp_err_to_name(err));
        return 1;
    }

    return 0;
}

static int erase_namespace(int argc, char **argv)
{
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&erase_all_args);
    if (nerrors != 0) {
        return 1;
    }

    const char *name = erase_all_args.namespace->sval[0];

    esp_err_t err = erase_all(name);
    if (err != ESP_OK) {
    	cmd_send_messaging(argv[0],MESSAGING_ERROR, "%s", esp_err_to_name(err));
        return 1;
    }

    return 0;
}

static int erase_network_manager(int argc, char **argv)
{
    nvs_handle nvs;
	esp_err_t err = nvs_open("config", NVS_READWRITE, &nvs);
	if (err == ESP_OK) {
		err = nvs_erase_all(nvs);
		if (err == ESP_OK) {
			err = nvs_commit(nvs);
		}
	}
	nvs_close(nvs);
	if (err != ESP_OK) {
		cmd_send_messaging(argv[0],MESSAGING_ERROR,  "System configuration was not erased. %s", esp_err_to_name(err));
		return 1;
	}
	else {
		cmd_send_messaging(argv[0],MESSAGING_WARNING,  "system configuration was erased. Please reboot.");
	}
	return 0;
}

static int wifi_erase_config(int argc, char **argv)
{
    esp_err_t err=ESP_OK;
    esp_err_t err_ap_list=ESP_OK;
    bool done = false;
	int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&wifi_erase_args);
    if (nerrors != 0) {
        return 1;
    }
    if(wifi_erase_args.ap_list->count>0){
        err_ap_list = network_wifi_erase_known_ap();
        if (err_ap_list != ESP_OK) {
            cmd_send_messaging(argv[0],MESSAGING_ERROR, "Could not erase legacy wifi configuration: %s", esp_err_to_name(err));
        }
        else {
            cmd_send_messaging(argv[0],MESSAGING_ERROR, "Legacy wifi configuration was erased");
        }
        done = true;
    }
    if(wifi_erase_args.legacy->count>0){
        err = network_wifi_erase_legacy();
        if (err != ESP_OK) {
            cmd_send_messaging(argv[0],MESSAGING_ERROR, "Could not erase known ap list : %s", esp_err_to_name(err));
        }
        else {
            cmd_send_messaging(argv[0],MESSAGING_ERROR, "Known access point list was erased");
        }        
        done = true;
    }
    if(!done){
        cmd_send_messaging(argv[0],MESSAGING_WARNING, "Please specify at least one configuration type to erase.", esp_err_to_name(err));
    }
	return (err_ap_list==ESP_OK && err==ESP_OK)?0:1;
}

static int list(const char *part, const char *name, const char *str_type)
{
    nvs_type_t type = str_to_type(str_type);

    nvs_iterator_t it = nvs_entry_find(part, NULL, type);
    if (it == NULL) {
    	log_send_messaging(MESSAGING_ERROR, "No such enty was found");
        return 1;
    }

    do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        it = nvs_entry_next(it);

        log_send_messaging(MESSAGING_INFO, "namespace '%s', key '%s', type '%s' \n",
               info.namespace_name, info.key, type_to_str(info.type));
    } while (it != NULL);

    return 0;
}
static int list_entries(int argc, char **argv)
{
    list_args.partition->sval[0] = "";
    list_args.namespace->sval[0] = "";
    list_args.type->sval[0] = "";

    int nerrors = arg_parse_msg(argc, argv,(struct arg_hdr **)&list_args);
    if (nerrors != 0) {
        return 1;
    }

    const char *part = list_args.partition->sval[0];
    const char *name = list_args.namespace->sval[0];
    const char *type = list_args.type->sval[0];

    return list(part, name, type);
}
void register_nvs()
{
	set_args.key = arg_str1(NULL, NULL, "<key>", "key of the value to be set");
    set_args.type = arg_str1(NULL, NULL, "<type>", ARG_TYPE_STR);
    set_args.value = arg_str1("v", "value", "<value>", "value to be stored");
    set_args.end = arg_end(2);

    get_args.key = arg_str1(NULL, NULL, "<key>", "key of the value to be read");
    get_args.type = arg_str1(NULL, NULL, "<type>", ARG_TYPE_STR);
    get_args.end = arg_end(2);

    erase_args.key = arg_str1(NULL, NULL, "<key>", "key of the value to be erased");
    erase_args.end = arg_end(2);

    erase_all_args.namespace = arg_str1(NULL, NULL, "<namespace>", "namespace to be erased");
    erase_all_args.end = arg_end(2);

    wifi_erase_args.ap_list = arg_lit0("a","ap_list","Erases Known access points list");
    wifi_erase_args.legacy = arg_lit0("l","legacy","Erases legacy access point storage");
    wifi_erase_args.end = arg_end(1);

    list_args.partition = arg_str1(NULL, NULL, "<partition>", "partition name");
    list_args.namespace = arg_str0("n", "namespace", "<namespace>", "namespace name");
    list_args.type = arg_str0("t", "type", "<type>", ARG_TYPE_STR);
    list_args.end = arg_end(2);
    const esp_console_cmd_t set_cmd = {
        .command = "nvs_set",
        .help = "Set variable in selected namespace. Blob type must be comma separated list of hex values. \n"
        "Examples:\n"
        " nvs_set VarName i32 -v 123 \n"
        " nvs_set VarName srt -v YourString \n"
        " nvs_set VarName blob -v 0123456789abcdef \n",
        .hint = NULL,
        .func = &set_value,
        .argtable = &set_args
    };

    const esp_console_cmd_t get_cmd = {
        .command = "nvs_get",
        .help = "Get variable from selected namespace. \n"
        "Example: nvs_get VarName i32",
        .hint = NULL,
        .func = &get_value,
        .argtable = &get_args
    };

    const esp_console_cmd_t erase_cmd = {
        .command = "nvs_erase",
        .help = "Erase variable from current namespace",
        .hint = NULL,
        .func = &erase_value,
        .argtable = &erase_args
    };

    const esp_console_cmd_t erase_namespace_cmd = {
        .command = "nvs_erase_namespace",
        .help = "Erases specified namespace",
        .hint = NULL,
        .func = &erase_namespace,
        .argtable = &erase_all_args
    };
    const esp_console_cmd_t erase_config_cmd = {
        .command = "wifi_erase_config",
        .help = "Erases all stored access points from flash",
        .hint = NULL,
        .func = &wifi_erase_config,
        .argtable = &wifi_erase_args
    };

    const esp_console_cmd_t erase_networkmanager_cmd = {
        .command = "nvs_erase_configuration",
        .help = "Erases system's configuration",
        .hint = NULL,
        .func = &erase_network_manager,
        .argtable = NULL
    };


    const esp_console_cmd_t list_entries_cmd = {
           .command = "nvs_list",
           .help = "List stored key-value pairs stored in NVS."
           "Namespace and type can be specified to print only those key-value pairs.\n"
           "Following command list variables stored inside 'nvs' partition, under namespace 'storage' with type uint32_t"
           "Example: nvs_list nvs -n storage -t u32 \n",
           .hint = NULL,
           .func = &list_entries,
           .argtable = &list_args
       };
    MEMTRACE_PRINT_DELTA_MESSAGE("registering list_entries_cmd");
    ESP_ERROR_CHECK(esp_console_cmd_register(&list_entries_cmd));
    MEMTRACE_PRINT_DELTA_MESSAGE("registering set_cmd");
    ESP_ERROR_CHECK(esp_console_cmd_register(&set_cmd));
    MEMTRACE_PRINT_DELTA_MESSAGE("registering get_cmd");
    ESP_ERROR_CHECK(esp_console_cmd_register(&get_cmd));
    MEMTRACE_PRINT_DELTA_MESSAGE("registering erase_cmd");
    ESP_ERROR_CHECK(esp_console_cmd_register(&erase_cmd));
    MEMTRACE_PRINT_DELTA_MESSAGE("registering erase_namespace_cmd");
    ESP_ERROR_CHECK(esp_console_cmd_register(&erase_namespace_cmd));
    MEMTRACE_PRINT_DELTA_MESSAGE("registering erase_config_cmd");
    ESP_ERROR_CHECK(esp_console_cmd_register(&erase_networkmanager_cmd));
    MEMTRACE_PRINT_DELTA_MESSAGE("registering erase_config_cmd");
    ESP_ERROR_CHECK(esp_console_cmd_register(&erase_config_cmd));
    MEMTRACE_PRINT_DELTA_MESSAGE("Done");

}
#ifdef __cplusplus
extern }
#endif
