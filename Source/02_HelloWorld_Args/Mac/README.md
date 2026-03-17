# helloworld MySQL UDF

MySQL User Defined Function (UDF) 플러그인 예제입니다.
`SELECT helloworld('name');` 호출 시 `"Welcome name, HelloWorld Mysql Plugin"` 형식의 문자열을 반환합니다.

## 파일 구성

```
helloworld/
├── helloworld_udf.cpp   # UDF 소스 코드
├── Makefile             # 빌드 스크립트
└── README.md
```

## 요구사항

- macOS (Apple Silicon / Intel) 또는 Linux
- MySQL 설치 (`/usr/local/mysql`)

## 빌드

```bash
make
```

빌드 결과: `helloworld_udf.so`

> `mysql_config` 경로는 Makefile의 `MYSQL_CONFIG` 변수에 고정되어 있습니다 (`/usr/local/mysql/bin/mysql_config`).

## 설치

```bash
sudo make install
```

또는 수동으로 MySQL 플러그인 디렉토리에 복사:

```bash
cp helloworld_udf.so $(/usr/local/mysql/bin/mysql_config --plugindir)/
```

## MySQL에 함수 등록

MySQL 클라이언트에서 실행:

```sql
CREATE FUNCTION helloworld RETURNS STRING SONAME 'helloworld_udf.so';
```

## 사용

```sql
SELECT helloworld('Alice');
-- 결과: Welcome Alice, HelloWorld Mysql Plugin

SELECT helloworld('홍길동');
-- 결과: Welcome 홍길동, HelloWorld Mysql Plugin
```

## 함수 제거

MySQL에서 등록 해제:

```sql
DROP FUNCTION IF EXISTS helloworld;
```

플러그인 파일 삭제:

```bash
sudo make uninstall
```

## UDF 구조

| 함수 | 역할 |
|------|------|
| `helloworld_init()` | 인수 검증 (문자열 1개 필수) 및 반환값 메타정보 설정 |
| `helloworld()` | `"Welcome {name}, HelloWorld Mysql Plugin"` 문자열 반환 |
| `helloworld_deinit()` | 동적 할당 버퍼 해제 |

## 주의사항

- `helloworld()`는 문자열 인수 1개를 반드시 받아야 합니다. 인수가 없거나 다른 타입이면 오류가 발생합니다.
- `NULL`을 전달하면 `NULL`을 반환합니다.
- MySQL 서버 재시작 후에도 `CREATE FUNCTION` 등록 정보는 유지됩니다.
- 플러그인 파일을 교체할 경우, `DROP FUNCTION` 후 재등록이 필요합니다.
