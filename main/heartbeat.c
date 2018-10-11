
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>

static void heartbeat_timer_callback(void* arg);

const static esp_timer_create_args_t heartbeat_timer_args = {
  .callback = heartbeat_timer_callback,
  .arg = 0,
  .dispatch_method = ESP_TIMER_TASK,
  .name = "heartbeat_timer" };
static esp_timer_handle_t heartbeat_timer;

void heartbeat_init(int interval_s) {
  esp_timer_create(&heartbeat_timer_args, &heartbeat_timer);
  esp_timer_start_periodic(heartbeat_timer, interval_s*1000000);
}

static void heartbeat_timer_callback(void* arg) {
  ESP_LOGI(__FUNCTION__, "### Heartbeat free_heap=%i", esp_get_free_heap_size());
}
