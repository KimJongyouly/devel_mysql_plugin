# my_resource — MySQL 8.0 Daemon Plugin

macOS 에서 CPU · 메모리 · 디스크 I/O · DB 연결 수 · 초당 쿼리 수를 **5초마다**
MySQL error log 에 기록하는 데몬 플러그인입니다.

---

## 수집 항목

| 항목 | 방법 | SHOW STATUS 키 |
|---|---|---|
| CPU 사용률 (%) | macOS Mach `host_processor_info` | `my_resource_cpu_pct` |
| 메모리 사용량 (MB) | `vm_statistics64` / `hw.memsize` | `my_resource_mem_used_mb` / `my_resource_mem_total_mb` |
| 디스크 읽기 속도 (KB/s) | IOKit `IOBlockStorageDriver` | `my_resource_disk_read_kbps` |
| 디스크 쓰기 속도 (KB/s) | IOKit `IOBlockStorageDriver` | `my_resource_disk_write_kbps` |
| DB 연결 수 | `SHOW GLOBAL STATUS 'Threads_connected'` | `my_resource_db_connections` |
| 초당 쿼리 수 | `SHOW GLOBAL STATUS 'Questions'` 델타 | `my_resource_db_queries_ps` |

---

## 환경 요구 사항

| 항목 | 버전 |
|---|---|
| macOS | 12 Monterey 이상 (arm64 / x86_64) |
| MySQL | 8.0.43 (`/usr/local/mysql`) |
| Xcode Command Line Tools | 최신 |
| git | 2.x 이상 |
| clang++ | Xcode CLT 포함 |

---

## 빌드 절차

### Step 1 — MySQL 서버 소스 헤더 다운로드 (최초 1회)

MySQL 바이너리 배포판에는 `mysql/plugin.h` 가 포함되지 않습니다.
Makefile 의 `setup` 타겟이 GitHub 에서 `include/` 디렉터리만 sparse-checkout 으로 받습니다.

```bash
cd 04_Daemon
make setup
```

> 약 10~20 MB 다운로드. 완료 후 `mysql-src-8.0.43/include/` 가 생성됩니다.

이미 MySQL 8.0.43 소스를 로컬에 갖고 있다면 다운로드 없이 경로를 직접 지정할 수 있습니다.

```bash
make MYSQL_SRC=/path/to/mysql-8.0.43
```

---

### Step 2 — 컴파일

```bash
make
```

성공 시 `my_resource.so` 가 생성됩니다.

---

### Step 3 — MySQL plugin 디렉터리에 복사

```bash
sudo make install
# 또는 수동으로:
sudo cp my_resource.so /usr/local/mysql/lib/plugin/
```

---

### Step 4 — 모니터링 전용 MySQL 계정 생성 (선택)

root 계정을 그대로 사용해도 되지만, 최소 권한 계정을 권장합니다.

```sql
CREATE USER 'monitor'@'localhost' IDENTIFIED BY 'monitor_pass';
GRANT REPLICATION CLIENT ON *.* TO 'monitor'@'localhost';
-- 또는 필요 최소 권한:
-- GRANT PROCESS ON *.* TO 'monitor'@'localhost';
FLUSH PRIVILEGES;
```

---

### Step 5 — 플러그인 등록

MySQL 에 접속 후 실행합니다.

```sql
INSTALL PLUGIN my_resource SONAME 'my_resource.so';
```

---

## 설정 (my.cnf)

`[mysqld]` 섹션에 추가하면 MySQL 시작 시 자동 적용됩니다.

```ini
[mysqld]
# 모니터링 주기 (초, 기본값: 5)
my_resource_interval = 5

# 로컬 접속용 Unix 소켓 (기본값: /tmp/mysql.sock)
my_resource_socket = /tmp/mysql.sock

# 접속 계정 / 비밀번호 (기본값: root / 빈 문자열)
my_resource_user     = monitor
my_resource_password = monitor_pass
```

런타임 주기 변경 (소켓·계정은 READONLY — 재시작 필요):

```sql
SET GLOBAL my_resource_interval = 10;
```

---

## 동작 확인

### error log 에서 확인

현재 MySQL 서버의 `log_error` 경로를 자동으로 읽어 실시간 확인합니다.

```bash
sudo tail -f $(mysql --login-path=local -N -s -e "show global variables like 'log_error';" | awk '{print $2}')
```

> `--login-path=local` 은 `mysql_config_editor` 로 미리 저장한 접속 정보를 사용합니다.
> 저장 방법: `mysql_config_editor set --login-path=local --host=localhost --user=root --password`

경로를 직접 지정하는 경우:

```bash
tail -f /usr/local/mysql/data/*.err
```

정상 동작 시 아래와 같은 라인이 5초마다 출력됩니다.

```
[Note] my_resource: CPU=4.2% | MEM=7821/16384MB | DISK_R=12KB/s DISK_W=80KB/s | CONN=3 | QPS=15
```

### SHOW STATUS 로 확인

```sql
SHOW STATUS LIKE 'my_resource%';
```

```
+------------------------------+----------+
| Variable_name                | Value    |
+------------------------------+----------+
| my_resource_cpu_pct          | 4.200000 |
| my_resource_db_connections   | 3        |
| my_resource_db_queries_ps    | 15       |
| my_resource_disk_read_kbps   | 12       |
| my_resource_disk_write_kbps  | 80       |
| my_resource_mem_total_mb     | 16384    |
| my_resource_mem_used_mb      | 7821     |
+------------------------------+----------+
```

### 플러그인 상태 확인

```sql
SELECT PLUGIN_NAME, PLUGIN_STATUS
FROM information_schema.PLUGINS
WHERE PLUGIN_NAME = 'my_resource';
```

---

## 플러그인 제거

```sql
UNINSTALL PLUGIN my_resource;
```

---

## 구조 설명

```
my_resource.cpp
├── cpu_usage_measure()       CPU 1초 샘플링 (Mach API)
├── mem_usage_get()           메모리 조회 (vm_statistics64)
├── disk_stat_get()           디스크 I/O 누적값 (IOKit)
├── db_metric_get()           DB 지표 (libmysqlclient 로컬 재접속)
├── monitor_thread_func()     5초 주기 모니터 스레드 (pthread)
├── plugin_init()             스레드 시작
├── plugin_deinit()           스레드 종료 대기 (pthread_join)
├── my_resource_status_vars[] SHOW STATUS 변수 정의
└── my_resource_sys_vars[]    SET GLOBAL 변수 정의
```

### 스레드 구조

```
mysqld
└── plugin_init()
    └── pthread_create → monitor_thread_func()
        ├── [매 주기] cpu_usage_measure()   (1초 sleep 포함)
        ├── [매 주기] mem_usage_get()
        ├── [매 주기] disk_stat_get()       (누적 델타 계산)
        ├── [매 주기] db_metric_get()       (소켓 재접속 → 조회 → 닫기)
        ├── [매 주기] my_plugin_log_message() → error log
        └── [나머지 주기] sleep(1) × N
```

---

## 주의 사항

- `my_resource_password` 는 `SHOW VARIABLES` 로 노출됩니다.
  운영 환경에서는 전용 최소 권한 계정 사용을 권장합니다.
- 디스크 I/O 는 **모든 블록 스토리지 드라이브**의 합산 값입니다.
- CPU 측정은 1초 샘플링을 포함하므로 실질 주기는 `interval + 1`초입니다.
- MySQL 8.0 에서 `sql_print_information` 은 deprecated — 대신 `my_plugin_log_message` 사용.
- `check_uninstall` 콜백은 MySQL 8.0 플러그인 구조체에서 추가된 필드입니다
  (기존 7-field 구조체로 빌드하면 오류 발생).

---

## 빌드 명령 요약

```bash
make setup      # MySQL 헤더 다운로드 (최초 1회)
make            # 빌드
sudo make install  # 플러그인 디렉터리에 복사
make clean      # .so 삭제
make distclean  # .so + 다운로드 헤더 삭제
```
