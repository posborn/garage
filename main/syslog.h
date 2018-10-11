
#pragma once

enum syslog_state {
  SYSLOG_NONE,        // not initialized
  SYSLOG_WAIT,        // waiting for Wifi
  //  SYSLOG_INIT,        // WIFI avail, must initialize
  //SYSLOG_INITDONE,
  //SYSLOG_DNSWAIT,     // WIFI avail, init done, waiting for DNS resolve
  SYSLOG_READY,       // Wifi established, ready to send
  SYSLOG_SENDING,     // UDP package on the air
  SYSLOG_SEND,
  SYSLOG_SENT,
  SYSLOG_HALTED,      // heap full, discard message
  SYSLOG_ERROR,
};


enum syslog_priority {
  SYSLOG_PRIO_EMERG,      /* system is unusable */
  SYSLOG_PRIO_ALERT,      /* action must be taken immediately */
  SYSLOG_PRIO_CRIT,       /* critical conditions */
  SYSLOG_PRIO_ERR,                /* error conditions */
  SYSLOG_PRIO_WARNING,    /* warning conditions */
  SYSLOG_PRIO_NOTICE,     /* normal but significant condition */
  SYSLOG_PRIO_INFO,       /* informational */
  SYSLOG_PRIO_DEBUG,      /* debug-level messages */
};

enum syslog_facility {
  SYSLOG_FAC_KERN,        /* kernel messages */
  SYSLOG_FAC_USER,        /* random user-level messages */
  SYSLOG_FAC_MAIL,        /* mail system */
  SYSLOG_FAC_DAEMON,      /* system daemons */
  SYSLOG_FAC_AUTH,        /* security/authorization messages */
  SYSLOG_FAC_SYSLOG,      /* messages generated internally by syslogd */
  SYSLOG_FAC_LPR,         /* line printer subsystem */
  SYSLOG_FAC_NEWS,        /* network news subsystem */
  SYSLOG_FAC_UUCP,        /* UUCP subsystem */
  SYSLOG_FAC_CRON,        /* clock daemon */
  SYSLOG_FAC_AUTHPRIV,/* security/authorization messages (private) */
  SYSLOG_FAC_FTP,         /* ftp daemon */
  SYSLOG_FAC_LOCAL0,      /* reserved for local use */
  SYSLOG_FAC_LOCAL1,      /* reserved for local use */
  SYSLOG_FAC_LOCAL2,      /* reserved for local use */
  SYSLOG_FAC_LOCAL3,      /* reserved for local use */
  SYSLOG_FAC_LOCAL4,      /* reserved for local use */
  SYSLOG_FAC_LOCAL5,      /* reserved for local use */
  SYSLOG_FAC_LOCAL6,      /* reserved for local use */
  SYSLOG_FAC_LOCAL7,      /* reserved for local use */
};


void syslog_init(void);
