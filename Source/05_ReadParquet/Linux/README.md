# READ_PARQUET — MariaDB Storage Engine

로컬에 저장된 `.parquet` 파일을 MariaDB 테이블로 읽어오는 커스텀 스토리지 엔진입니다.
Apache Arrow C++ 라이브러리를 사용해 Parquet 파일을 파싱하며, **읽기 전용(Read-Only)** 으로 동작합니다.

> **대상 환경:** Ubuntu 22.04 / MariaDB 10.6

---

## 파일 구성

| 파일 | 역할 |
| --- | --- |
| `ha_parquet.h` | 클래스 선언 — `Parquet_share`, `ha_parquet` |
| `ha_parquet.cc` | 엔진 구현 — lifecycle, 스캔, 타입 변환, 플러그인 등록 |
| `CMakeLists.txt` | MariaDB 빌드 시스템 플러그인 등록 |
| `build.sh` | 빌드 자동화 스크립트 |

---

## 의존 라이브러리

| 라이브러리 | 버전 | 용도 |
| --- | --- | --- |
| Apache Arrow C++ (`libarrow`) | 6.0+ | 컬럼형 메모리 표현, 타입 시스템 |
| Apache Parquet C++ (`libparquet`) | 6.0+ | `.parquet` 파일 파싱 |
| MariaDB Server 헤더 | **설치된 서버와 동일 버전** | `handler`, `TABLE_SHARE`, `Field`, `THR_LOCK` 등 |

> **중요:** mariadb-server 소스 버전이 설치된 MariaDB 서버 버전과 일치해야 합니다.
> 버전 불일치 시 `INSTALL PLUGIN` 시 "API version too different" 오류가 발생합니다.

---

## MySQL 8.x 와의 주요 API 차이

이 엔진은 **MariaDB 10.6 전용**입니다. MySQL 8.x 버전과의 차이점은 다음과 같습니다.

| 항목 | MySQL 8.x | MariaDB 10.6 |
| --- | --- | --- |
| `open()` 시그니처 | `open(..., const dd::Table*)` | `dd::Table*` 없음 |
| `create()` 시그니처 | `create(..., dd::Table*)` | `dd::Table*` 없음 |
| `delete_table()` 시그니처 | `delete_table(..., const dd::Table*)` | `dd::Table*` 없음 |
| create handler 함수 | `(hton, table, bool partitioned, mem_root)` | `bool partitioned` 없음 |
| 플러그인 등록 매크로 | `mysql_declare_plugin` | `maria_declare_plugin` |
| 플러그인 descriptor 필드 | Check Uninstall 콜백 포함 | `version_info` 문자열, `maturity` 추가 |
| 디버그 매크로 | `DBUG_TRACE` | `DBUG_ENTER` / `DBUG_RETURN` / `DBUG_VOID_RETURN` |
| handler 헤더 | `"sql/handler.h"` | `"handler.h"` |

---

## 클래스 구조

```
handler (MariaDB 추상 기반 클래스)
└── ha_parquet
        ├── Parquet_share   (Handler_share 상속 — 테이블 잠금 공유 상태)
        ├── arrow::Table    (파일 전체를 메모리에 로드한 컬럼 테이블)
        ├── current_row     (rnd_next 커서)
        └── total_rows      (파일의 전체 행 수)
```

### Parquet_share

테이블 하나당 하나의 인스턴스를 공유합니다.
`Handler_share`를 상속하며 MariaDB의 테이블 레벨 락(`THR_LOCK`)을 보유합니다.

```
Parquet_share
├── THR_LOCK lock          — MariaDB 테이블 레벨 락
└── char table_name[]      — 테이블 경로 (확장자 제외)
```

### ha_parquet

`handler` 추상 클래스를 구현한 엔진 핸들러입니다.

```
ha_parquet
├── THR_LOCK_DATA lock          — 인스턴스별 락 데이터
├── Parquet_share *share        — 공유 상태 포인터
├── shared_ptr<arrow::Table>    — 메모리에 올라온 Arrow 테이블
├── int64_t current_row         — 현재 스캔 커서
└── int64_t total_rows          — 전체 행 수
```

---

## 동작 흐름

### CREATE TABLE

```
CREATE TABLE t (...) ENGINE=READ_PARQUET COMMENT='/data/sales.parquet';
        ↓
create()
  ├── COMMENT에서 parquet 파일 경로 추출
  │     없으면 → <MariaDB 테이블 경로>.parquet 을 기본값으로 사용
  └── 경로를 사이드카 파일 <tablepath>.prq 에 저장
```

> COMMENT를 생략하면 MariaDB 데이터 디렉터리의 `<테이블명>.parquet`을 사용합니다.
> 해당 파일이 없으면 SELECT 시 "Table doesn't exist" 오류가 발생합니다.

### SELECT

```
SELECT * FROM t;
        ↓
open()
  ├── .prq 사이드카 파일 읽기 → parquet 경로 획득
  ├── arrow::io::ReadableFile::Open()
  ├── parquet::arrow::OpenFile()  → FileReader 생성 (Result API)
  └── reader->ReadTable()         → arrow::Table 메모리 로드

rnd_init()
  └── current_row = 0

rnd_next() × N
  └── fill_row(buf, current_row++)
        ├── Arrow ChunkedArray 에서 해당 행 값 추출
        └── MariaDB Field::store() 로 row 버퍼에 기록

rnd_next()
  └── HA_ERR_END_OF_FILE  (스캔 종료)

close()
  └── arrow_table.reset()  (Arrow 메모리 해제)
```

### DROP TABLE

```
DROP TABLE t;
        ↓
delete_table()
  └── .prq 사이드카 파일 삭제
```

---

## 핵심 메서드

| 메서드 | 역할 |
| --- | --- |
| `create()` | `.prq` 사이드카 파일 생성, parquet 경로 저장 |
| `open()` | parquet 파일 열기, Arrow 테이블 메모리 로드 |
| `close()` | Arrow 테이블 메모리 해제 |
| `delete_table()` | `.prq` 사이드카 파일 삭제 |
| `rnd_init()` | 풀스캔 준비, 커서 초기화 |
| `rnd_next()` | 한 행씩 읽어 MariaDB row 버퍼에 기록 |
| `rnd_end()` | 풀스캔 종료 |
| `position()` | 현재 row 인덱스를 `ref` 버퍼에 저장 |
| `rnd_pos()` | `ref`로 특정 행 랜덤 접근 |
| `fill_row()` | Arrow 컬럼 타입 → MariaDB `Field::store()` 변환 |
| `info()` | 옵티마이저에 행 수 등 통계 제공 |
| `store_lock()` | 쓰기 락을 읽기 락으로 다운그레이드 |
| `write_row()` 외 | `HA_ERR_WRONG_COMMAND` — 쓰기 거부 |

---

## Arrow → MariaDB 타입 매핑

`fill_row()` 내부에서 Arrow 컬럼의 타입을 런타임에 판별해 적합한 `Field::store()` 변형을 호출합니다.

| Arrow 타입 | MariaDB 필드 타입 | 변환 방법 |
| --- | --- | --- |
| `INT8` / `INT16` / `INT32` / `INT64` | `INT`, `BIGINT` | `field->store(longlong, false)` |
| `UINT8` / `UINT16` / `UINT32` / `UINT64` | `INT UNSIGNED` | `field->store(longlong, true)` |
| `FLOAT` | `FLOAT` | `field->store(double)` |
| `DOUBLE` | `DOUBLE` | `field->store(double)` |
| `HALF_FLOAT` | `FLOAT` | IEEE 754 binary16 → float32 수동 변환 후 `store(double)` |
| `BOOL` | `TINYINT(1)` | `field->store(0 or 1, false)` |
| `STRING` / `LARGE_STRING` | `VARCHAR`, `TEXT` | `field->store(ptr, len, utf8mb4)` |
| `BINARY` / `LARGE_BINARY` | `BLOB`, `VARBINARY` | `field->store(ptr, len, binary)` |
| `DATE32` | `DATE` | Hinnant civil-from-days → `store_time()` |
| `DATE64` | `DATE` | ms ÷ 86400000 → days → `store_time()` |
| `TIMESTAMP(s/ms/us/ns)` | `DATETIME(6)` | 단위 정규화 → `gmtime_r()` → `store_time()` |
| `DECIMAL128` / `DECIMAL256` | `DECIMAL` | `FormatValue()` → 문자열 → `store()` |
| 그 외 | `VARCHAR` | `arr->GetScalar()->ToString()` fallback |

### 날짜 변환 상세 — `days_to_mysql_date()`

DATE32는 Unix epoch(1970-01-01) 기준 일수입니다.
Howard Hinnant의 Gregorian calendar 알고리즘으로 연/월/일로 변환합니다.

```
days → z = days + 719468
     → era, doe, yoe 계산 (400년 주기)
     → year, month, day 추출
     → MYSQL_TIME{} 구성 → field->store_time()
```

---

## 사이드카 파일 (`.prq`)

MariaDB 서버가 재시작되어도 parquet 경로를 기억하기 위해, 테이블 생성 시 `.prq` 파일을 작성합니다.

```text
/var/lib/mysql/mydb/mytable.prq
  ↳ 내용: /data/warehouse/sales_2024.parquet
```

| 이벤트 | 동작 |
| --- | --- |
| `CREATE TABLE` | `.prq` 파일 생성 (COMMENT 경로 또는 기본 경로 저장) |
| `open()` | `.prq` 파일 읽어 parquet 경로 획득 |
| `DROP TABLE` | `.prq` 파일 삭제 |

---

## 빌드 방법

### 1단계 — 의존성 설치

```bash
sudo apt install mariadb-server mariadb-server-dev \
                 libarrow-dev libparquet-dev \
                 cmake g++ libssl-dev
```

### 2단계 — MariaDB 서버 소스 클론 및 버전 맞춤

**설치된 MariaDB 서버와 소스 버전이 반드시 일치해야 합니다.**

```bash
cd 05_ReadParquet/Linux/

# 전체 클론
git clone https://github.com/MariaDB/server.git mariadb-server

# 또는 얕은 클론
git clone --depth=1 https://github.com/MariaDB/server.git mariadb-server
```

설치된 MariaDB 버전 확인 후 소스를 해당 태그로 체크아웃합니다:

```bash
# 설치된 서버 버전 확인
mariadb_config --version        # 예: 10.6.18-MariaDB
# 또는
mysql_config --version

# 소스를 동일 버전 태그로 전환
git -C mariadb-server fetch --tags
git -C mariadb-server checkout mariadb-10.6.18   # 버전에 맞게 변경
```

### 3단계 — 심볼릭 링크 설정

`build.sh`가 자동으로 처리하지만, 수동으로 설정하려면:

```bash
ln -s "$(pwd)" mariadb-server/storage/read_parquet
```

### 4단계 — 빌드 스크립트 실행

```bash
chmod +x build.sh

./build.sh          # configure + 빌드 (CMakeLists.txt 변경 시 자동 재설정)
./build.sh clean    # 빌드 디렉터리 삭제 후 클린 빌드
./build.sh install  # 빌드 후 MariaDB 플러그인 디렉터리에 자동 복사 (sudo 필요)
```

빌드 성공 시 `ha_parquet.so` 가 생성됩니다.

#### 빌드 내부 동작

전체 MariaDB 서버를 컴파일하지 않습니다. 필요한 부분만 선택적으로 빌드합니다:

```text
cmake configure    → 헤더 경로·컴파일 플래그 생성 (빌드 없음)
        ↓
make GenError      → mysqld_error.h 등 생성 헤더만 빌드
        ↓
make (build.make)  → ha_parquet.cc.o 만 컴파일
        ↓
g++ -shared -fPIC -Wl,-soname,ha_parquet.so
        → Arrow + Parquet 만 링크
        → MariaDB 심볼은 INSTALL PLUGIN 시 mysqld 가 런타임에 제공
        ↓
ha_parquet.so
```

#### 환경 정보 (스크립트 내 기본값)

| 항목 | 경로 |
| --- | --- |
| MariaDB 서버 소스 | `05_ReadParquet/Linux/mariadb-server/` |
| mariadb_config | `$(command -v mariadb_config)` |
| Apache Arrow | `/usr/lib/x86_64-linux-gnu/libarrow.so` |
| Apache Parquet | `/usr/lib/x86_64-linux-gnu/libparquet.so` |
| 빌드 디렉터리 | `05_ReadParquet/Linux/build_parquet/` |
| 출력 파일 | `05_ReadParquet/Linux/ha_parquet.so` |

---

## 사용 방법

### 1. 플러그인 설치

```sql
-- ha_parquet.so 를 MariaDB 플러그인 디렉터리에 복사한 후 등록
INSTALL PLUGIN read_parquet SONAME 'ha_parquet.so';

-- 설치 확인
SHOW PLUGINS;
```

`./build.sh install` 을 사용하면 복사까지 자동으로 처리합니다.

### 2. 테이블 생성

parquet 파일의 스키마와 MariaDB DDL의 **컬럼 순서**가 일치해야 합니다.

```sql
-- COMMENT로 절대 경로 지정 (권장)
CREATE TABLE sales (
    order_id   INT,
    product    VARCHAR(128),
    amount     DOUBLE,
    order_date DATE
) ENGINE=READ_PARQUET
  COMMENT='/data/warehouse/sales_2024.parquet';
```

```sql
-- COMMENT 생략 → MariaDB 테이블 데이터 디렉터리의 .parquet 파일 사용
-- /var/lib/mysql/mydb/sales.parquet 이 자동으로 사용됨
CREATE TABLE sales (
    order_id   INT,
    product    VARCHAR(128),
    amount     DOUBLE,
    order_date DATE
) ENGINE=READ_PARQUET;
```

#### 테스트용 parquet 파일 생성 (Python)

```python
import pandas as pd

df = pd.DataFrame({
    'order_id':   [1, 2, 3],
    'product':    ['Apple', 'Banana', 'Cherry'],
    'amount':     [10.5, 20.0, 15.75],
    'order_date': pd.to_datetime(['2024-01-01', '2024-01-02', '2024-01-03'])
})
df.to_parquet('/tmp/sales.parquet', index=False)
```

### 3. 쿼리

```sql
-- 전체 조회
SELECT * FROM sales LIMIT 10;

-- 집계
SELECT product, SUM(amount) AS total
FROM sales
GROUP BY product
ORDER BY total DESC;

-- 조인 (다른 엔진과도 가능)
SELECT s.product, c.region
FROM sales s
JOIN categories c ON s.product = c.name;
```

### 4. 제거

```sql
DROP TABLE sales;
-- .prq 사이드카 파일도 함께 삭제됩니다

UNINSTALL PLUGIN read_parquet;
```

---

## 제약 사항

| 항목 | 내용 |
| --- | --- |
| 쓰기 | 불가 — INSERT / UPDATE / DELETE 모두 거부 |
| 인덱스 | 미지원 — 항상 풀스캔 |
| 트랜잭션 | 미지원 |
| 컬럼 매핑 | 이름이 아닌 **순서** 기준 (parquet col 0 → MariaDB field 0) |
| 메모리 | 파일 전체를 메모리에 로드 (대용량 파일 주의) |
| NULL | Arrow null → MariaDB NULL 정상 처리 |
| parquet 경로 | COMMENT 미지정 시 `.parquet` 파일이 데이터 디렉터리에 있어야 함 |
| MariaDB 버전 | 소스와 서버 버전 일치 필요 (불일치 시 API 버전 오류) |
| C++ 표준 | C++17 이상 필요 |

---

## 트러블슈팅

| 증상 | 원인 | 해결 |
| --- | --- | --- |
| `API version for STORAGE ENGINE plugin is too different` | mariadb-server 소스 버전 ≠ 설치된 서버 버전 | `git checkout mariadb-<버전>` 후 `./build.sh clean` |
| `Table 'db.table' doesn't exist` (SELECT 시) | parquet 파일 없음 또는 경로 오류 | COMMENT에 절대 경로 지정, 파일 존재 확인 |
| `GenError 빌드 실패` | OpenSSL 또는 zlib 누락 | `sudo apt install libssl-dev` 후 `./build.sh clean` |
| `컴파일 실패 — mysqld_error.h not found` | 생성 헤더 누락 | `./build.sh clean` 으로 재설정 |
| `링크 실패 — libarrow.so not found` | Apache Arrow 미설치 | `sudo apt install libarrow-dev` |
| `sudo: cp: command not found` (install 시) | sudo 환경 PATH 문제 | `sudo cp ha_parquet.so $(mariadb_config --plugindir)/` |

---

## 참고한 엔진

| 엔진 | 참고한 패턴 |
| --- | --- |
| `ha_example.cc` | `get_share()`, `store_lock()`, 플러그인 등록 기본 구조 |
| `ha_blackhole.cc` | Slave applier 처리, 락 다운그레이드 패턴 |
| `ha_tina.cc` (CSV) | 파일 기반 테이블 open/close, `rnd_next()` 스캔 루프 |
| `transparent_file.cc` | 파일 읽기 버퍼링 방식 참고 |
