# MySQL / MariaDB 플러그인 개발 실전 — 목차

> 이 책은 MySQL UDF(User Defined Function)부터 스토리지 엔진까지,
> C/C++로 MySQL·MariaDB 플러그인을 만드는 과정을 단계별로 안내합니다.

---

## [제1장: Hello World UDF — 첫 번째 플러그인 만들기](pages/01-ch.md)

### [1.1 UDF란 무엇인가](pages/01-ch.md#11-udf란-무엇인가)
- User Defined Function 개념
- 스칼라 UDF vs 집계 UDF vs 스토리지 엔진
- UDF가 필요한 상황

### [1.2 개발 환경 준비](pages/01-ch.md#12-개발-환경-준비)
- macOS 환경 설정 (Homebrew + MySQL 8.0)
- Linux 환경 설정 (Ubuntu + MariaDB)
- 필수 헤더 및 빌드 도구 확인

### [1.3 첫 번째 UDF 작성](pages/01-ch.md#13-첫-번째-udf-작성)
- `mysql.h` 헤더 구조
- `_init()`, 메인 함수, `_deinit()` 세 가지 함수
- "Hello World Mysql Plugin-UDF" 반환하기

### [1.4 빌드와 설치](pages/01-ch.md#14-빌드와-설치)
- Makefile 작성법
- `make` → `.so` 파일 생성
- MySQL 플러그인 디렉터리에 설치
- `CREATE FUNCTION` 으로 등록

### [1.5 동작 확인](pages/01-ch.md#15-동작-확인)
- `SELECT helloworld();` 실행
- `DROP FUNCTION` 으로 등록 해제
- 트러블슈팅: `mysql.h not found` 등

---

## [제2장: 인자를 받는 UDF — Hello World with Args](pages/02-ch.md)

### [2.1 UDF 인자 처리 구조](pages/02-ch.md#21-udf-인자-처리-구조)
- `UDF_ARGS` 구조체
- 인자 타입 검증 (`STRING_RESULT`)
- NULL 처리

### [2.2 동적 메모리 관리](pages/02-ch.md#22-동적-메모리-관리)
- `initid->ptr`로 버퍼 할당
- `_deinit()`에서 해제

### [2.3 구현: `helloworld(name)` 함수](pages/02-ch.md#23-구현-helloworldname-함수)
- `"Welcome {name}, HelloWorld Mysql Plugin"` 반환
- 문자열 조합 로직

### [2.4 테스트](pages/02-ch.md#24-테스트)
- 다양한 입력값 테스트
- NULL 입력 처리 확인
- 오류 케이스 확인

---

## [제3장: 집계 UDF — GROUP BY와 함께 중앙값 계산](pages/03-ch.md)

### [3.1 집계 UDF란](pages/03-ch.md#31-집계-udf란)
- 스칼라 UDF와 집계 UDF의 차이
- MySQL Aggregate UDF의 6단계 라이프사이클

### [3.2 6개 함수 구조](pages/03-ch.md#32-6개-함수-구조)
- `_init()`: 초기화
- `_clear()`: 그룹 초기화 (GROUP BY 핵심)
- `_reset()`: 첫 번째 행 처리
- `_add()`: 누적
- 메인 함수: 결과 계산
- `_deinit()`: 메모리 해제

### [3.3 중앙값 알고리즘](pages/03-ch.md#33-중앙값-알고리즘)
- `std::vector<double>` 누적
- `std::sort()` 후 중앙값 추출
- 짝수/홀수 처리

### [3.4 `CREATE AGGREGATE FUNCTION` 으로 등록](pages/03-ch.md#34-create-aggregate-function-으로-등록)
### [3.5 `GROUP BY` 와 함께 사용하기](pages/03-ch.md#35-group-by-와-함께-사용하기)

---

## [제4장: 데몬 플러그인 — OS 리소스 모니터링](pages/04-ch.md)

### [4.1 데몬 플러그인이란](pages/04-ch.md#41-데몬-플러그인이란)
- Daemon Plugin 구조
- `MYSQL_DAEMON_PLUGIN` 매크로
- UDF와의 차이점

### [4.2 백그라운드 스레드 구현](pages/04-ch.md#42-백그라운드-스레드-구현)
- `pthread_create()`로 모니터링 스레드 시작
- `pthread_join()`으로 안전한 종료
- `INSTALL PLUGIN` / `UNINSTALL PLUGIN` 연동

### [4.3 OS 메트릭 수집](pages/04-ch.md#43-os-메트릭-수집)
- macOS: Mach API (`host_processor_info`, `vm_statistics64`)
- macOS: IOKit (`IOBlockStorageDriver`)
- Linux: `sysinfo()` 시스템 콜

### [4.4 MySQL STATUS 변수 노출](pages/04-ch.md#44-mysql-status-변수-노출)
- `SHOW STATUS LIKE 'my_resource%'`
- `SHOW GLOBAL VARIABLES`로 주기 설정

### [4.5 MySQL 에러 로그에 기록](pages/04-ch.md#45-mysql-에러-로그에-기록)
- `my_plugin_log_message()` 사용
- 주의: `sql_print_information` deprecated (MySQL 8.0)

---

## [제5장: 커스텀 스토리지 엔진 — Parquet 파일 읽기](pages/05-ch.md)

### [5.1 스토리지 엔진 아키텍처](pages/05-ch.md#51-스토리지-엔진-아키텍처)
- `handler` 추상 클래스
- `handlerton` 구조체
- MySQL 8.0 vs MariaDB 10.6 API 차이

### [5.2 핵심 클래스 설계](pages/05-ch.md#52-핵심-클래스-설계)
- `Parquet_share`: 테이블 수준 공유 상태 및 락
- `ha_parquet`: 핸들러 구현체
- 커서 기반 스캔: `current_row` / `total_rows`

### [5.3 Apache Arrow C++ 라이브러리](pages/05-ch.md#53-apache-arrow-c-라이브러리)
- `libarrow` / `libparquet` 설치
- `arrow::io::ReadableFile` → `parquet::arrow::FileReader`
- `reader->ReadTable()`: 파일 전체를 메모리에 로드

### [5.4 핵심 메서드 구현](pages/05-ch.md#54-핵심-메서드-구현)
- `create()`: 사이드카 `.prq` 파일 생성
- `open()` / `close()`: Arrow 테이블 로드/해제
- `rnd_init()` / `rnd_next()` / `rnd_end()`: 풀스캔
- `fill_row()`: Arrow 타입 → MySQL Field 변환

### [5.5 Arrow → MySQL 타입 매핑](pages/05-ch.md#55-arrow-mysql-타입-매핑)
- 정수, 실수, 문자열, 날짜, Decimal 변환
- `DATE32`: Howard Hinnant Gregorian calendar 알고리즘

### [5.6 빌드 시스템](pages/05-ch.md#56-빌드-시스템)
- MySQL/MariaDB 소스 버전 맞추기
- CMakeLists.txt 작성
- `build.sh`로 선택적 빌드 (전체 서버 컴파일 없이)

### [5.7 사용 예시](pages/05-ch.md#57-사용-예시)
- `CREATE TABLE ... ENGINE=READ_PARQUET COMMENT='경로'`
- Python으로 테스트용 Parquet 파일 생성
- 제약 사항: 읽기 전용, 인덱스 미지원, 컬럼 순서 기반 매핑

---

---

## 챕터 구성 흐름

```
1장 스칼라 UDF (인자 없음)
  ↓ 개념 확장: 인자 처리 + 동적 메모리
2장 스칼라 UDF (인자 있음)
  ↓ 함수 종류 확장: 집계 함수
3장 집계 UDF (GROUP BY 지원)
  ↓ 플러그인 종류 변경: 백그라운드 스레드
4장 데몬 플러그인 (OS 모니터링)
  ↓ 플러그인 종류 확장: 스토리지 레이어
5장 커스텀 스토리지 엔진 (Parquet 읽기)
```

**흐름 원칙:**
- 단순 → 복잡 (인자 없음 → 있음 → 집계 → 데몬 → 엔진)
- 각 챕터는 이전 챕터에서 배운 플러그인 기초 위에 새로운 개념을 추가
- 4장(데몬)은 3장까지의 UDF 개념과 독립적이지만, MySQL 플러그인 시스템의 다른 축을 보여줌
- 5장은 독립적으로 읽을 수 있으나, 4장의 `plugin_init`/`plugin_deinit` 패턴을 이해한 후 보면 더 명확

---

## 챕터별 학습 목표

| 챕터 | 학습 목표 |
|------|----------|
| 1장 | MySQL UDF의 기본 구조(`_init` / 메인 / `_deinit`)를 이해하고, C++로 최소한의 스칼라 UDF를 직접 빌드·등록·실행할 수 있다. |
| 2장 | `UDF_ARGS`를 통해 인자를 검증·처리하고, 동적 메모리를 안전하게 관리하며 문자열을 반환하는 UDF를 작성할 수 있다. |
| 3장 | 집계 UDF의 6단계 라이프사이클(`_init/_clear/_reset/_add/main/_deinit`)을 파악하고, `GROUP BY`에서 올바르게 동작하는 중앙값 함수를 구현할 수 있다. |
| 4장 | Daemon Plugin으로 백그라운드 스레드를 관리하고, OS 메트릭을 수집해 MySQL STATUS 변수 및 에러 로그에 노출하는 방법을 이해할 수 있다. |
| 5장 | 커스텀 스토리지 엔진의 `handler` 추상 클래스를 구현하고, Apache Arrow C++ 라이브러리로 Parquet 파일을 MySQL 테이블처럼 쿼리하는 엔진을 빌드할 수 있다. |

---

## [부록](pages/06-appendix.md)

### [A. MySQL UDF API 레퍼런스](pages/06-appendix.md#부록-a-mysql-udf-api-레퍼런스)
- `UDF_INIT`, `UDF_ARGS` 구조체 필드 전체 목록
- 반환 타입별 함수 시그니처

### [B. MySQL vs MariaDB 플러그인 API 차이](pages/06-appendix.md#부록-b-mysql-vs-mariadb-플러그인-api-차이)
- UDF, Daemon Plugin, Storage Engine 별 호환성 정리

### [C. 빌드 환경 체크리스트](pages/06-appendix.md#부록-c-빌드-환경-체크리스트)
- macOS / Linux 필수 패키지 목록
- 버전 호환성 매트릭스