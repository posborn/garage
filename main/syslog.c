#include <stdio.h>
#include <time.h>
#include <esp_wifi.h>
#include <esp_event_loop.h>
#include <esp_log.h>
#include <lwip/sockets.h>
#include <lwip/mem.h>
#include "syslog.h"

#define RECEIVER_IP_ADDR "192.168.1.2"
#define RECEIVER_PORT_NUM 514
#define SENDER_PORT_NUM 5454
#define SYSLOG_INTERVAL_MS 100
static char my_ip[32];
static int syslog_socket;
static struct sockaddr_in sa,ra;
static uint32_t syslog_msgid = 1;
static const char *my_hostname = "ESP32";

typedef struct syslog_entry_t syslog_entry_t;
struct syslog_entry_t {
  syslog_entry_t *next;
  uint32_t msgid;
  uint32_t tick;
  uint16_t datagram_len;
  char datagram[];
};
static syslog_entry_t *syslogQueue = NULL;

//#define SYSLOG_DBG
#ifdef SYSLOG_DBG
#define DBG(format, ...) do { printf(format, ## __VA_ARGS__); } while(0)
#else
#define DBG(format, ...) do { } while(0) 
#endif
#define MIN_HEAP_SIZE 10000

static enum syslog_state syslogState = SYSLOG_NONE;
static void syslog_schedule_send(void);

#ifdef SYSLOG_DBG
static char  *syslog_get_status(void) {
  switch (syslogState)
    {
    case SYSLOG_NONE:
      return "SYSLOG_NONE";
    case SYSLOG_WAIT:
      return "SYSLOG_WAIT";
      //    case SYSLOG_INIT:
      //return "SYSLOG_INIT";
      //case SYSLOG_INITDONE:
      //return "SYSLOG_INITDONE";
      //case SYSLOG_DNSWAIT:
      //return "SYSLOG_DNSWAIT";
    case SYSLOG_READY:
      return "SYSLOG_READY";
    case SYSLOG_SENDING:
      return "SYSLOG_SENDING";
    case SYSLOG_SEND:
      return "SYSLOG_SEND";
    case SYSLOG_SENT:
      return "SYSLOG_SENT";
    case SYSLOG_HALTED:
      return "SYSLOG_HALTED";
    case SYSLOG_ERROR:
      return "SYSLOG_ERROR";
    default:
      break;
    }
  return "UNKNOWN ";
}
#endif

static void syslog_set_status(enum syslog_state state) {
  syslogState = state;
  DBG("[%dµs] %s: %s (%d)\n", esp_log_timestamp(), __FUNCTION__, syslog_get_status(), state);
}

static void syslog_timer_callback(void* arg);
const static esp_timer_create_args_t syslog_timer_args = {
  .callback = syslog_timer_callback,
  .arg = 0,
  .dispatch_method = ESP_TIMER_TASK,
  .name = "syslog_timer" };
static esp_timer_handle_t syslog_timer;

static syslog_entry_t *syslog_compose_internal(uint8_t facility, uint8_t severity,
					       const char *tag, const char *fmt,
					       va_list argptr
					       ) {

#define MALLOC_SIZE (sizeof(syslog_entry_t) + 1024)
  char fmt_tmp[256];
  DBG("[%dµs] %s id=%u\n", esp_log_timestamp(), __FUNCTION__, syslog_msgid);
  syslog_entry_t *se = mem_malloc(MALLOC_SIZE); // allow up to 1k datagram
  char *p = se->datagram;
  memset(se, 0, sizeof(MALLOC_SIZE));
  se->tick = esp_log_timestamp();
  se->msgid = syslog_msgid;

  {
    int i,j;
    bool in_escape_code=false;
    for (i=0,j=0; fmt[i]!=0; i++) {
      if (in_escape_code) {
	if (fmt[i]=='m') in_escape_code = false;
      }else{
	if (fmt[i]==0x1B) {
	  in_escape_code = true;
	  continue;
	}
	fmt_tmp[j] = fmt[i];
	j++;
      }
    }
    fmt_tmp[j] = 0;
  }

  // The Priority value is calculated by first multiplying the Facility
  // number by 8 and then adding the numerical value of the Severity.
  p += sprintf(p, "<%d>1 ", facility * 8 + severity);

  {
    //struct tm *timeinfo;
    //time_t now = esp_log_timestamp()/1000;
    //timeinfo=gmtime(&now);
    //p += strftime(p, 100, "%FT%TZ ", timeinfo);
    p += sprintf(p, "- ");
  }

  // add HOSTNAME APP-NAME PROCID MSGID
  p += sprintf(p, "%s %s - %u ", my_hostname, tag, syslog_msgid++);

  // append syslog message
  p += vsprintf(p, fmt_tmp, argptr );

  se->datagram_len = p - se->datagram;
  se = mem_trim(se, sizeof(syslog_entry_t) + se->datagram_len + 1);
  return se;
}

void syslog_timer_callback(void *arg) {
  int sent_data;
  syslog_entry_t *pse = syslogQueue;
  if (syslogQueue == NULL) return;
  DBG("[%dµs] %s id=%u\n", esp_log_timestamp(), __FUNCTION__, pse->msgid);
  if (syslogState != SYSLOG_READY && syslogState != SYSLOG_HALTED) return;
  syslogQueue = syslogQueue->next;

  DBG("Sending UDP\n");
  sent_data = sendto(syslog_socket, pse->datagram, pse->datagram_len,
		     0,(struct sockaddr*)&ra,sizeof(ra));
  mem_free(pse);
  syslog_set_status(SYSLOG_READY);
  if(sent_data < 0)
    {
      printf("%s: send failed\n", __FUNCTION__);
      close(syslog_socket);
      syslog_set_status(SYSLOG_WAIT);
      return;
    }

  if (syslogQueue != NULL) syslog_schedule_send();
  //vTaskDelay(4000 / portTICK_PERIOD_MS);

}

/******************************************************************************
 * FunctionName : syslog_compose
 * Description  : compose a syslog_entry_t from va_args
 * Parameters   : va_args
 * Returns      : the malloced syslog_entry_t
 ******************************************************************************/
static syslog_entry_t *syslog_compose(uint8_t facility, uint8_t severity,
				      const char *tag, const char *fmt, ...
				      ) {
  syslog_entry_t *msg;
  va_list argptr;
  va_start(argptr,fmt);
  msg = syslog_compose_internal(facility, severity,
				tag, fmt,
				argptr);
  va_end(argptr);
  return msg;
}

/******************************************************************************
 * FunctionName : syslog_add_entry
 * Description  : add a syslog_entry_t to the syslogQueue
 * Parameters   : entry: the syslog_entry_t
 * Returns      : none
 ******************************************************************************/
static void syslog_add_entry(syslog_entry_t *entry) {
  syslog_entry_t *pse = syslogQueue;
  if (entry==NULL) {
    return;
  }

  DBG("[%dµs] %s id=%u\n", esp_log_timestamp(), __FUNCTION__, entry->msgid);
  // append msg to syslog_queue
  if (pse == NULL)
    syslogQueue = entry;
  else {
    while (pse->next != NULL)
      pse = pse->next;
    pse->next = entry;  // append msg to syslog queue
  }
  DBG("%p %u %d\n", entry, entry->msgid, heap_caps_get_free_size(MALLOC_CAP_8BIT));

  // ensure we have sufficient heap for the rest of the system
  if (heap_caps_get_free_size(MALLOC_CAP_8BIT) < MIN_HEAP_SIZE) {
    if (syslogState != SYSLOG_HALTED) {
      printf("%s: Warning: queue filled up, halted\n", __FUNCTION__);
      entry->next = syslog_compose(SYSLOG_FAC_USER, SYSLOG_PRIO_CRIT, "SYSLOG", "queue filled up, halted");
      syslog_set_status(SYSLOG_HALTED);
    }
  }
  syslog_schedule_send();
}


static void syslog_schedule_send(void) {
  if (syslogState != SYSLOG_READY) return;
  DBG("Scheduling a send\n");
  esp_timer_start_once(syslog_timer, SYSLOG_INTERVAL_MS*1000);
}

static void open_syslog_socket(void){

  if (syslogState != SYSLOG_WAIT) return;

  syslog_socket = socket(PF_INET, SOCK_DGRAM, 0);

  if ( syslog_socket < 0 )
    {
      printf("socket call failed");
      return;
    }

  memset(&sa, 0, sizeof(struct sockaddr_in));
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = inet_addr(my_ip);
  sa.sin_port = htons(SENDER_PORT_NUM);


  if (bind(syslog_socket, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) == -1)
    {
      printf("Bind to Port Number %d ,IP address %s failed\n",SENDER_PORT_NUM,my_ip /*SENDER_IP_ADDR*/);
      close(syslog_socket);
      return;
    }
  printf("Bind to Port Number %d ,IP address %s SUCCESS!!!\n",SENDER_PORT_NUM,my_ip);

  memset(&ra, 0, sizeof(struct sockaddr_in));
  ra.sin_family = AF_INET;
  ra.sin_addr.s_addr = inet_addr(RECEIVER_IP_ADDR);
  ra.sin_port = htons(RECEIVER_PORT_NUM);

  ESP_ERROR_CHECK(tcpip_adapter_get_hostname(TCPIP_ADAPTER_IF_STA, &my_hostname));

  syslog_set_status(SYSLOG_READY);
  if (syslogQueue) syslog_schedule_send();
}

static void close_syslog_socket(void) {
  ESP_LOGW(__FUNCTION__, "Taking syslog offline.");
  close(syslog_socket);
  syslog_set_status(SYSLOG_WAIT);
}

/* Event handler to monitor for wifi connection/disconnection */
static system_event_cb_t old_event_handler = NULL;
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
  /* chain to the original handler, do this first in case time critical */
  old_event_handler(ctx, event);

  /* analyse the event, do we enable/disable the service */
  switch (event->event_id) {
  case SYSTEM_EVENT_STA_GOT_IP:
    sprintf(my_ip,IPSTR, IP2STR(&event->event_info.got_ip.ip_info.ip));
    open_syslog_socket();
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    close_syslog_socket();
    break;
  default:
    break;
  }
  return ESP_OK;
}

static vprintf_like_t old_log_vprintf;

int syslog_vprintf(const char *msg, va_list arglist) {
  syslog_entry_t *syslog_msg;
  char task_name[16];
  char *cur_task = pcTaskGetTaskName(xTaskGetCurrentTaskHandle());
  strncpy(task_name, cur_task, 16);
  task_name[15] = 0;
  if (strncmp(task_name, "tiT", 16) != 0) {
    // have confirmed we are not in the TCPIP task
    //syslog_msg = syslog_compose(SYSLOG_FAC_USER, SYSLOG_PRIO_INFO,
    //			      "TAG", msg, arglist);
    syslog_msg = syslog_compose_internal(SYSLOG_FAC_USER, SYSLOG_PRIO_INFO,
    					 "TAG", msg, arglist);
    syslog_add_entry(syslog_msg);
  }
  return old_log_vprintf(msg, arglist);
}

void syslog_init(void) {
  if (syslogState != SYSLOG_NONE) return;
  esp_timer_create(&syslog_timer_args, &syslog_timer);
  old_event_handler = esp_event_loop_set_cb(event_handler, NULL);
  syslog_set_status(SYSLOG_WAIT);

  old_log_vprintf = esp_log_set_vprintf(syslog_vprintf);
  ESP_LOGI(__FUNCTION__, "************** SYSLOG STARTING *************");
}
