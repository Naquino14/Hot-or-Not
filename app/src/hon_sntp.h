#ifndef HON_TIMESYNC
#define HON_TIMESYNC

#include <stdbool.h>

void hon_sntp_set_server_ipaddr(const char* ipaddr);

int hon_sntp_get_time();

void hon_sntp_cancel();

bool hon_sntp_ok();

#endif