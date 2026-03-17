# my_median — MySQL 8.0 Aggregate UDF

MySQL 8.0용 Aggregate UDF(User Defined Function)로, SQL에서 `my_median(column)`을 통해 중앙값(median)을 계산합니다.
`GROUP BY`와 함께 사용하면 각 그룹별 중앙값을 구할 수 있습니다.

---

## 파일 구성

```
03_Aggregate/
├── my_median.cpp   # UDF 소스 코드
├── Makefile        # 빌드 및 설치 스크립트
└── README.md       # 이 문서
```

---

## UDF 함수 구조

MySQL Aggregate UDF는 아래 6개의 함수를 순서대로 호출합니다.

| 함수 | 호출 시점 | 역할 |
|---|---|---|
| `my_median_init` | 쿼리 시작 시 1회 | 메모리 할당, 인자 타입 검증 |
| `my_median_clear` | 각 그룹 시작 시 | 이전 그룹 값 초기화 (GROUP BY 필수) |
| `my_median_reset` | 각 그룹의 첫 번째 로우 | clear 후 첫 값 추가 |
| `my_median_add` | 각 그룹의 두 번째~ 로우 | 값 누적 |
| `my_median` | 각 그룹 종료 시 | 정렬 후 중앙값 계산 및 반환 |
| `my_median_deinit` | 쿼리 완료 후 1회 | 메모리 해제 |

> **주의**: `my_median_clear`가 없으면 GROUP BY 사용 시 이전 그룹의 값이 다음 그룹에 누적되어 잘못된 결과가 반환됩니다.

---

## 빌드 환경

- macOS (Apple Silicon / Intel 모두 지원)
- MySQL 8.0.x (`/usr/local/mysql`)
- g++ (C++17 이상)

---

## 빌드 및 설치

### 1. 빌드

```bash
make
```

`my_median.so` 파일이 생성됩니다.

### 2. MySQL 플러그인 디렉터리에 설치 및 UDF 등록

```bash
make install
```

내부적으로 아래 작업을 수행합니다:
1. `my_median.so`를 MySQL 플러그인 디렉터리(`/usr/local/mysql/lib/plugin`)에 복사
2. MySQL에 Aggregate Function으로 등록

```sql
CREATE AGGREGATE FUNCTION my_median RETURNS REAL SONAME 'my_median.so';
```

### 3. 제거

```bash
make uninstall
```

### 4. 빌드 파일 정리

```bash
make clean
```

---

## 사용 예시

### 기본 사용

```sql
-- 단일 중앙값
SELECT my_median(score) FROM exam_results;

-- 그룹별 중앙값
SELECT department, my_median(salary)
FROM employees
GROUP BY department;
```

### 테스트용 데이터

```sql
CREATE TABLE test_median (
    id       INT AUTO_INCREMENT PRIMARY KEY,
    category VARCHAR(10),
    value    DOUBLE
);

INSERT INTO test_median (category, value) VALUES
    ('A', 3), ('A', 1), ('A', 4), ('A', 1), ('A', 5),
    ('B', 10), ('B', 20), ('B', 30);

-- 전체 중앙값: 4.5 (1,1,3,4,5,10,20,30 → (4+5)/2)
SELECT my_median(value) FROM test_median;

-- 그룹별 중앙값
-- A: 3.0  (1,1,3,4,5 → 3)
-- B: 20.0 (10,20,30 → 20)
SELECT category, my_median(value)
FROM test_median
GROUP BY category;
```

---

## 지원하는 인자 타입

| MySQL 타입 | 지원 여부 |
|---|---|
| `DOUBLE`, `FLOAT` | ✅ |
| `INT`, `BIGINT` 등 정수형 | ✅ (내부적으로 DOUBLE로 변환) |
| `DECIMAL` | ✅ (내부적으로 DOUBLE로 변환) |
| `VARCHAR`, `TEXT` 등 문자열 | ❌ |

---

## 알고리즘

1. 각 로우의 값을 `std::vector<double>`에 누적
2. 그룹 종료 시 `std::sort`로 오름차순 정렬
3. 짝수 개: 중간 두 값의 평균, 홀수 개: 중간 값 반환
4. 빈 그룹: `NULL` 반환

**시간 복잡도**: O(n log n) — n은 그룹 내 로우 수
**공간 복잡도**: O(n)

---

## 트러블슈팅

### `mysql.h` not found

```
my_median.cpp:1:10: fatal error: mysql.h: No such file or directory
```

`/usr/local/mysql`이 없거나 경로가 다른 경우 Makefile의 `MYSQL_CONFIG` 경로를 수정하세요.

```makefile
MYSQL_CONFIG := /path/to/your/mysql/bin/mysql_config
```

### UDF 등록 오류 (`plugin_dir`)

MySQL 설정에서 `plugin_dir`이 다른 경로를 가리키는 경우:

```sql
SHOW VARIABLES LIKE 'plugin_dir';
```

결과에 맞게 `.so` 파일을 복사하거나 `make install` 전에 `MYSQL_PLUGIN_DIR`을 재정의하세요:

```bash
make install MYSQL_PLUGIN_DIR=/your/plugin/dir
```

### `Can't open shared library` 오류

`plugin_dir`이 올바른지 확인 후 MySQL 서버를 재시작해보세요:

```bash
sudo /usr/local/mysql/support-files/mysql.server restart
```
