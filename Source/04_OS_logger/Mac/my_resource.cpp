/**
 * my_resource.cpp
 * MySQL 8.0 Daemon Plugin — System & DB Resource Monitor
 *
 * 수집 항목 (5초 간격, MySQL error log에 기록):
 *   - CPU 사용률(%)          : macOS Mach host_processor_info
 *   - 메모리 사용량(MB)       : macOS vm_statistics64 / sysctl
 *   - 디스크 I/O (KB/s)      : IOKit IOBlockStorageDriver
 *   - DB 연결 수              : SHOW GLOBAL STATUS 'Threads_connected'
 *   - 초당 쿼리 수            : SHOW GLOBAL STATUS 'Questions' 델타
 *
 * 빌드: Makefile 참조  (mysql_config=/usr/local/mysql/bin/mysql_config)
 * 설치: INSTALL PLUGIN my_resource SONAME 'my_resource.so';
 */

#include <mysql/plugin.h>   /* MYSQL_DAEMON_PLUGIN, my_plugin_log_message … */
#include <mysql.h>          /* mysql_init, mysql_real_connect … (libmysqlclient) */

/* macOS system APIs */
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/processor_info.h>
#include <mach/vm_statistics.h>
#include <sys/sysctl.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

/* C++ standard */
#include <pthread.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

/* ─────────────────────────────────────────────────────────────
 * 로깅 — my_plugin_log_service 서비스 포인터 매크로 우회
 *
 * MYSQL_DYNAMIC_PLUGIN 정의 시 service_my_plugin_log.h 가
 *   #define my_plugin_log_message  my_plugin_log_service->my_plugin_log_message
 * 로 확장한다. 그런데 macOS mysqld 8.0 은 my_plugin_log_service 전역 포인터를
 * flat namespace 로 export 하지 않아 dlopen 실패(symbol not found)가 발생한다.
 *
 * mysqld 는 C++ 함수 my_plugin_log_message 를 직접 export 한다:
 *   __Z21my_plugin_log_messagePPv16plugin_log_levelPKcz
 * 매크로를 #undef 하고 C++ 전방 선언으로 해당 심볼을 직접 참조한다.
 * ───────────────────────────────────────────────────────────── */
#ifdef my_plugin_log_message
#  undef my_plugin_log_message
#endif

int my_plugin_log_message(MYSQL_PLUGIN *plugin, enum plugin_log_level level,
                          const char *format, ...)
    MY_ATTRIBUTE((format(printf, 3, 4)));

/* ─────────────────────────────────────────────────────────────
 * IOKit 디스크 통계 키 (IOBlockStorageDriver.h 없이 직접 정의)
 * ───────────────────────────────────────────────────────────── */
#ifndef kIOBlockStorageDriverStatisticsKey
#  define kIOBlockStorageDriverStatisticsKey             "Statistics"
#endif
#ifndef kIOBlockStorageDriverStatisticsBytesReadKey
#  define kIOBlockStorageDriverStatisticsBytesReadKey    "Bytes (Read)"
#endif
#ifndef kIOBlockStorageDriverStatisticsBytesWrittenKey
#  define kIOBlockStorageDriverStatisticsBytesWrittenKey "Bytes (Write)"
#endif

/* ─────────────────────────────────────────────────────────────
 * 플러그인 메타
 * ───────────────────────────────────────────────────────────── */
#define PLUGIN_NAME     "my_resource"
#define MY_RESOURCE_VER  0x0100   /* 1.0 */

/* ─────────────────────────────────────────────────────────────
 * 전역 상태
 * ───────────────────────────────────────────────────────────── */
static MYSQL_PLUGIN          g_plugin_info  = nullptr;
static std::atomic<bool>     g_running{false};
static pthread_t             g_monitor_tid;

/* 현재 수집된 지표 (SHOW STATUS 콜백에서 읽힘) */
static double    g_cpu_pct         = 0.0;
static long long g_mem_used_mb     = 0;
static long long g_mem_total_mb    = 0;
static long long g_disk_read_kbps  = 0;
static long long g_disk_write_kbps = 0;
static long long g_db_connections  = 0;
static long long g_db_queries_ps   = 0;

/* 시스템 변수 (my.cnf [mysqld] 또는 SET GLOBAL 으로 설정 가능) */
static int   g_interval_sec  = 5;
static char *g_mon_socket    = nullptr;
static char *g_mon_user      = nullptr;
static char *g_mon_password  = nullptr;

/* ═══════════════════════════════════════════════════════════════
 * 1. CPU 사용률 — macOS Mach processor_info
 * ═══════════════════════════════════════════════════════════════ */
struct CpuTick { uint64_t user, sys, idle, nice; };

static bool cpu_ticks_get(CpuTick &out) {
    natural_t              cpu_cnt  = 0;
    processor_info_array_t cpu_info = nullptr;
    mach_msg_type_number_t info_cnt = 0;

    kern_return_t kr = host_processor_info(
        mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
        &cpu_cnt, &cpu_info, &info_cnt);
    if (kr != KERN_SUCCESS) return false;

    out = {0, 0, 0, 0};
    for (natural_t i = 0; i < cpu_cnt; ++i) {
        integer_t *p = cpu_info + (CPU_STATE_MAX * i);
        out.user += (uint64_t)p[CPU_STATE_USER];
        out.sys  += (uint64_t)p[CPU_STATE_SYSTEM];
        out.idle += (uint64_t)p[CPU_STATE_IDLE];
        out.nice += (uint64_t)p[CPU_STATE_NICE];
    }
    vm_deallocate(mach_task_self(),
                  (vm_address_t)cpu_info,
                  info_cnt * sizeof(integer_t));
    return true;
}

/**
 * 1초 간격으로 두 번 샘플링하여 CPU 사용률(%) 반환.
 * 모니터 스레드가 5초 주기 중 첫 1초를 여기서 소비한다.
 */
static double cpu_usage_measure() {
    CpuTick t1, t2;
    if (!cpu_ticks_get(t1)) return 0.0;
    sleep(1);
    if (!cpu_ticks_get(t2)) return 0.0;

    uint64_t du    = t2.user - t1.user;
    uint64_t ds    = t2.sys  - t1.sys;
    uint64_t di    = t2.idle - t1.idle;
    uint64_t dn    = t2.nice - t1.nice;
    uint64_t total = du + ds + di + dn;
    return (total == 0) ? 0.0
                        : 100.0 * (double)(du + ds + dn) / (double)total;
}

/* ═══════════════════════════════════════════════════════════════
 * 2. 메모리 — sysctl hw.memsize + vm_statistics64
 * ═══════════════════════════════════════════════════════════════ */
static bool mem_usage_get(long long &used_mb, long long &total_mb) {
    int64_t total_bytes = 0;
    size_t  len         = sizeof(total_bytes);
    if (sysctlbyname("hw.memsize", &total_bytes, &len, nullptr, 0) != 0)
        return false;
    total_mb = total_bytes / (1024LL * 1024);

    vm_size_t page_size = 0;
    host_page_size(mach_host_self(), &page_size);

    vm_statistics64_data_t vm_stat;
    mach_msg_type_number_t cnt = HOST_VM_INFO64_COUNT;
    kern_return_t kr = host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                         (host_info64_t)&vm_stat, &cnt);
    if (kr != KERN_SUCCESS) return false;

    uint64_t free_pages = vm_stat.free_count + vm_stat.speculative_count;
    uint64_t free_mb    = (free_pages * (uint64_t)page_size) / (1024ULL * 1024);
    used_mb             = total_mb - (long long)free_mb;
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * 3. 디스크 I/O — IOKit IOBlockStorageDriver
 * ═══════════════════════════════════════════════════════════════ */
struct DiskStat { uint64_t bytes_read; uint64_t bytes_written; };

static bool disk_stat_get(DiskStat &out) {
    out = {0, 0};
    io_iterator_t drive_list = IO_OBJECT_NULL;

    kern_return_t kr = IOServiceGetMatchingServices(
        kIOMainPortDefault,
        IOServiceMatching("IOBlockStorageDriver"),
        &drive_list);
    if (kr != KERN_SUCCESS) return false;

    io_registry_entry_t drive;
    while ((drive = IOIteratorNext(drive_list)) != IO_OBJECT_NULL) {
        CFMutableDictionaryRef props = nullptr;
        kr = IORegistryEntryCreateCFProperties(
                 drive, &props, kCFAllocatorDefault, kNilOptions);
        if (kr == KERN_SUCCESS && props) {
            auto *stat_dict = (CFDictionaryRef)CFDictionaryGetValue(
                props, CFSTR(kIOBlockStorageDriverStatisticsKey));
            if (stat_dict) {
                auto fetch = [&](const char *key, uint64_t &val) {
                    CFStringRef cfkey = CFStringCreateWithCString(
                        kCFAllocatorDefault, key, kCFStringEncodingUTF8);
                    auto *n = (CFNumberRef)CFDictionaryGetValue(stat_dict, cfkey);
                    CFRelease(cfkey);
                    if (n) {
                        int64_t v = 0;
                        CFNumberGetValue(n, kCFNumberSInt64Type, &v);
                        val += (uint64_t)v;
                    }
                };
                fetch(kIOBlockStorageDriverStatisticsBytesReadKey,
                      out.bytes_read);
                fetch(kIOBlockStorageDriverStatisticsBytesWrittenKey,
                      out.bytes_written);
            }
            CFRelease(props);
        }
        IOObjectRelease(drive);
    }
    IOObjectRelease(drive_list);
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * 4. DB 지표 — libmysqlclient 로컬 소켓 접속
 * ═══════════════════════════════════════════════════════════════ */
struct DbMetric { long long connections; long long questions; };

static bool db_metric_get(DbMetric &out) {
    out = {0, 0};

    const char *sock = g_mon_socket   ? g_mon_socket   : "/tmp/mysql.sock";
    const char *user = g_mon_user     ? g_mon_user     : "root";
    const char *pass = g_mon_password ? g_mon_password : "p@ssw0rd!@";

    MYSQL *mysql = mysql_init(nullptr);
    if (!mysql) return false;

    unsigned int timeout = 3;
    mysql_options(mysql, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(mysql, MYSQL_OPT_READ_TIMEOUT,    &timeout);

    if (!mysql_real_connect(mysql, nullptr, user, pass,
                            nullptr, 0, sock, 0)) {
        mysql_close(mysql);
        return false;
    }

    /* SHOW GLOBAL STATUS row[1] 값을 long long 으로 읽는 람다 */
    auto query_ll = [&](const char *sql, long long &val) {
        if (mysql_query(mysql, sql) != 0) return;
        MYSQL_RES *res = mysql_store_result(mysql);
        if (!res) return;
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row && row[1]) val = atoll(row[1]);
        mysql_free_result(res);
    };

    query_ll("SHOW GLOBAL STATUS LIKE 'Threads_connected'",
             out.connections);
    query_ll("SHOW GLOBAL STATUS LIKE 'Questions'",
             out.questions);

    mysql_close(mysql);
    return true;
}

/* ═══════════════════════════════════════════════════════════════
 * 5. 모니터 스레드
 * ═══════════════════════════════════════════════════════════════ */
static void *monitor_thread_func(void *) {
    mysql_thread_init();   /* libmysqlclient 스레드 초기화 */

    my_plugin_log_message(&g_plugin_info, MY_INFORMATION_LEVEL,
        PLUGIN_NAME ": monitor thread started (interval=%ds)",
        g_interval_sec);

    /* 디스크 I/O 델타용 이전 값 */
    DiskStat prev_disk = {0, 0};
    disk_stat_get(prev_disk);

    /* 쿼리/s 델타용 이전 Questions 값 */
    long long prev_questions = 0;
    {
        DbMetric m;
        if (db_metric_get(m)) prev_questions = m.questions;
    }

    while (g_running.load(std::memory_order_relaxed)) {

        /* ── 1. CPU (내부에서 1초 슬립) ── */
        double cpu = cpu_usage_measure();

        /* ── 2. 메모리 ── */
        long long mem_used = 0, mem_total = 0;
        mem_usage_get(mem_used, mem_total);

        /* ── 3. 디스크 I/O 델타 ── */
        DiskStat cur_disk = {0, 0};
        disk_stat_get(cur_disk);

        long long r_kbps = 0, w_kbps = 0;
        if (cur_disk.bytes_read >= prev_disk.bytes_read)
            r_kbps = (long long)(
                (cur_disk.bytes_read - prev_disk.bytes_read)
                / (uint64_t)g_interval_sec / 1024ULL);
        if (cur_disk.bytes_written >= prev_disk.bytes_written)
            w_kbps = (long long)(
                (cur_disk.bytes_written - prev_disk.bytes_written)
                / (uint64_t)g_interval_sec / 1024ULL);
        prev_disk = cur_disk;

        /* ── 4. DB 지표 ── */
        DbMetric db    = {0, 0};
        bool     db_ok = db_metric_get(db);
        long long qps  = 0;
        if (db_ok && db.questions >= prev_questions)
            qps = (db.questions - prev_questions) / g_interval_sec;
        if (db_ok) prev_questions = db.questions;

        /* ── 5. 전역 지표 갱신 (SHOW STATUS 로 조회 가능) ── */
        g_cpu_pct         = cpu;
        g_mem_used_mb     = mem_used;
        g_mem_total_mb    = mem_total;
        g_disk_read_kbps  = r_kbps;
        g_disk_write_kbps = w_kbps;
        g_db_connections  = db.connections;
        g_db_queries_ps   = qps;

        /* ── 6. MySQL error log 기록 ── */
        my_plugin_log_message(&g_plugin_info, MY_INFORMATION_LEVEL,
            PLUGIN_NAME
            ": CPU=%.1f%% | MEM=%lld/%lldMB"
            " | DISK_R=%lldKB/s DISK_W=%lldKB/s"
            " | CONN=%lld | QPS=%lld",
            cpu,
            mem_used, mem_total,
            r_kbps,   w_kbps,
            db.connections,
            qps);

        /* ── 7. 나머지 인터벌 대기 (cpu_usage_measure 에서 1초 소비됨) ── */
        int remain = g_interval_sec - 1;
        if (remain < 1) remain = 1;
        for (int i = 0; i < remain &&
                        g_running.load(std::memory_order_relaxed); ++i)
            sleep(1);
    }

    mysql_thread_end();
    my_plugin_log_message(&g_plugin_info, MY_INFORMATION_LEVEL,
        PLUGIN_NAME ": monitor thread stopped");
    return nullptr;
}

/* ═══════════════════════════════════════════════════════════════
 * 6. SHOW STATUS — 플러그인 상태 변수
 *    SHOW STATUS LIKE 'my_resource%';  로 조회
 * ═══════════════════════════════════════════════════════════════ */
static SHOW_VAR my_resource_status_vars[] = {
    {"my_resource_cpu_pct",
     (char *)&g_cpu_pct,         SHOW_DOUBLE,   SHOW_SCOPE_GLOBAL},
    {"my_resource_mem_used_mb",
     (char *)&g_mem_used_mb,     SHOW_LONGLONG, SHOW_SCOPE_GLOBAL},
    {"my_resource_mem_total_mb",
     (char *)&g_mem_total_mb,    SHOW_LONGLONG, SHOW_SCOPE_GLOBAL},
    {"my_resource_disk_read_kbps",
     (char *)&g_disk_read_kbps,  SHOW_LONGLONG, SHOW_SCOPE_GLOBAL},
    {"my_resource_disk_write_kbps",
     (char *)&g_disk_write_kbps, SHOW_LONGLONG, SHOW_SCOPE_GLOBAL},
    {"my_resource_db_connections",
     (char *)&g_db_connections,  SHOW_LONGLONG, SHOW_SCOPE_GLOBAL},
    {"my_resource_db_queries_ps",
     (char *)&g_db_queries_ps,   SHOW_LONGLONG, SHOW_SCOPE_GLOBAL},
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_GLOBAL}
};

/* ═══════════════════════════════════════════════════════════════
 * 7. 시스템 변수 (my.cnf [mysqld] 또는 SET GLOBAL 으로 설정)
 * ═══════════════════════════════════════════════════════════════ */
static MYSQL_SYSVAR_INT(
    interval,
    g_interval_sec,
    PLUGIN_VAR_OPCMDARG,
    "모니터링 주기(초, 기본값 5)",
    nullptr, nullptr,
    5, 1, 3600, 0);

static MYSQL_SYSVAR_STR(
    socket,
    g_mon_socket,
    PLUGIN_VAR_READONLY | PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
    "MySQL 모니터링 접속용 Unix 소켓 경로 (기본값: /tmp/mysql.sock)",
    nullptr, nullptr,
    "/tmp/mysql.sock");

static MYSQL_SYSVAR_STR(
    user,
    g_mon_user,
    PLUGIN_VAR_READONLY | PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
    "MySQL 모니터링 접속 계정 (기본값: root)",
    nullptr, nullptr,
    "root");

static MYSQL_SYSVAR_STR(
    password,
    g_mon_password,
    PLUGIN_VAR_READONLY | PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
    "MySQL 모니터링 접속 비밀번호 (기본값: 빈 문자열)",
    nullptr, nullptr,
    "");

static SYS_VAR *my_resource_sys_vars[] = {
    MYSQL_SYSVAR(interval),
    MYSQL_SYSVAR(socket),
    MYSQL_SYSVAR(user),
    MYSQL_SYSVAR(password),
    nullptr
};

/* ═══════════════════════════════════════════════════════════════
 * 8. 플러그인 init / deinit
 * ═══════════════════════════════════════════════════════════════ */
static int plugin_init(MYSQL_PLUGIN plugin_info) {
    g_plugin_info = plugin_info;
    g_running.store(true);

    int rc = pthread_create(&g_monitor_tid, nullptr,
                            monitor_thread_func, nullptr);
    if (rc != 0) {
        my_plugin_log_message(&g_plugin_info, MY_ERROR_LEVEL,
            PLUGIN_NAME ": pthread_create failed (rc=%d)", rc);
        g_running.store(false);
        return 1;
    }

    my_plugin_log_message(&g_plugin_info, MY_INFORMATION_LEVEL,
        PLUGIN_NAME ": initialized (interval=%ds)", g_interval_sec);
    return 0;
}

static int plugin_deinit(MYSQL_PLUGIN /*unused*/) {
    g_running.store(false);
    pthread_join(g_monitor_tid, nullptr);
    my_plugin_log_message(&g_plugin_info, MY_INFORMATION_LEVEL,
        PLUGIN_NAME ": deinitialized");
    return 0;
}

/* ═══════════════════════════════════════════════════════════════
 * 9. 플러그인 디스크립터 선언
 * ═══════════════════════════════════════════════════════════════ */
static struct st_mysql_daemon my_resource_descriptor = {
    MYSQL_DAEMON_INTERFACE_VERSION
};

mysql_declare_plugin(my_resource)
{
    MYSQL_DAEMON_PLUGIN,
    &my_resource_descriptor,
    PLUGIN_NAME,
    "KimJongYoul(youly92@naver.com)",
    "Monitors CPU/MEM/DiskIO/Connections/QPS every N seconds",
    PLUGIN_LICENSE_GPL,
    plugin_init,
    nullptr,            /* check_uninstall (MySQL 8.0+) */
    plugin_deinit,
    MY_RESOURCE_VER,
    my_resource_status_vars,
    my_resource_sys_vars,
    nullptr,            /* __reserved1 */
    0                   /* flags */
}
mysql_declare_plugin_end;
