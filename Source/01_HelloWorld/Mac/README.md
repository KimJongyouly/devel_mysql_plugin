# helloworld MySQL UDF

MySQL User Defined Function (UDF) 플러그인 예제입니다.
`SELECT helloworld();` 호출 시 문자열 `"Hello World Mysql Plugin-UDF"`를 반환합니다.

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
SELECT helloworld();
-- 결과: Hello World Mysql Plugin-UDF
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
| `helloworld_init()` | 인수 검증 및 반환값 메타정보 설정 |
| `helloworld()` | 실제 문자열 반환 |
| `helloworld_deinit()` | 리소스 해제 (이 예제에서는 없음) |

## 주의사항

- `helloworld()`는 인수를 받지 않습니다. 인수를 전달하면 오류가 발생합니다.
- MySQL 서버 재시작 후에도 `CREATE FUNCTION` 등록 정보는 유지됩니다.
- 플러그인 파일을 교체할 경우, `DROP FUNCTION` 후 재등록이 필요합니다.
