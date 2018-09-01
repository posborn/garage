
void garage_init(void);

/* Homekit compatible enumeration */
typedef enum {
  GARAGE_OPEN=0,
  GARAGE_CLOSED=1,
  GARAGE_OPENING=2,
  GARAGE_CLOSING=3,
  GARAGE_STOPPED=4
} garage_state_t;

//#define TO_HOMEKIT(x) (x==GARAGE_STUCK ? GARAGE_OPEN : x)
#define TO_HOMEKIT(x) (x)

typedef void (*garage_state_callback_t)(garage_state_t current_state, garage_state_t target_state);

void garage_set_state_callback(garage_state_callback_t fn);
garage_state_t garage_get_current_state(void);
void garage_action_open(void);
void garage_action_close(void);
