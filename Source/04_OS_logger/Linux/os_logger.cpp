#include <mysql.h>
#include <mysql/plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/sysinfo.h>

static pthread_t logger_thread;
static volatile int stop_logger = 0;
static int thread_running = 0;

static void *os_logger_worker(void *p) {
    struct sysinfo info;
    while (!stop_logger) {
        if (sysinfo(&info) == 0) {
            double load = info.loads[0] / 65536.0;
            unsigned long total_ram = (info.totalram * info.mem_unit) / 1024 / 1024;
            unsigned long free_ram  = (info.freeram  * info.mem_unit) / 1024 / 1024;
            fprintf(stderr, "[os_logger] CPU Load: %.2f, RAM: %lu/%lu MB\n",
                    load, free_ram, total_ram);
        }
        for (int i = 0; i < 5 && !stop_logger; i++) sleep(1);
    }
    return NULL;
}

static int os_logger_init(void *p) {
    stop_logger    = 0;
    thread_running = 0;

    int ret = pthread_create(&logger_thread, NULL, os_logger_worker, NULL);
    if (ret != 0) {
        fprintf(stderr, "[os_logger] pthread_create failed: %d\n", ret);
        return 1;
    }
    thread_running = 1;
    return 0;
}

static int os_logger_deinit(void *p) {
    stop_logger = 1;
    if (thread_running) {
        pthread_join(logger_thread, NULL);
        thread_running = 0;
    }
    return 0;
}

extern "C" {

struct st_mysql_daemon os_logger_info = { MYSQL_DAEMON_INTERFACE_VERSION };

maria_declare_plugin(os_logger)
{
    MYSQL_DAEMON_PLUGIN,
    &os_logger_info,
    "os_logger",
    "youly",
    "OS Resource Logger",
    PLUGIN_LICENSE_GPL,
    os_logger_init,
    os_logger_deinit,
    0x0100,
    NULL,
    NULL,
    "1.0",
    MariaDB_PLUGIN_MATURITY_STABLE
}
maria_declare_plugin_end;

} // extern "C"
