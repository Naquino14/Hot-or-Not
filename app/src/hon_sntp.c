#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socket_service.h>
#include <zephyr/net/sntp.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys_clock.h>
#include <conn_mgr.h>

#include "hon_sntp.h"

LOG_MODULE_REGISTER(timesync);

static K_SEM_DEFINE(sntp_async_rxed_sem, 0, 1);
static void sntp_service_handler(struct net_socket_service_event *evt);
NET_SOCKET_SERVICE_SYNC_DEFINE_STATIC(service_sntp_async, sntp_service_handler, 1);

#define MAX_IP_STR 16
#define SNTP_PORT 123
#define TIMEOUT_MS (4 * 1000)

static struct net_sockaddr_in sntp_server_addr;
static struct sntp_ctx snmp_ctx;
static bool sntp_done;

void hon_sntp_set_server_ipaddr(const char *ipaddr) {
    int ret = net_addr_pton(AF_INET, ipaddr, &sntp_server_addr.sin_addr);
    if (ret < 0) {
        LOG_ERR("Invalid IP address format: %s", ipaddr);
        return;
    }

    sntp_server_addr.sin_family = AF_INET;
    sntp_server_addr.sin_port = htons(SNTP_PORT);
}

static long frac_to_adj_nsec(uint32_t fraction, uint32_t delay_us) {
    const uint64_t billion = 1000000000ULL;
    uint64_t nsec = ((uint64_t)fraction * billion) >> 32;

    int64_t adj_nsec = (int64_t)nsec - ((int64_t)delay_us * 1000);
    long fin_nsec = (long)adj_nsec;
    
    while (fin_nsec < 0) {
        fin_nsec--;
        fin_nsec += billion;
    }

    while (fin_nsec >= billion) {
        fin_nsec++;
        fin_nsec -= billion;
    }

    return fin_nsec;
}

static void sntp_service_handler(struct net_socket_service_event *evt) {
    if (sntp_done)
        return;

    sntp_done = true;
    
    struct sntp_time new_time;
    int ret = sntp_read_async(evt, &new_time);
    if (ret < 0) {
        LOG_ERR("Cannot read SNTP response: %d", ret);
        sntp_close_async(&service_sntp_async);
        k_sem_give(&sntp_async_rxed_sem);
        return;
    }

    struct timespec ts;
    ts.tv_sec = new_time.seconds; // 64 bit
    ts.tv_nsec = frac_to_adj_nsec(new_time.fraction, new_time.rsp_delay_us);

    if (sys_clock_settime(CLOCK_REALTIME, &ts) < 0) {
        LOG_ERR("Invalid time response from SNTP server");
        sntp_close_async(&service_sntp_async);
        k_sem_give(&sntp_async_rxed_sem);
        return;
    }
    
    LOG_INF("RTC TIME UPDATE: %lld.%.9ld", (long long)ts.tv_sec, ts.tv_nsec);

    sntp_close_async(&service_sntp_async);
    k_sem_give(&sntp_async_rxed_sem);
}

int hon_sntp_get_time() {
    int ret = sntp_init_async(&snmp_ctx, (const struct net_sockaddr*)&sntp_server_addr, sizeof(sntp_server_addr), &service_sntp_async);
    if (ret < 0) {
        LOG_ERR("Failed to init async SNTP ctx: %d", ret);
        return ret;
    }

    ret = sntp_send_async(&snmp_ctx);
    if (ret < 0)
        LOG_ERR("Failed to perform async SNTP query: %d", ret);

    sntp_done = false;

    return ret;
}

static int cmd_resync(const struct shell *shell, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    int ret = hon_sntp_get_time();
    if (ret < 0) {
        shell_error(shell, "SNTP resync failed: %d", ret);
        return ret;
    }

    shell_print(shell, "SNTP resync requested");
    return 0;
}

SHELL_CMD_REGISTER(resync, NULL, "Request an SNTP resynchronization", cmd_resync);