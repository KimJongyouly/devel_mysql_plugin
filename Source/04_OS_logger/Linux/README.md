# os_logger — MariaDB Daemon Plugin (Linux)

CPU 사용률과 메모리 사용량을 **5초마다** MariaDB 에러 로그에 기록하는 데몬 플러그인입니다.

> **대상 환경:** Ubuntu / MariaDB 11.4.3

## 수집 항목

| 항목 | 방법 | 출력 예 |
|------|------|---------|
| CPU 부하 | `sysinfo()` 시스템 콜 | `CPU Load: 0.23` |
| RAM 사용량/전체 (MB) | `sysinfo()` 시스템 콜 | `RAM: 3412/7982 MB` |

출력 형식:
```
[os_logger] CPU Load: 0.23, RAM: 3412/7982 MB
```

## 파일 구성

```
04_OS_logger/Linux/
├── os_logger.cpp   # 플러그인 소스 코드
├── Makefile        # 빌드 스크립트
└── README.md
```

## 요구사항

- Linux (Ubuntu 권장)
- MariaDB 11.4.3 (`/home/mariadb/mariadb_11.4.3`)
- g++

## 빌드 및 설치

```bash
cd /home/youly/Documents/Book/01_mysql_plugin/Book_Source/04_OS_logger

# 컴파일
make

# 플러그인 디렉터리에 복사
make install
# → /home/mariadb/mariadb_11.4.3/lib/plugin/ 에 복사
```

## 플러그인 로드

MariaDB에 접속 후:

```sql
-- 플러그인 설치 (동적 로드)
INSTALL PLUGIN os_logger SONAME 'os_logger.so';

-- 설치 확인
SELECT PLUGIN_NAME, PLUGIN_STATUS, PLUGIN_TYPE
FROM information_schema.PLUGINS
WHERE PLUGIN_NAME = 'os_logger';
```

## 동작 확인

플러그인은 백그라운드 데몬 스레드로 실행됩니다. MariaDB 에러 로그에서 출력을 확인합니다:

```bash
# 실시간 로그 확인
tail -f /home/mariadb/mariadb_11.4.3/data/*.err
```

5초마다 아래와 같이 출력됩니다:
```
[os_logger] CPU Load: 0.23, RAM: 3412/7982 MB
[os_logger] CPU Load: 0.18, RAM: 3398/7982 MB
```

## 플러그인 제거

```sql
-- 동적 언로드 (스레드 정지 후 제거)
UNINSTALL PLUGIN os_logger;
```

## 서버 재시작 시 자동 로드 (선택)

`my.cnf`의 `[mysqld]` 섹션에 추가:

```ini
[mysqld]
plugin-load-add = os_logger=os_logger.so
```

## 동작 흐름

```
INSTALL PLUGIN
     │
     ▼
os_logger_init()  ──→  pthread_create()  ──→  워커 스레드 시작
                                                │
                                                ▼
                                    5초마다 sysinfo() 읽어
                                    에러 로그에 출력
     │
UNINSTALL PLUGIN
     │
     ▼
os_logger_deinit()  ──→  stop_logger=1  ──→  pthread_join()  ──→  종료
```

## 플러그인 구조

| 함수 | 역할 |
|------|------|
| `os_logger_init()` | 백그라운드 스레드(`pthread_create`) 시작 |
| `os_logger_deinit()` | 스레드 종료 신호 → `pthread_join()`으로 대기 |
| `worker_thread()` | 5초 주기로 `sysinfo()` 읽어 에러 로그에 기록 |
