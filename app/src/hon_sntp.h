#ifndef HON_TIMESYNC
#define HON_TIMESYNC

void hon_sntp_set_server_ipaddr(const char* ipaddr);

int hon_sntp_get_time();

#endif