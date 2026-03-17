# ─────────────────────────────────────────────────────────────────────────────
# Makefile  —  my_resource MySQL 8.0 Daemon Plugin
#
# 사용법:
#   make setup    # MySQL 8.0.43 소스 헤더 다운로드 (최초 1회)
#   make          # 플러그인 빌드 → my_resource.so
#   make install  # MySQL plugin 디렉터리에 복사
#   make clean    # 빌드 산출물 제거
#   make distclean# 빌드 산출물 + 다운로드 헤더 제거
# ─────────────────────────────────────────────────────────────────────────────

# ── 도구 경로 ─────────────────────────────────────────────────────────────────
MYSQL_CONFIG  := /usr/local/mysql/bin/mysql_config
CXX           := clang++

# ── MySQL 버전 / 소스 헤더 ────────────────────────────────────────────────────
MYSQL_VERSION := 8.0.43
# MySQL 8.0.43 소스에서 include/ 만 git sparse-checkout 으로 받아 두는 디렉터리
MYSQL_SRC_DIR := $(CURDIR)/mysql-src-$(MYSQL_VERSION)
MYSQL_SRC_INC := $(MYSQL_SRC_DIR)/include

# 사용자가 직접 소스 경로를 지정한 경우 우선 사용
#   make MYSQL_SRC=/path/to/mysql-8.0.43/source
ifdef MYSQL_SRC
  MYSQL_SRC_INC := $(MYSQL_SRC)/include
endif

# ── mysql_config 로부터 컴파일/링크 플래그 ────────────────────────────────────
MYSQL_CFLAGS  := $(shell $(MYSQL_CONFIG) --cflags)
MYSQL_LIBS    := $(shell $(MYSQL_CONFIG) --libs)

# ── 컴파일 옵션 ───────────────────────────────────────────────────────────────
CXXFLAGS := \
    -std=c++17 \
    -fPIC \
    -O2 \
    -Wall \
    -Wextra \
    -fvisibility=hidden \
    -DMYSQL_DYNAMIC_PLUGIN \
    $(MYSQL_CFLAGS) \
    -I$(MYSQL_SRC_INC)

# ── 링크 옵션 (macOS) ─────────────────────────────────────────────────────────
#   -dynamiclib              : macOS 공유 라이브러리
#   -undefined dynamic_lookup: plugin API 심볼을 mysqld 로부터 런타임 해결
#   -lmysqlclient            : DB 지표(연결 수·쿼리 수) 조회에 필요
#   -Wl,-rpath,...           : .so 안에 libmysqlclient 절대 경로를 rpath 로 내장.
#                              mysqld 는 동일 install name 라이브러리를 이미 로드했으므로
#                              macOS dyld 가 중복 로드 없이 기존 인스턴스를 재사용한다.
#                              rpath 미지정 시 "@rpath/libmysqlclient.21.dylib" 해결
#                              불가로 "Library not loaded" 오류가 발생한다.
#   IOKit / CoreFoundation   : 디스크 I/O 모니터링 (macOS 프레임워크)
MYSQL_LIB_DIR := $(shell $(MYSQL_CONFIG) --variable=pkglibdir 2>/dev/null || echo /usr/local/mysql/lib)

LDFLAGS := \
    -dynamiclib \
    -undefined dynamic_lookup \
    -L$(MYSQL_LIB_DIR) \
    -lmysqlclient \
    -Wl,-rpath,$(MYSQL_LIB_DIR) \
    -framework IOKit \
    -framework CoreFoundation

# ── 출력 파일 ─────────────────────────────────────────────────────────────────
TARGET     := my_resource.so
SRC        := my_resource.cpp
PLUGIN_DIR := $(shell $(MYSQL_CONFIG) --plugindir)

# ─────────────────────────────────────────────────────────────────────────────
# 기본 타겟
# ─────────────────────────────────────────────────────────────────────────────
.PHONY: all setup install clean distclean check-headers

all: check-headers $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $<
	@echo ""
	@echo "✓  빌드 성공: $(TARGET)"
	@echo "   설치 명령: make install"

# ─────────────────────────────────────────────────────────────────────────────
# check-headers : plugin.h 와 sql 스텁 존재 확인
# ─────────────────────────────────────────────────────────────────────────────
check-headers:
	@if [ ! -f "$(MYSQL_SRC_INC)/mysql/plugin.h" ] || \
	    [ ! -f "$(MYSQL_SRC_INC)/sql/sql_plugin.h" ] || \
	    [ ! -f "$(MYSQL_SRC_INC)/my_config.h" ]; then \
	    echo ""; \
	    echo "ERROR: MySQL 서버 소스 헤더를 찾을 수 없습니다."; \
	    echo "  예상 경로: $(MYSQL_SRC_INC)/mysql/plugin.h"; \
	    echo ""; \
	    echo "  해결 방법 1 (자동 다운로드):"; \
	    echo "    make setup"; \
	    echo ""; \
	    echo "  해결 방법 2 (직접 지정):"; \
	    echo "    make MYSQL_SRC=/path/to/mysql-$(MYSQL_VERSION)-source"; \
	    echo ""; \
	    exit 1; \
	fi

# ─────────────────────────────────────────────────────────────────────────────
# setup : 헤더 다운로드 + sql/sql_plugin.h 스텁 생성
# ─────────────────────────────────────────────────────────────────────────────
setup: $(MYSQL_SRC_INC)/mysql/plugin.h \
       $(MYSQL_SRC_INC)/sql/sql_plugin.h \
       $(MYSQL_SRC_INC)/my_config.h
	@echo "✓  MySQL $(MYSQL_VERSION) 헤더 준비 완료: $(MYSQL_SRC_INC)"

$(MYSQL_SRC_INC)/mysql/plugin.h:
	@echo ""
	@echo "MySQL $(MYSQL_VERSION) 소스 헤더를 GitHub 에서 다운로드합니다 ..."
	@echo "(include/ 디렉터리만 sparse-checkout — 약 10~20 MB)"
	@echo ""
	@mkdir -p "$(MYSQL_SRC_DIR)"
	cd "$(MYSQL_SRC_DIR)" && \
	    git init -q && \
	    git remote add origin https://github.com/mysql/mysql-server.git && \
	    git config core.sparseCheckout true && \
	    echo "include/" > .git/info/sparse-checkout && \
	    git fetch --depth=1 origin mysql-$(MYSQL_VERSION) && \
	    git checkout FETCH_HEAD -- include/
	@echo ""
	@echo "✓  헤더 다운로드 완료: $(MYSQL_SRC_INC)"

# ─────────────────────────────────────────────────────────────────────────────
# sql/sql_plugin.h 최소 스텁 생성
#
# plugin.h 36번째 줄이 `#include "sql/sql_plugin.h"` 를 요구한다.
# 이 파일은 MySQL 서버 내부 헤더(sql/ 트리)에 있어 include/ 에는 없다.
# 데몬 플러그인은 THDVAR_SET 매크로를 사용하지 않으므로
# plugin_thdvar_safe_update 의 전방 선언만 있으면 컴파일 가능하다.
# ─────────────────────────────────────────────────────────────────────────────
$(MYSQL_SRC_INC)/sql/sql_plugin.h: $(MYSQL_SRC_INC)/mysql/plugin.h
	@mkdir -p "$(@D)"
	@printf '%s\n' \
	    '/* Auto-generated stub: sql/sql_plugin.h' \
	    '   Satisfies #include in include/mysql/plugin.h (THDVAR_SET macro).' \
	    '   Daemon plugins do not call THDVAR_SET, so a forward declaration suffices. */' \
	    '#ifndef SQL_SQL_PLUGIN_H_STUB' \
	    '#define SQL_SQL_PLUGIN_H_STUB' \
	    '#ifdef __cplusplus' \
	    'class THD;' \
	    'struct st_mysql_sys_var;' \
	    'extern "C" bool plugin_thdvar_safe_update(' \
	    '    THD *, struct st_mysql_sys_var *, void *, const void *);' \
	    '#endif /* __cplusplus */' \
	    '#endif /* SQL_SQL_PLUGIN_H_STUB */' \
	    > "$@"
	@echo "✓  sql/sql_plugin.h 스텁 생성: $@"

# ─────────────────────────────────────────────────────────────────────────────
# my_config.h 스텁 생성 (CMake 생성 파일 — 소스 트리에는 없음)
# macOS (arm64 / x86_64) LP64 모델 기준 값.
# ─────────────────────────────────────────────────────────────────────────────
$(MYSQL_SRC_INC)/my_config.h: $(MYSQL_SRC_INC)/mysql/plugin.h
	@printf '%s\n' \
	    '/* Auto-generated stub: my_config.h for macOS (LP64) */' \
	    '#ifndef MY_CONFIG_H' \
	    '#define MY_CONFIG_H' \
	    '' \
	    '/* Integer sizes — LP64 (macOS arm64 & x86_64) */' \
	    '#define SIZEOF_INT         4' \
	    '#define SIZEOF_LONG        8' \
	    '#define SIZEOF_LONG_LONG   8' \
	    '#define SIZEOF_VOIDP       8' \
	    '#define SIZEOF_OFF_T       8' \
	    '#define SIZEOF_SIZE_T      8' \
	    '#define SIZEOF_DOUBLE      8' \
	    '#define SIZEOF_FLOAT       4' \
	    '' \
	    '/* Standard POSIX headers (all present on macOS) */' \
	    '#define HAVE_ALLOCA_H      1' \
	    '#define HAVE_DLFCN_H       1' \
	    '#define HAVE_FCNTL_H       1' \
	    '#define HAVE_FLOAT_H       1' \
	    '#define HAVE_INTTYPES_H    1' \
	    '#define HAVE_LIMITS_H      1' \
	    '#define HAVE_MEMORY_H      1' \
	    '#define HAVE_PTHREAD_H     1' \
	    '#define HAVE_STDINT_H      1' \
	    '#define HAVE_STDLIB_H      1' \
	    '#define HAVE_STRING_H      1' \
	    '#define HAVE_STRINGS_H     1' \
	    '#define HAVE_SYS_TYPES_H   1' \
	    '#define HAVE_SYS_SOCKET_H  1' \
	    '#define HAVE_SYS_TIME_H    1' \
	    '#define HAVE_UNISTD_H      1' \
	    '#define HAVE_NETINET_IN_H  1' \
	    '#define HAVE_ARPA_INET_H   1' \
	    '#define HAVE_NETDB_H       1' \
	    '' \
	    '/* macOS does not expose ulong in system headers by default */' \
	    '/* #undef HAVE_ULONG */' \
	    '' \
	    '/* OpenSSL / zlib (present in MySQL binary distribution) */' \
	    '#define HAVE_OPENSSL       1' \
	    '' \
	    '/* MySQL paths */' \
	    '#define DEFAULT_MYSQL_HOME "/usr/local/mysql"' \
	    '#define MYSQL_DATADIR      "/usr/local/mysql/data"' \
	    '#define MYSQL_UNIX_ADDR    "/tmp/mysql.sock"' \
	    '' \
	    '#endif /* MY_CONFIG_H */' \
	    > "$@"
	@echo "✓  my_config.h 스텁 생성: $@"

# ─────────────────────────────────────────────────────────────────────────────
# install : MySQL plugin 디렉터리에 복사 (sudo 필요할 수 있음)
# ─────────────────────────────────────────────────────────────────────────────
install: $(TARGET)
	@echo "플러그인 디렉터리: $(PLUGIN_DIR)"
	cp $(TARGET) "$(PLUGIN_DIR)/$(TARGET)"
	@echo ""
	@echo "✓  복사 완료. MySQL 에서 아래 명령으로 플러그인을 등록하세요:"
	@echo ""
	@echo "   INSTALL PLUGIN my_resource SONAME 'my_resource.so';"
	@echo ""
	@echo "  (이미 로드된 경우) 재로드:"
	@echo "   UNINSTALL PLUGIN my_resource;"
	@echo "   INSTALL PLUGIN my_resource SONAME 'my_resource.so';"

# ─────────────────────────────────────────────────────────────────────────────
# clean / distclean
# ─────────────────────────────────────────────────────────────────────────────
clean:
	rm -f $(TARGET)

distclean: clean
	rm -rf $(MYSQL_SRC_DIR)
