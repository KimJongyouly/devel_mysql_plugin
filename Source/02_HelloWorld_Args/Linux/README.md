# hello_world (with args) — MariaDB UDF (Linux)

MariaDB User Defined Function (UDF) 플러그인 예제입니다.
`SELECT hello_world('name');` 호출 시 `"Hello World, {name}!"` 형식의 문자열을 반환합니다.

> **대상 환경:** Ubuntu / MariaDB 11.4.3

## 파일 구성

```
02_HelloWorld_Args/Linux/
├── udf_hello_world_args.cpp   # UDF 소스 코드
├── compile.sh                 # 빌드 스크립트
└── README.md
```

## 요구사항

- Linux (Ubuntu 권장)
- MariaDB 11.4.3 (`/home/mariadb/mariadb_11.4.3`)
- g++

## 빌드

```bash
bash compile.sh
```

또는 직접 실행:

```bash
g++ -fPIC -shared -o udf_hello_world_args.so udf_hello_world_args.cpp \
    $(/home/mariadb/mariadb_11.4.3/bin/mysql_config --cflags --libs)
```

빌드 결과: `udf_hello_world_args.so`

## 설치

플러그인 디렉터리 확인:

```bash
/home/mariadb/mariadb_11.4.3/bin/mariadb -u root -p \
    -e "SHOW VARIABLES LIKE 'plugin_dir';"
```

플러그인 복사 및 권한 설정:

```bash
sudo cp udf_hello_world_args.so /home/mariadb/mariadb_11.4.3/lib/plugin/
sudo chown mariadb:mariadb /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world_args.so
sudo chmod 755 /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world_args.so
```

## MariaDB에 함수 등록

```sql
USE test;
CREATE FUNCTION hello_world RETURNS STRING SONAME 'udf_hello_world_args.so';
```

## 사용

```sql
SELECT hello_world('youly');
-- 결과: Hello World, youly!

-- 테이블 컬럼에 적용
CREATE TABLE test1 (a VARCHAR(10));
INSERT INTO test1(a) VALUES ('a1'), ('a2'), ('a3');

SELECT hello_world(a) FROM test1;
-- 결과:
-- +------------------+
-- | hello_world(a)   |
-- +------------------+
-- | Hello World, a1! |
-- | Hello World, a2! |
-- | Hello World, a3! |
-- +------------------+
```

## 함수 제거

```sql
DROP FUNCTION hello_world;
```

## UDF 구조

| 함수 | 역할 |
|------|------|
| `hello_world_init()` | 인수 검증 (문자열 1개 필수) 및 반환값 메타정보 설정 |
| `hello_world()` | `"Hello World, {name}!"` 문자열 반환 |
| `hello_world_deinit()` | 동적 할당 버퍼 해제 |

## 인자 처리

- 문자열(`STRING_RESULT`) 인수 1개가 필수입니다. 없거나 다른 타입이면 오류가 발생합니다.
- `NULL`을 전달하면 `NULL`을 반환합니다.

## 주의사항

- MariaDB `mysql_config` 경로는 환경에 따라 다를 수 있습니다. `compile.sh`의 경로를 확인하세요.
