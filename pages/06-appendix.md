# 부록

---

## 부록 A. MySQL UDF API 레퍼런스

### UDF_INIT 구조체 전체 필드

```c
typedef struct st_udf_init {
    my_bool   maybe_null;       // NULL 반환 가능 여부 (기본값: false)
    unsigned int decimals;      // 반환값의 소수점 자릿수 (REAL 타입 시)
    unsigned long max_length;   // 반환 문자열의 최대 길이 (STRING 타입 시)
    char     *ptr;              // _init()에서 할당한 메모리 포인터 (자유 사용)
    my_bool   const_item;       // 항상 같은 값을 반환하는 상수 함수인지 여부
    void     *extension;        // 향후 확장용 (현재 미사용)
} UDF_INIT;
```

| 필드 | 타입 | 기본값 | 설명 |
|------|------|--------|------|
| `maybe_null` | `my_bool` | `false` | `true`이면 이 함수가 NULL을 반환할 수 있음 |
| `decimals` | `uint` | `0` | `REAL` 반환 시 소수점 자릿수 힌트 |
| `max_length` | `ulong` | `255` | `STRING` 반환 시 최대 바이트 길이 |
| `ptr` | `char*` | `NULL` | 초기화 함수에서 동적 할당한 메모리 저장 |
| `const_item` | `my_bool` | `false` | `true`이면 옵티마이저가 이 함수를 상수로 처리 |

### UDF_ARGS 구조체 전체 필드

```c
typedef struct st_udf_args {
    unsigned int   arg_count;          // 전달된 인자 개수
    enum Item_result *arg_type;        // 인자 타입 배열
    char           **args;             // 인자 값 포인터 배열
    unsigned long  *lengths;           // 인자 길이 배열
    char           *maybe_null;        // 인자별 NULL 가능 여부
    char           **attributes;       // 인자 이름(별칭) 배열
    unsigned long  *attribute_lengths; // 인자 이름 길이 배열
    void           *extension;
} UDF_ARGS;
```

### 반환 타입별 함수 시그니처

#### STRING 반환

```cpp
// _init()
bool my_func_init(UDF_INIT *initid, UDF_ARGS *args, char *message);

// 메인 함수
char *my_func(UDF_INIT *initid, UDF_ARGS *args,
              char *result, unsigned long *length,
              char *is_null, char *error);

// _deinit()
void my_func_deinit(UDF_INIT *initid);
```

#### INTEGER 반환

```cpp
bool my_func_init(UDF_INIT *initid, UDF_ARGS *args, char *message);

long long my_func(UDF_INIT *initid, UDF_ARGS *args,
                  char *is_null, char *error);

void my_func_deinit(UDF_INIT *initid);
```

#### REAL(DOUBLE) 반환

```cpp
bool my_func_init(UDF_INIT *initid, UDF_ARGS *args, char *message);

double my_func(UDF_INIT *initid, UDF_ARGS *args,
               char *is_null, char *error);

void my_func_deinit(UDF_INIT *initid);
```

### 인자 타입 상수

| 상수 | 값 | 설명 | C 데이터 타입 |
|------|-----|------|--------------|
| `STRING_RESULT` | `0` | 문자열 (VARCHAR, TEXT 등) | `char*` |
| `REAL_RESULT` | `1` | 실수 (FLOAT, DOUBLE) | `double*` |
| `INT_RESULT` | `2` | 정수 (INT, BIGINT 등) | `long long*` |
| `ROW_RESULT` | `3` | 다중 컬럼 (특수 용도) | — |
| `DECIMAL_RESULT` | `4` | 고정소수점 (DECIMAL) | `char*` (문자열) |

---

## 부록 B. MySQL vs MariaDB 플러그인 API 차이

### UDF API

UDF API는 MySQL과 MariaDB 사이에 거의 차이가 없습니다. 단, 다음에 주의합니다.

| 항목 | MySQL 8.0 | MariaDB 10.x/11.x |
|------|-----------|-------------------|
| `_init()` 반환 타입 | `bool` | `my_bool` |
| `is_null` 인자 타입 (메인 함수) | `char*` | `unsigned char*` |
| `error` 인자 타입 (메인 함수) | `char*` | `unsigned char*` |

MariaDB에서 `unsigned char*`를 `char*`로 잘못 선언하면 컴파일 경고나 오류가 발생할 수 있습니다.

### 데몬 플러그인 API

| 항목 | MySQL 8.0 | MariaDB 10.x/11.x |
|------|-----------|-------------------|
| 등록 매크로 | `mysql_declare_plugin` | `maria_declare_plugin` |
| descriptor 필드 수 | 11개 | 13개 (version 문자열, maturity 추가) |
| check_uninstall 콜백 | 있음 (7번째 필드) | 없음 |
| 헤더 | `mysql/plugin.h` | `mysql/plugin.h` (동일) |

MariaDB descriptor 추가 필드:

```cpp
maria_declare_plugin(name)
{
    ...,
    os_logger_init,      // init
    os_logger_deinit,    // deinit
    0x0100,              // version
    NULL,                // status vars
    NULL,                // system vars
    "1.0",               // ← MariaDB 전용: 버전 문자열
    MariaDB_PLUGIN_MATURITY_STABLE  // ← MariaDB 전용: 안정성 등급
}
```

MariaDB 안정성 등급:

| 상수 | 의미 |
|------|------|
| `MariaDB_PLUGIN_MATURITY_UNKNOWN` | 알 수 없음 |
| `MariaDB_PLUGIN_MATURITY_EXPERIMENTAL` | 실험적 |
| `MariaDB_PLUGIN_MATURITY_ALPHA` | 알파 |
| `MariaDB_PLUGIN_MATURITY_BETA` | 베타 |
| `MariaDB_PLUGIN_MATURITY_GAMMA` | 거의 안정 |
| `MariaDB_PLUGIN_MATURITY_STABLE` | 안정 |

### 스토리지 엔진 API

| 항목 | MySQL 8.0 | MariaDB 10.6 |
|------|-----------|-------------|
| `open()` 시그니처 | `open(name, mode, flags, const dd::Table*)` | `open(name, mode, flags)` |
| `create()` 시그니처 | `create(name, form, info, dd::Table*)` | `create(name, form, info)` |
| `delete_table()` 시그니처 | `delete_table(name, const dd::Table*)` | `delete_table(name)` |
| handler 생성 함수 | `(hton, table, partitioned, mem_root)` | `(hton, table, mem_root)` |
| 디버그 매크로 | `DBUG_TRACE` | `DBUG_ENTER` / `DBUG_RETURN` |
| 헤더 경로 | `"sql/handler.h"` | `"handler.h"` |

---

## 부록 C. 빌드 환경 체크리스트

### macOS 필수 패키지

| 패키지 | 용도 | 설치 방법 |
|--------|------|----------|
| Xcode Command Line Tools | clang++, make | `xcode-select --install` |
| MySQL 8.0 | 서버 + `mysql_config` | 공식 사이트 `.pkg` 또는 `brew install mysql` |
| cmake | 5장 스토리지 엔진 빌드 | `brew install cmake` |
| apache-arrow | 5장: libarrow, libparquet | `brew install apache-arrow` |
| openssl@3 | 5장 Arrow 빌드 의존성 | `brew install openssl@3` |

### Linux (Ubuntu) 필수 패키지

| 패키지 | 용도 | 설치 방법 |
|--------|------|----------|
| g++ | C++ 컴파일러 | `sudo apt install g++` |
| make | 빌드 | `sudo apt install make` |
| libmariadb-dev | MariaDB 헤더 | `sudo apt install libmariadb-dev` |
| cmake | 5장 스토리지 엔진 빌드 | `sudo apt install cmake` |
| libarrow-dev | 5장: Arrow 라이브러리 | `sudo apt install libarrow-dev` |
| libparquet-dev | 5장: Parquet 라이브러리 | `sudo apt install libparquet-dev` |
| libssl-dev | 5장 Arrow 빌드 의존성 | `sudo apt install libssl-dev` |

### 버전 호환성 매트릭스

| 챕터 | macOS | Linux |
|------|-------|-------|
| 1장 (HelloWorld UDF) | MySQL 8.0.x | MariaDB 10.x / 11.x |
| 2장 (HelloWorld Args UDF) | MySQL 8.0.x | MariaDB 10.x / 11.x |
| 3장 (Median Aggregate UDF) | MySQL 8.0.x | MariaDB 10.x / 11.x |
| 4장 (OS Logger Daemon) | MySQL 8.0.43 | MariaDB 11.4.3 |
| 5장 (ReadParquet Storage Engine) | MySQL 8.0.43 | MariaDB 10.6.x |

**중요**: 5장은 빌드 시 사용하는 MySQL/MariaDB 소스 버전이 실행 중인 서버 버전과 **정확히 일치**해야 합니다.

### 빠른 환경 확인 명령어

```bash
# MySQL 버전 확인
mysql_config --version          # macOS
mysql_config --version          # Linux

# MariaDB 버전 확인
/home/mariadb/mariadb_11.4.3/bin/mysql_config --version  # Linux 직접 설치

# 플러그인 디렉터리 확인
mysql_config --plugindir
mysql -u root -p -e "SHOW VARIABLES LIKE 'plugin_dir';"

# Arrow 라이브러리 확인
pkg-config --modversion arrow    # Linux
brew info apache-arrow           # macOS
```
