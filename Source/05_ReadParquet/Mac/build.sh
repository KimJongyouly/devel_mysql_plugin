#!/usr/bin/env bash
# =============================================================================
# build.sh — READ_PARQUET 스토리지 엔진 빌드 스크립트
#
# 사용법:
#   chmod +x build.sh
#   ./build.sh            # 빌드
#   ./build.sh clean      # 빌드 디렉터리 삭제 후 재빌드
#   ./build.sh install    # 빌드 후 MySQL 플러그인 디렉터리에 복사
# =============================================================================
set -euo pipefail

# ─── 경로 설정 ────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MYSQL_SRC="$SCRIPT_DIR/mysql-server"
BUILD_DIR="$SCRIPT_DIR/build_parquet"
BOOST_DIR="$MYSQL_SRC/boost"

# mysql_config 위치 (공식 DMG 설치 기준)
MYSQL_CONFIG="/usr/local/mysql/bin/mysql_config"

# Apache Arrow (brew 설치 기준) — cmake config 경로는 CMakeLists.txt 에서 직접 처리
ARROW_PREFIX="$(brew --prefix apache-arrow 2>/dev/null || echo '')"

# 병렬 빌드 코어 수
JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ─── 색상 출력 ────────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# =============================================================================
# 사전 조건 확인
# =============================================================================
check_deps() {
  info "의존성 확인 중..."

  # cmake
  command -v cmake &>/dev/null || error "cmake 가 없습니다. brew install cmake"

  # MySQL 서버 소스
  [[ -d "$MYSQL_SRC" ]] \
    || error "mysql-server 소스가 없습니다: $MYSQL_SRC\n  git clone https://github.com/mysql/mysql-server.git $MYSQL_SRC"

  # plugin 소스 — 05_Engine/ 을 read_parquet 로 심볼릭 링크
  # 복사 없이 05_Engine/ 에서 바로 빌드됩니다.
  PLUGIN_LINK="$MYSQL_SRC/storage/read_parquet"
  if [[ ! -L "$PLUGIN_LINK" ]]; then
    info "심볼릭 링크 생성: $PLUGIN_LINK → $SCRIPT_DIR"
    ln -s "$SCRIPT_DIR" "$PLUGIN_LINK"
  fi

  # Apache Arrow 라이브러리 존재 확인
  [[ -f "$ARROW_PREFIX/lib/libarrow.dylib" ]] \
    || error "Apache Arrow 를 찾을 수 없습니다: $ARROW_PREFIX\n  brew install apache-arrow"

  # OpenSSL (MySQL 빌드 필수)
  command -v openssl &>/dev/null || warn "openssl 이 PATH에 없습니다."

  # mysql_config (선택 — 버전 확인용)
  if [[ -x "$MYSQL_CONFIG" ]]; then
    MYSQL_VER=$("$MYSQL_CONFIG" --version)
    info "MySQL 버전: $MYSQL_VER  (via $MYSQL_CONFIG)"
  else
    warn "mysql_config 를 찾을 수 없습니다 ($MYSQL_CONFIG) — 계속 진행합니다."
  fi

  info "의존성 확인 완료."
}

# =============================================================================
# cmake configure
# =============================================================================
configure() {
  info "CMake configure 시작..."
  info "  소스  : $MYSQL_SRC"
  info "  빌드  : $BUILD_DIR"
  info "  Arrow : $ARROW_PREFIX"

  mkdir -p "$BUILD_DIR"
  mkdir -p "$BOOST_DIR"

  # OpenSSL 경로 (brew)
  OPENSSL_ROOT="$(brew --prefix openssl@3 2>/dev/null \
                  || brew --prefix openssl  2>/dev/null \
                  || echo '/usr/local')"

  cmake -S "$MYSQL_SRC" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    \
    `# ── SSL / 압축 ─────────────────────────────────────────────────────────`\
    -DWITH_SSL=system \
    -DOPENSSL_ROOT_DIR="$OPENSSL_ROOT" \
    -DWITH_ZLIB=bundled \
    -DWITH_LZ4=system \
    -DWITH_ZSTD=bundled \
    \
    `# ── Boost (없으면 자동 다운로드) ───────────────────────────────────────`\
    -DDOWNLOAD_BOOST=ON \
    -DWITH_BOOST="$BOOST_DIR" \
    \
    `# ── 빌드 범위 최소화 ────────────────────────────────────────────────────`\
    -DWITH_UNIT_TESTS=OFF \
    -DWITH_ROUTER=OFF \
    -DWITH_NDB=OFF \
    -DWITHOUT_SERVER=OFF \
    -DWITH_MYSQLX=OFF \
    -DWITH_PROTOBUF=bundled \
    -DCMAKE_DISABLE_FIND_PACKAGE_absl=ON \
    \
    `# ── 플러그인 명시적 활성화 ──────────────────────────────────────────────`\
    -DWITH_READ_PARQUET=YES

  info "CMake configure 완료."
}

# =============================================================================
# 빌드 — 전체 서버 빌드 없이 플러그인만 컴파일+링크
#
# MODULE_ONLY 플러그인은 런타임에 mysqld 가 심볼을 제공하므로
# 빌드 시점에 MySQL 라이브러리를 링크할 필요가 없습니다.
#
#   1단계: cmake 가 생성한 플래그로 .cc → .o 컴파일 (서버 빌드 없음)
#   2단계: .o + libarrow + libparquet 만으로 .so 생성
#          MySQL 심볼은 -undefined dynamic_lookup 으로 런타임 해석
# =============================================================================
build() {
  local OBJ_DIR="$BUILD_DIR/storage/read_parquet/CMakeFiles/read_parquet.dir"
  local OBJ_FILE="$OBJ_DIR/ha_parquet.cc.o"
  local FLAGS_FILE="$OBJ_DIR/flags.make"

  [[ -f "$FLAGS_FILE" ]] \
    || error "cmake configure 결과물이 없습니다 ($FLAGS_FILE). ./build.sh clean 을 실행하세요."

  # ── 0단계: 필수 생성 헤더 빌드 (mysqld_error.h 등) ──────────────────────
  if [[ ! -f "$BUILD_DIR/include/mysqld_error.h" ]]; then
    info "헤더 생성 중: GenError ..."
    make -C "$BUILD_DIR" GenError -j"$JOBS" \
      || error "GenError 빌드 실패"
  fi

  # ── 1단계: ha_parquet.cc → .o  ────────────────────────────────────────────
  # build.make 를 직접 지정하여 플러그인 .o 파일만 컴파일합니다.
  # build.make 는 flags.make 를 include 하므로 make 의 변수 확장을 통해
  # 플래그가 올바르게 전달됩니다 (bash 문자열 파싱 문제 없음).
  info "컴파일 중: ha_parquet.cc → .o ..."
  local BUILD_MAKE="storage/read_parquet/CMakeFiles/read_parquet.dir/build.make"
  local OBJ_TARGET="storage/read_parquet/CMakeFiles/read_parquet.dir/ha_parquet.cc.o"
  make -C "$BUILD_DIR" -f "$BUILD_MAKE" "$OBJ_TARGET" \
    || error "컴파일 실패"

  # ── 2단계: .o + Arrow/Parquet → .so  (MySQL 심볼은 런타임 해석) ─────────
  info "링크 중: .o + Arrow/Parquet → ha_read_parquet.so ..."
  mkdir -p "$BUILD_DIR/plugin_output_directory"
  local OUT_SO="$BUILD_DIR/plugin_output_directory/ha_read_parquet.so"
  clang++ -dynamiclib \
    -Wl,-headerpad_max_install_names \
    -undefined dynamic_lookup \
    -arch arm64 \
    -o "$OUT_SO" \
    "$OBJ_FILE" \
    "$ARROW_PREFIX/lib/libarrow.dylib" \
    "$ARROW_PREFIX/lib/libparquet.dylib" \
    || error "링크 실패"

  cp "$OUT_SO" "$SCRIPT_DIR/ha_read_parquet.so"
  info "빌드 성공 → $SCRIPT_DIR/ha_read_parquet.so"
}

# =============================================================================
# MySQL 플러그인 디렉터리에 복사
# =============================================================================
install_plugin() {
  SO_FILE=$(find "$BUILD_DIR" \( -name "ha_parquet.so" -o -name "ha_parquet.dylib" \
                                -o -name "ha_read_parquet.so" -o -name "ha_read_parquet.dylib" \) \
            2>/dev/null | head -1)
  [[ -n "$SO_FILE" ]] || error "먼저 빌드를 실행하세요: ./build.sh"

  # 플러그인 디렉터리 탐색 순서
  PLUGIN_DIRS=(
    "/usr/local/mysql/lib/plugin"
    "$(brew --prefix mysql)/lib/plugin 2>/dev/null"
  )

  DEST=""
  for d in "${PLUGIN_DIRS[@]}"; do
    if [[ -d "$d" ]]; then
      DEST="$d"
      break
    fi
  done

  if [[ -z "$DEST" ]]; then
    # mysql_config 로 플러그인 경로 조회
    if [[ -x "$MYSQL_CONFIG" ]]; then
      DEST=$("$MYSQL_CONFIG" --plugindir 2>/dev/null || echo "")
    fi
  fi

  [[ -n "$DEST" ]] || error "MySQL 플러그인 디렉터리를 찾을 수 없습니다. 수동으로 복사하세요."

  info "플러그인 복사: $SO_FILE → $DEST"
  cp "$SO_FILE" "$DEST/"
  info "설치 완료!"
  echo ""
  echo "MySQL에서 다음 명령으로 등록하세요:"
  echo "  INSTALL PLUGIN read_parquet SONAME '$(basename "$SO_FILE")';"
}

# =============================================================================
# 메인
# =============================================================================
ACTION="${1:-build}"

case "$ACTION" in
  clean)
    warn "빌드 디렉터리 삭제: $BUILD_DIR"
    rm -rf "$BUILD_DIR"
    check_deps
    configure
    build
    ;;
  install)
    check_deps
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] || \
       [[ "$SCRIPT_DIR/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ]]; then
      configure
    fi
    build
    install_plugin
    ;;
  build|*)
    check_deps
    # CMakeCache.txt 없거나, CMakeLists.txt 가 cache 보다 새로우면 재설정
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] || \
       [[ "$SCRIPT_DIR/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ]]; then
      configure
    fi
    build
    ;;
esac
