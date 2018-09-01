#include <stdio.h>
#include <esp_event_loop.h>
#include <esp_log.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "garage_config.h"
#include "garage_control.h"

#define STATE_STR(x) (x==GARAGE_OPEN ? "OPEN" : \
		      x==GARAGE_CLOSED ? "CLOSED" : \
		      x==GARAGE_OPENING ? "OPENING" : \
		      x==GARAGE_CLOSING ? "CLOSING" : \
		      x==GARAGE_STOPPED ? "STOPPED" : "UNKNOWN")


static void set_target_state(garage_state_t state);
static void set_current_state(garage_state_t state);

static void control_pin_timer_callback(void* arg);
static void sensor_pin_timer_callback(void* arg);
static void stuck_timer_callback(void* arg);


static garage_state_t last_good_state = 0;
static garage_state_t current_state = GARAGE_OPEN;
static garage_state_callback_t garage_state_callback = NULL;

const static esp_timer_create_args_t control_pin_timer_args = {
  .callback = control_pin_timer_callback,
  .arg = 0,
  .dispatch_method =  ESP_TIMER_TASK,
  .name = "control_pin_timer" };
static esp_timer_handle_t control_pin_timer;

const static esp_timer_create_args_t sensor_pin_timer_args = {
  .callback = sensor_pin_timer_callback,
  .arg = 0,
  .dispatch_method =  ESP_TIMER_TASK,
  .name = "sensor_pin_timer" };
static esp_timer_handle_t sensor_pin_timer;

const static esp_timer_create_args_t stuck_timer_args = {
  .callback = stuck_timer_callback,
  .arg = 0,
  .dispatch_method =  ESP_TIMER_TASK,
  .name = "stuck_timer" };
static esp_timer_handle_t stuck_timer;

#define GPIO_SEL_(x) ((uint64_t)(((uint64_t)1)<<x))
void garage_init(void) {
  gpio_config_t control_pins = {
    .pin_bit_mask = GPIO_SEL_(GARAGE_OPEN_CONTROL_PIN) | GPIO_SEL_(GARAGE_CLOSE_CONTROL_PIN),
    .mode = GPIO_MODE_OUTPUT_OD,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE,
  };

  gpio_set_direction(GARAGE_OPEN_SENSOR_PIN, GPIO_MODE_INPUT);
  gpio_pullup_en(GARAGE_OPEN_SENSOR_PIN);

  gpio_set_direction(GARAGE_CLOSED_SENSOR_PIN, GPIO_MODE_INPUT);
  gpio_pullup_en(GARAGE_CLOSED_SENSOR_PIN);

  gpio_config(&control_pins);
  gpio_set_level(GARAGE_OPEN_CONTROL_PIN, 1);
  gpio_set_level(GARAGE_CLOSE_CONTROL_PIN, 1);

  //gpio_set_direction(GARAGE_OPEN_CONTROL_PIN, GPIO_MODE_INPUT); // open drain
  //gpio_pullup_en(GARAGE_OPEN_CONTROL_PIN);

  //gpio_set_direction(GARAGE_CLOSE_CONTROL_PIN, GPIO_MODE_INPUT); // open drain
  //gpio_pullup_en(GARAGE_CLOSE_CONTROL_PIN);

  gpio_set_level(GARAGE_STATUS_LED_PIN, 0);
  gpio_set_direction(GARAGE_STATUS_LED_PIN, GPIO_MODE_OUTPUT);

  esp_timer_create(&control_pin_timer_args, &control_pin_timer);
  esp_timer_create(&sensor_pin_timer_args, &sensor_pin_timer);
  esp_timer_create(&stuck_timer_args, &stuck_timer);
  esp_timer_start_periodic(sensor_pin_timer, GARAGE_SENSOR_POLL_MILLISECONDS);
  printf("Completed garage_init\n");
}

void garage_set_state_callback(garage_state_callback_t fn) {
  garage_state_callback = fn;
}

garage_state_t garage_get_current_state(void) {
  return TO_HOMEKIT(current_state);
}


static bool garage_is_open(void) {
  return gpio_get_level(GARAGE_OPEN_SENSOR_PIN) == 0 ? true : false;
}
static bool garage_is_closed(void) {
  return gpio_get_level(GARAGE_CLOSED_SENSOR_PIN) == 0 ? true : false;
}

void garage_action_open(void) {
  printf("*** OPENING GARAGE\n");
  gpio_set_direction(GARAGE_OPEN_CONTROL_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(GARAGE_OPEN_CONTROL_PIN, 0);
  esp_timer_stop(control_pin_timer);
  esp_timer_start_once(control_pin_timer, GARAGE_CONTROL_PULSE_MILLISECONDS * 1000);
  set_target_state(GARAGE_OPEN);
}

void garage_action_close(void) {
  printf("*** CLOSING GARAGE\n");
  gpio_set_direction(GARAGE_CLOSE_CONTROL_PIN, GPIO_MODE_OUTPUT);
  gpio_set_level(GARAGE_CLOSE_CONTROL_PIN, 0);
  esp_timer_stop(control_pin_timer);
  esp_timer_start_once(control_pin_timer, GARAGE_CONTROL_PULSE_MILLISECONDS * 1000);
  set_target_state(GARAGE_CLOSED);
}

static void control_pin_timer_callback(void* arg) {
  //  gpio_set_level(GARAGE_OPEN_CONTROL_PIN, 0);
  //  gpio_set_level(GARAGE_CLOSE_CONTROL_PIN, 0);
  gpio_set_direction(GARAGE_OPEN_CONTROL_PIN, GPIO_MODE_INPUT);
  gpio_set_direction(GARAGE_CLOSE_CONTROL_PIN, GPIO_MODE_INPUT);
}

/*******************************************************************
 * Stuck door monitor
 */
static bool door_is_stuck = false;

static void start_stuck_timer(void) {
  door_is_stuck = false;
  esp_timer_stop(stuck_timer);
  esp_timer_start_once(stuck_timer, GARAGE_MAX_TRANSIT_SECONDS * 1000000);
}
static void stop_stuck_timer(void) {
  esp_timer_stop(stuck_timer);
  door_is_stuck = false;
}
static void stuck_timer_callback(void* arg) {
  door_is_stuck = true;
  printf("*** DOOR MAY BE STUCK\n");
  set_current_state(GARAGE_STOPPED);
}

/******************************************************************
 * 
 */
static garage_state_t target_state = GARAGE_OPEN;
static void set_target_state(garage_state_t state) {
  target_state = state;
}
static void set_current_state(garage_state_t state) {
  if (state == current_state) return;
  if (current_state == GARAGE_OPEN || current_state == GARAGE_CLOSED) start_stuck_timer();
  current_state = state;
  switch (state) {
  case GARAGE_OPEN:
  case GARAGE_CLOSED:
  case GARAGE_STOPPED:
    last_good_state = state;
    target_state = (state == GARAGE_CLOSED ? GARAGE_CLOSED : GARAGE_OPEN);
    stop_stuck_timer();
    break;
  case GARAGE_OPENING:
    target_state = GARAGE_OPEN;
    break;
  case GARAGE_CLOSING:
    target_state = GARAGE_CLOSED;
    break;
  default:
    break;
  }
  printf("NEW STATE: %s => %s\n", STATE_STR(current_state), STATE_STR(target_state));
  if (garage_state_callback) garage_state_callback(current_state, target_state);
}
static bool door_was_open(void) {
  if (last_good_state == GARAGE_OPEN) return true;
  return false;
}
static bool door_was_closed(void) {
  if (last_good_state == GARAGE_CLOSED) return true;
  return false;
}

/******************************************************************
 * 
 */

static void sensor_pin_timer_callback(void* arg) {
#define MAX_COUNT 3
  static int open_count=0;
  static int closed_count=0;
  static int unknown_count=0;

  if (garage_is_open()) {
    closed_count=0;
    unknown_count=0;
    if (open_count < MAX_COUNT) open_count++;
  }else{
    open_count=0;
    if (garage_is_closed()) {
      open_count=0;
      unknown_count=0;
      if (closed_count < MAX_COUNT) closed_count++;
    }else{
      closed_count = 0;
      if (unknown_count < MAX_COUNT) unknown_count++;
    }
  }

  if (open_count >= MAX_COUNT) {
    set_current_state(GARAGE_OPEN);
    return;
  }else if (closed_count >= MAX_COUNT) {
    set_current_state(GARAGE_CLOSED);
    return;
  }else if (unknown_count >= MAX_COUNT) {
    if (door_was_open()) {
      set_current_state(GARAGE_CLOSING);
    }else if (door_was_closed()) {
      set_current_state(GARAGE_OPENING);
    }
  }
}
