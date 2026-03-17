# hello_world — MariaDB UDF (Linux)

MariaDB User Defined Function (UDF) 플러그인 예제입니다.
`SELECT hello_world();` 호출 시 문자열 `"Hello World from UDF!"`를 반환합니다.

> **대상 환경:** Ubuntu / MariaDB 11.4.3

## 파일 구성

```
01_HelloWorld/Linux/
├── udf_hello_world.cpp   # UDF 소스 코드
├── compile.sh            # 빌드 스크립트
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
g++ -fPIC -shared -o udf_hello_world.so udf_hello_world.cpp \
    $(/home/mariadb/mariadb_11.4.3/bin/mysql_config --cflags --libs)
```

빌드 결과: `udf_hello_world.so`

## 설치

플러그인 디렉터리 확인:

```bash
/home/mariadb/mariadb_11.4.3/bin/mariadb -u root -p \
    -e "SHOW VARIABLES LIKE 'plugin_dir';"
```

플러그인 복사 및 권한 설정:

```bash
sudo cp udf_hello_world.so /home/mariadb/mariadb_11.4.3/lib/plugin/
sudo chown mariadb:mariadb /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world.so
sudo chmod 755 /home/mariadb/mariadb_11.4.3/lib/plugin/udf_hello_world.so
```

## MariaDB에 함수 등록

MariaDB 클라이언트에서 실행:

```sql
USE test;
CREATE FUNCTION hello_world RETURNS STRING SONAME 'udf_hello_world.so';
```

## 사용

```sql
SELECT hello_world();
-- 결과: Hello World from UDF!
```

## 함수 제거

```sql
DROP FUNCTION hello_world;
```

## UDF 구조

| 함수 | 역할 |
|------|------|
| `hello_world_init()` | 인수 검증 및 반환값 메타정보 설정 |
| `hello_world()` | 문자열 `"Hello World from UDF!"` 반환 |
| `hello_world_deinit()` | 리소스 해제 |

## 주의사항

- MariaDB `mysql_config` 경로는 환경에 따라 다를 수 있습니다. `compile.sh`의 경로를 확인하세요.
- `plugin_dir` 경로가 다른 경우 설치 경로를 맞게 수정하세요.
