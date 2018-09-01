#include <stdio.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include "wifi.h"
#include "garage_control.h"

#define ALLOW_REMOTE_OPEN
#define ALLOW_REMOTE_CLOSE

void on_wifi_ready();

esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
        case SYSTEM_EVENT_STA_START:
            printf("STA start\n");
            esp_wifi_connect();
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            printf("WiFI ready\n");
            on_wifi_ready();
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            printf("STA disconnected\n");
            esp_wifi_connect();
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void wifi_init() {
    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

const int GPIO_LED = 2;
const int GPIO_SENSOR_CLOSED = 0;
bool led_on = false;
//bool garage_open = false;
static garage_state_t garage_target = GARAGE_OPEN;
static garage_state_t garage_current = GARAGE_OPEN;
extern homekit_characteristic_t c_current_door_state;
extern homekit_characteristic_t c_target_door_state;

void led_write(bool on) {
    gpio_set_level(GPIO_LED, on ? 1 : 0);
}


void led_identify_task(void *_args) {
    for (int i=0; i<3; i++) {
        for (int j=0; j<2; j++) {
            led_write(true);
            vTaskDelay(100 / portTICK_PERIOD_MS);
            led_write(false);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        vTaskDelay(250 / portTICK_PERIOD_MS);
    }

    led_write(led_on);

    vTaskDelete(NULL);
}

void garage_identify(homekit_value_t _value) {
    printf("Garage identify\n");
    xTaskCreate(led_identify_task, "LED identify", 512, NULL, 2, NULL);
}

homekit_value_t garage_current_get(void) {
  return HOMEKIT_UINT8(garage_current);
}

/* This is called to provide state updates, and may repeat existing states, so filter */
void garage_state_callback(garage_state_t current_state, garage_state_t target_state) {
  if (garage_target != target_state) {
    garage_target = target_state;
    homekit_characteristic_notify(&c_target_door_state, HOMEKIT_UINT8(garage_target));
  }
  if (garage_current != current_state) {
    garage_current = current_state;
    homekit_characteristic_notify(&c_current_door_state, HOMEKIT_UINT8(garage_current));
  }
}


homekit_value_t garage_target_get(void) {
  return HOMEKIT_UINT8(garage_target);
}

void garage_target_set(homekit_value_t value) {
    if (value.format != homekit_format_uint8) {
        printf("Invalid value format: %d\n", value.format);
        return;
    }

    switch (value.int_value) {
    case GARAGE_OPEN:
      if (garage_target == GARAGE_OPEN) return;
#ifdef ALLOW_REMOTE_OPEN
      garage_action_open();
#else
    homekit_characteristic_notify(&c_target_door_state, HOMEKIT_UINT8(garage_target));
#endif
      break;
    case GARAGE_CLOSED:
      if (garage_target == GARAGE_CLOSED) return;
#ifdef ALLOW_REMOTE_CLOSE
      garage_action_close();
#else
    homekit_characteristic_notify(&c_target_door_state, HOMEKIT_UINT8(garage_target));
#endif
      break;
    default:
      printf("%s: Unknown value %i\n", __func__, value.int_value);
      break;
    }
    //    led_write(led_on);    
}


homekit_characteristic_t c_current_door_state =
		  HOMEKIT_CHARACTERISTIC_(CURRENT_DOOR_STATE, 0,
					 .getter=garage_current_get);

homekit_characteristic_t c_target_door_state =
		  HOMEKIT_CHARACTERISTIC_(TARGET_DOOR_STATE, 0,
					 .getter=garage_target_get,
					 .setter=garage_target_set);

homekit_accessory_t *accessories[] = {
    HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_lightbulb, .services=(homekit_service_t*[]){
        HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Garage Controller"),
            HOMEKIT_CHARACTERISTIC(MANUFACTURER, "PAO"),
            HOMEKIT_CHARACTERISTIC(SERIAL_NUMBER, "4820AF5839B2"),
            HOMEKIT_CHARACTERISTIC(MODEL, "Pro"),
            HOMEKIT_CHARACTERISTIC(FIRMWARE_REVISION, "0.2"),
            HOMEKIT_CHARACTERISTIC(IDENTIFY, garage_identify),
            NULL
        }),
	  /*        HOMEKIT_SERVICE(LIGHTBULB, .primary=true, .characteristics=(homekit_characteristic_t*[]){
            HOMEKIT_CHARACTERISTIC(NAME, "Sample LED"),
            HOMEKIT_CHARACTERISTIC(
                ON, false,
                .getter=led_on_get,
                .setter=led_on_set
            ),
            NULL
	    }),*/
	  HOMEKIT_SERVICE(GARAGE_DOOR_OPENER, .primary=true, .characteristics=(homekit_characteristic_t*[]){
	        HOMEKIT_CHARACTERISTIC(NAME, "Garage"),
		  &c_current_door_state,
		  &c_target_door_state,
		  HOMEKIT_CHARACTERISTIC(OBSTRUCTION_DETECTED, false),
		  NULL,
		  }),
        NULL
    }),
    NULL
};

homekit_server_config_t config = {
    .accessories = accessories,
    .password = "222-22-222"
};

void on_wifi_ready() {
    homekit_server_init(&config);
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    //    printf("Calling wifi init\n");
    wifi_init();
    printf("Calling garage init\n");
    garage_init();
    garage_set_state_callback(garage_state_callback);
}
