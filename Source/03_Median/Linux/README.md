# my_median — MariaDB Aggregate UDF (Linux)

MariaDB용 Aggregate UDF로, `my_median(column)`을 통해 중앙값(median)을 계산합니다.
`GROUP BY`와 함께 사용하면 각 그룹별 중앙값을 구할 수 있습니다.

> **대상 환경:** Ubuntu / MariaDB

## 파일 구성

```
03_Median/Linux/
├── median.cpp (또는 my_median.cpp)  # UDF 소스 코드
├── Makefile                          # 빌드 스크립트
└── README.md
```

## 요구사항

- Linux (Ubuntu 권장)
- MariaDB (`mysql_config` 사용 가능한 환경)
- g++ (C++17 이상)

## 빌드 및 설치

```bash
# 컴파일
make

# 플러그인 디렉터리에 설치
make install
```

## MariaDB에 함수 등록

```sql
USE test;
DROP FUNCTION IF EXISTS my_median;
CREATE AGGREGATE FUNCTION my_median RETURNS REAL SONAME 'median.so';
```

## 사용 예시

### 단순 중앙값

```sql
-- 테이블 생성 및 데이터 삽입
CREATE TABLE test_scores (score DOUBLE);
INSERT INTO test_scores (score) VALUES (10), (20), (30), (40), (50);

-- 중앙값 계산 (결과: 30)
SELECT my_median(score) FROM test_scores;
-- +------------------+
-- | my_median(score) |
-- +------------------+
-- |               30 |
-- +------------------+

-- 짝수 개 (결과: 32.5)
INSERT INTO test_scores (score) VALUES (35);
SELECT my_median(score) FROM test_scores;
-- +------------------+
-- | my_median(score) |
-- +------------------+
-- |             32.5 |
-- +------------------+
```

### GROUP BY와 함께 사용

```sql
CREATE TABLE test_score2 (
    a VARCHAR(10),
    b DOUBLE
);

INSERT INTO test_score2(a, b) VALUES
    ('A', 10), ('A', 20), ('A', 30), ('A', 40),
    ('B', 110), ('B', 120), ('B', 30), ('B', 410);

SELECT a, my_median(b) FROM test_score2 GROUP BY a;
-- +------+--------------+
-- | a    | my_median(b) |
-- +------+--------------+
-- | A    |           25 |
-- | B    |          115 |
-- +------+--------------+
```

## 알고리즘

1. 각 로우의 값을 `std::vector<double>`에 누적
2. 그룹 종료 시 `std::sort`로 오름차순 정렬
3. 짝수 개: 중간 두 값의 평균, 홀수 개: 중간 값 반환
4. 빈 그룹: `NULL` 반환

## Aggregate UDF 라이프사이클

| 함수 | 호출 시점 |
|------|----------|
| `_init()` | 쿼리 시작 시 1회 — 메모리 할당 |
| `_clear()` | 각 그룹 시작 시 — 이전 값 초기화 |
| `_reset()` | 각 그룹의 첫 번째 로우 |
| `_add()` | 각 그룹의 두 번째~ 로우 — 값 누적 |
| 메인 함수 | 각 그룹 종료 시 — 결과 계산 |
| `_deinit()` | 쿼리 완료 후 1회 — 메모리 해제 |

> `_clear()`가 없으면 `GROUP BY` 사용 시 이전 그룹의 값이 누적되어 잘못된 결과가 반환됩니다.

## 함수 제거

```sql
DROP FUNCTION IF EXISTS my_median;
```
