# Developing MySQL / MariaDB Plugin

**C/C++로 MySQL·MariaDB 플러그인 개발 (UDF → 스토리지 엔진)**

---

## 책 소개

이 책은 MySQL·MariaDB 플러그인을 C/C++로 직접 만들어보는 실전 가이드입니다. 가장 단순한 스칼라 UDF(User Defined Function)에서 출발해, 집계 UDF, 데몬 플러그인, 그리고 커스텀 스토리지 엔진까지 차근차근 다루고 있습니다. 각 챕터는 독립적으로 실행해볼 수 있는 예제를 포함하고 있으며, macOS와 Linux 양쪽 환경을 모두 지원합니다.

---

## 목차

| 챕터 | 제목 | 핵심 내용 |
|------|------|-----------|
| 1장 | Hello World UDF | UDF 기본 구조, 빌드 및 설치, 첫 번째 함수 |
| 2장 | 인자를 받는 UDF | `UDF_ARGS` 구조체, 동적 메모리, 타입 검증 |
| 3장 | 집계 UDF — 중앙값 | 6단계 라이프사이클, GROUP BY 지원 집계 함수 |
| 4장 | 데몬 플러그인 | 백그라운드 스레드, OS 메트릭 수집, STATUS 변수 |
| 5장 | 커스텀 스토리지 엔진 | Parquet 파일 읽기, `handler` 클래스 구현 |
| 부록 A | UDF API 레퍼런스 | `UDF_INIT`/`UDF_ARGS` 구조체 전체 필드 참조표 |
| 부록 B | MySQL vs MariaDB API 차이 | 버전별 API 호환성, descriptor 차이 정리 |
| 부록 C | 빌드 환경 체크리스트 | macOS/Linux 패키지, 버전 호환성 매트릭스 |

### 챕터 흐름

```
1장 스칼라 UDF (인자 없음)
  ↓
2장 스칼라 UDF (인자 있음)
  ↓
3장 집계 UDF (GROUP BY 지원)
  ↓
4장 데몬 플러그인 (OS 모니터링)
  ↓
5장 커스텀 스토리지 엔진 (Parquet 읽기)
```

---

## 챕터별 상세 내용

### 1장: Hello World UDF

MySQL UDF의 기초를 다지는 챕터입니다. UDF의 종류(스칼라/집계/스토리지엔진)를 비교해보고, 가장 단순한 `helloworld()` 함수를 처음부터 함께 만들어 봅니다.

**주요 학습 내용:**
- UDF의 3-function 구조: `_init()` (1회) → 메인 함수 (행마다) → `_deinit()` (1회)
- `extern "C"` 선언이 필요한 이유 (C++ name mangling 방지)
- `-fPIC` / `-shared` 컴파일 플래그가 의미하는 것
- `CREATE FUNCTION` / `DROP FUNCTION` SQL 구문
- 자주 마주치는 설치 오류 3가지와 해결 방법

```sql
CREATE FUNCTION helloworld RETURNS STRING SONAME 'helloworld_udf.so';
SELECT helloworld();
```

---

### 2장: 인자를 받는 UDF

`UDF_ARGS` 구조체를 통해 SQL에서 전달된 인자를 처리하는 방법을 배웁니다. 메모리 관리와 타입 안전성을 어떻게 다루는지도 함께 살펴봅니다.

**주요 학습 내용:**
- `UDF_ARGS` 구조체 전체 필드 (arg_count, arg_type, args, lengths, maybe_null, attributes)
- 인자 타입 5종: `STRING_RESULT` / `INT_RESULT` / `REAL_RESULT` / `DECIMAL_RESULT` / `ROW_RESULT`
- `result` 버퍼 255바이트 한계와 `initid->ptr` 동적 할당 패턴
- `%.*s` 관용구로 null-terminate 없는 문자열을 안전하게 처리하는 방법
- 인자 검증을 `_init()`에서 처리하는 이유 (행마다 반복 검사를 피하기 위해)
- macOS(`bool`) vs Linux/MariaDB(`my_bool`) 타입 차이

```sql
SELECT hello_world('MySQL');
SELECT hello_world(name) FROM users;
```

---

### 3장: 집계 UDF — 중앙값

`AVG()`처럼 여러 행을 묶어 하나의 값을 반환하는 집계 UDF를 구현합니다. GROUP BY와 함께 동작하는 `my_median()` 함수를 예제로 사용합니다.

**주요 학습 내용:**
- 스칼라 UDF와 집계 UDF의 차이 (행당 1값 vs 그룹당 1값)
- **6단계 라이프사이클**: `_init` → `_clear` → `_reset` → `_add` × n → 메인 함수 → `_deinit`
- `_clear()`를 빠뜨리면 그룹 간 데이터가 누적되어 잘못된 결과가 나오는 이유
- `std::vector<double>` + `std::sort()`를 활용한 중앙값 알고리즘 (O(n log n))
- `new (std::nothrow)` — UDF 내에서 예외 전파를 막아야 하는 이유

```sql
CREATE AGGREGATE FUNCTION my_median RETURNS REAL SONAME 'my_median.so';
SELECT department, my_median(salary) FROM employees GROUP BY department;
```

---

### 4장: 데몬 플러그인

SQL 쿼리와 무관하게 MySQL 서버 안에서 항상 실행되는 백그라운드 스레드를 만들어 봅니다. OS의 CPU·메모리·디스크 메트릭을 주기적으로 수집하고 `SHOW STATUS`로 확인할 수 있도록 합니다.

**주요 학습 내용:**
- MySQL 플러그인 6종 비교 (UDF / Daemon / Storage / FTS / Audit / Auth)
- UDF와 데몬 플러그인의 차이 (CREATE FUNCTION vs INSTALL PLUGIN, 요청 기반 vs 상시 실행)
- `mysql_declare_plugin` (MySQL 8.0) vs `maria_declare_plugin` (MariaDB) 구조 차이
- `volatile int stop_logger` 플래그가 하는 일 (컴파일러 캐시 방지)
- **`pthread_join()` 가 필수인 이유**: 스레드 실행 중 플러그인이 언로드되면 segfault가 발생할 수 있습니다
- Linux: `sysinfo()` API로 CPU 부하·메모리 정보 수집
- macOS: `host_processor_info()`, `vm_statistics64`, IOKit `IOBlockStorageDriver`
- `SHOW_VAR` 배열로 STATUS 변수 등록, `MYSQL_SYSVAR_INT`로 설정 변수 등록

```sql
INSTALL PLUGIN os_logger SONAME 'os_logger.so';
SHOW STATUS LIKE 'my_resource%';
SET GLOBAL my_resource_interval = 5;
UNINSTALL PLUGIN os_logger;
```

---

### 5장: 커스텀 스토리지 엔진

MySQL의 스토리지 계층을 직접 구현해보는 챕터입니다. Parquet 파일을 마치 테이블처럼 `SELECT`할 수 있는 읽기 전용 스토리지 엔진 `ha_parquet`를 만들고, Apache Arrow C++ 라이브러리를 활용하는 방법도 함께 배웁니다.

**주요 학습 내용:**
- MySQL 스토리지 계층 아키텍처: SQL → 파서 → 옵티마이저 → `handler` 추상 클래스 → 엔진
- `Parquet_share` (`Handler_share` 상속) — 테이블당 하나씩 생성되며 `THR_LOCK` 포함
- `ha_parquet` (`handler` 상속) — 핵심 메서드 구현
- **사이드카 `.prq` 파일**: `CREATE TABLE ... COMMENT='파일경로'`로 파일 경로를 저장하면 서버 재시작 후에도 유지됩니다
- `rnd_init` / `rnd_next` / `rnd_end` 풀스캔 패턴 (`HA_ERR_END_OF_FILE`로 종료)
- `fill_row()`: Arrow `ChunkedArray`의 `GetScalar()` → MySQL `Field::store()` 타입별 변환
- `DATE32` → `MYSQL_TIME` 변환: Howard Hinnant civil_from_days 알고리즘
- **선택적 빌드 전략**: `cmake configure` → `make GenError`만 실행 → 엔진 소스만 컴파일 (전체 서버를 빌드할 필요가 없습니다)
- MySQL 8.0 vs MariaDB 10.6 `open()` / `create()` 시그니처 차이

```sql
CREATE TABLE parquet_data (
  id INT, name VARCHAR(255), score DOUBLE
) ENGINE=PARQUET COMMENT='/path/to/data.parquet';

SELECT * FROM parquet_data WHERE score > 90.0;
```

**현재 제약사항:**
- 읽기 전용 (INSERT / UPDATE / DELETE는 지원하지 않습니다)
- 인덱스 없음 (항상 풀스캔)
- 컬럼 순서 기반 매핑 (컬럼 이름이 달라도 순서만 맞으면 됩니다)
- 파일 전체를 메모리에 로드하는 방식

---

## 부록

### 부록 A: UDF API 레퍼런스
`UDF_INIT` / `UDF_ARGS` 구조체의 전체 필드를 한눈에 확인할 수 있는 참조표입니다. 반환 타입별(STRING / INTEGER / REAL) 함수 시그니처 3종, 인자 타입 상수 5종의 값·설명·C 타입을 정리해두었으니 개발 중 빠르게 찾아볼 때 활용하세요.

### 부록 B: MySQL vs MariaDB API 차이
버전 간 API 호환성 차이를 정리한 표입니다. `bool` vs `my_bool`, `char*` vs `unsigned char*` 같은 타입 차이부터, 데몬 플러그인 descriptor 필드 수 차이(11 vs 13), 스토리지 엔진 `open()` / `create()` / `delete_table()` 시그니처 차이, MariaDB 안정성 등급 6종까지 담겨 있습니다.

### 부록 C: 빌드 환경 체크리스트

**버전 호환성 매트릭스:**

| 챕터 | macOS | Linux |
|------|-------|-------|
| 1–3장 | MySQL 8.0.x / MariaDB 10.x, 11.x | MySQL 8.0.x / MariaDB 10.x, 11.x |
| 4장 | MySQL 8.0.43 / MariaDB 11.4.3 | MySQL 8.0.43 / MariaDB 11.4.3 |
| 5장 | MySQL 8.0.43 | MariaDB 10.6.x (버전 정확 일치 필수) |

---

## 소스코드 구조

```
Book_Source/
├── 01_HelloWorld/          # 1장: 기본 Hello World UDF
├── 02_HelloWorld_Args/     # 2장: 인자를 받는 Hello World
├── 03_Median/              # 3장: 중앙값 집계 UDF
├── 04_OS_logger/           # 4장: OS 로거 데몬 플러그인
└── 05_ReadParquet/         # 5장: Parquet 스토리지 엔진
```

각 챕터 디렉토리:
```
챕터명/
├── Mac/    # macOS 빌드 (Makefile + 소스)
└── Linux/  # Linux 빌드 (Makefile 또는 CMake + 소스)
```

### 빌드 방법

```bash
# 각 챕터/플랫폼 디렉토리에서
make

# 5장 Linux (CMake 사용)
./build.sh
```

---

## 사전 요구사항

| 항목 | macOS | Linux (Ubuntu) |
|------|-------|----------------|
| 빌드 도구 | Xcode Command Line Tools, cmake | build-essential, cmake |
| MySQL 헤더 | mysql-client (Homebrew) | libmysqlclient-dev |
| MariaDB 헤더 | mariadb (Homebrew) | libmariadb-dev |
| 5장 추가 | apache-arrow (Homebrew) | libarrow-dev, libparquet-dev |

> **참고**: `*.so` 빌드 결과물은 커밋하지 않습니다. Mac/Linux 빌드는 각자 독립적이기 때문에 크로스컴파일은 지원하지 않습니다. 5장 스토리지 엔진을 빌드할 때는 서버 소스 버전과 설치된 서버 버전이 정확히 일치해야 합니다.

## 소스 및 개발환경 
* 소스 위치 : https://github.com/KimJongyouly/devel_mysql_plugin/blob/main/Source.tar.xz
* 개발환경 
  * Linux  
    * OS : Ubuntu 22.04 LTS
    * Database : Mariadb 11.0.4 
  * Mac
    * Mac : 
    * Database : Mysql 8.0.43

