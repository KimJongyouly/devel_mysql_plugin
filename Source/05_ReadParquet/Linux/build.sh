#!/usr/bin/env bash
# =============================================================================
# build.sh — READ_PARQUET 스토리지 엔진 빌드 스크립트 (MariaDB / Ubuntu 22.04)
#
# 사전 준비:
#   sudo apt install libarrow-dev libparquet-dev cmake g++ libssl-dev
#
# 사용법:
#   chmod +x build.sh
#   ./build.sh            # 빌드
#   ./build.sh clean      # 빌드 디렉터리 삭제 후 재빌드
#   ./build.sh install    # 빌드 후 MariaDB 플러그인 디렉터리에 복사
# =============================================================================
set -euo pipefail

# ─── 경로 설정 ────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MARIADB_SRC="/home/youly/Documents/Book/01_mysql_plugin/Source/mariadb-11.4.3"
BUILD_DIR="$SCRIPT_DIR/build_parquet"

# MariaDB 설치 경로 (바이너리 배포판)
MARIADB_HOME="/home/mariadb/mariadb_11.4.3"
MYSQL_CONFIG="$MARIADB_HOME/bin/mariadb-config"

# Apache Arrow (apt 설치 기준)
ARROW_LIB_DIR="/usr/lib/x86_64-linux-gnu"
ARROW_LIB="$ARROW_LIB_DIR/libarrow.so"
PARQUET_LIB="$ARROW_LIB_DIR/libparquet.so"

# 병렬 빌드 코어 수
JOBS=$(nproc 2>/dev/null || echo 4)

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
  command -v cmake &>/dev/null || error "cmake 가 없습니다.\n  sudo apt install cmake"

  # g++
  command -v g++ &>/dev/null || error "g++ 가 없습니다.\n  sudo apt install g++"

  # MariaDB 바이너리 설치 확인
  [[ -x "$MARIADB_HOME/bin/mariadb" ]] \
    || error "MariaDB 바이너리를 찾을 수 없습니다: $MARIADB_HOME/bin/mariadb"

  # MariaDB 서버 소스
  [[ -d "$MARIADB_SRC" ]] \
    || error "MariaDB 서버 소스가 없습니다: $MARIADB_SRC"

  # plugin 소스 심볼릭 링크 — storage/read_parquet 로 연결
  PLUGIN_LINK="$MARIADB_SRC/storage/read_parquet"
  if [[ ! -L "$PLUGIN_LINK" ]]; then
    info "심볼릭 링크 생성: $PLUGIN_LINK → $SCRIPT_DIR"
    ln -s "$SCRIPT_DIR" "$PLUGIN_LINK"
  fi

  # Apache Arrow / Parquet 라이브러리 확인
  [[ -f "$ARROW_LIB" ]] \
    || error "libarrow.so 를 찾을 수 없습니다: $ARROW_LIB\n  sudo apt install libarrow-dev"
  [[ -f "$PARQUET_LIB" ]] \
    || error "libparquet.so 를 찾을 수 없습니다: $PARQUET_LIB\n  sudo apt install libparquet-dev"

  # mariadb-config 버전 확인
  if [[ -x "$MYSQL_CONFIG" ]]; then
    MARIADB_VER=$("$MYSQL_CONFIG" --version)
    info "MariaDB 버전: $MARIADB_VER  (via $MYSQL_CONFIG)"
  else
    error "mariadb-config 를 찾을 수 없습니다: $MYSQL_CONFIG"
  fi

  info "의존성 확인 완료."
}

# =============================================================================
# cmake configure
# =============================================================================
configure() {
  info "CMake configure 시작..."
  info "  소스  : $MARIADB_SRC"
  info "  빌드  : $BUILD_DIR"
  info "  MariaDB : $MARIADB_HOME"

  mkdir -p "$BUILD_DIR"

  cmake -S "$MARIADB_SRC" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    \
    `# ── SSL / 압축 ─────────────────────────────────────────────────────────`\
    -DWITH_SSL=system \
    -DWITH_ZLIB=system \
    \
    `# ── 빌드 범위 최소화 ────────────────────────────────────────────────────`\
    -DWITH_UNIT_TESTS=OFF \
    \
    `# ── 불필요한 플러그인 비활성화 (소스 디렉터리 쓰기 충돌 방지) ────────────`\
    -DPLUGIN_MROONGA=NO \
    \
    `# ── 플러그인 명시적 활성화 ──────────────────────────────────────────────`\
    -DPLUGIN_READ_PARQUET=DYNAMIC \
    \
    `# ── 설치 경로 (바이너리 배포판 기준) ────────────────────────────────────`\
    -DCMAKE_INSTALL_PREFIX="$MARIADB_HOME"

  info "CMake configure 완료."
}

# =============================================================================
# 빌드 — 전체 서버 빌드 없이 플러그인만 컴파일+링크
#
# MODULE_ONLY 플러그인은 런타임에 mysqld 가 심볼을 제공하므로
# 빌드 시점에 MariaDB 라이브러리를 링크할 필요가 없습니다.
#
#   1단계: cmake 가 생성한 플래그로 .cc → .o 컴파일 (서버 빌드 없음)
#   2단계: .o + libarrow + libparquet 만으로 .so 생성
#          MariaDB 심볼은 런타임에 mysqld 프로세스에서 해석
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
  info "컴파일 중: ha_parquet.cc → .o ..."
  local BUILD_MAKE="storage/read_parquet/CMakeFiles/read_parquet.dir/build.make"
  local OBJ_TARGET="storage/read_parquet/CMakeFiles/read_parquet.dir/ha_parquet.cc.o"
  make -C "$BUILD_DIR" -f "$BUILD_MAKE" "$OBJ_TARGET" \
    || error "컴파일 실패"

  # ── 2단계: .o + Arrow/Parquet → .so  (MariaDB 심볼은 런타임 해석) ────────
  info "링크 중: .o + Arrow/Parquet → ha_parquet.so ..."
  mkdir -p "$BUILD_DIR/plugin_output_directory"
  local OUT_SO="$BUILD_DIR/plugin_output_directory/ha_parquet.so"
  g++ -shared -fPIC \
    -Wl,-soname,ha_parquet.so \
    -o "$OUT_SO" \
    "$OBJ_FILE" \
    "$ARROW_LIB" \
    "$PARQUET_LIB" \
    || error "링크 실패"

  cp "$OUT_SO" "$SCRIPT_DIR/ha_parquet.so"
  info "빌드 성공 → $SCRIPT_DIR/ha_parquet.so"
}

# =============================================================================
# MariaDB 플러그인 디렉터리에 복사
# =============================================================================
install_plugin() {
  local SO_FILE
  SO_FILE=$(find "$BUILD_DIR" -name "ha_parquet.so" 2>/dev/null | head -1)
  [[ -n "$SO_FILE" ]] || SO_FILE="$SCRIPT_DIR/ha_parquet.so"
  [[ -f "$SO_FILE" ]]  || error "먼저 빌드를 실행하세요: ./build.sh"

  # mariadb-config 로 플러그인 경로 조회 (바이너리 배포판 기준)
  local DEST
  DEST=$("$MYSQL_CONFIG" --plugindir 2>/dev/null || echo "")

  [[ -n "$DEST" && -d "$DEST" ]] \
    || error "플러그인 디렉터리를 찾을 수 없습니다.\n  수동으로 복사하세요: cp $SO_FILE $MARIADB_HOME/lib/plugin/"

  info "플러그인 복사: $SO_FILE → $DEST"
  cp "$SO_FILE" "$DEST/"
  info "설치 완료!"
  echo ""
  echo "MariaDB 에서 다음 명령으로 등록하세요:"
  echo "  INSTALL PLUGIN read_parquet SONAME 'ha_parquet.so';"
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
       [[ "$SCRIPT_DIR/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ]] || \
       [[ ! -f "$BUILD_DIR/storage/read_parquet/CMakeFiles/read_parquet.dir/flags.make" ]]; then
      configure
    fi
    build
    install_plugin
    ;;
  build|*)
    check_deps
    # CMakeCache.txt 없거나, CMakeLists.txt 가 cache 보다 새로우면 재설정
    # 플러그인 cmake 파일이 없어도 재설정 (CMakeCache.txt 만 있고 플러그인 미포함 상태 대응)
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]] || \
       [[ "$SCRIPT_DIR/CMakeLists.txt" -nt "$BUILD_DIR/CMakeCache.txt" ]] || \
       [[ ! -f "$BUILD_DIR/storage/read_parquet/CMakeFiles/read_parquet.dir/flags.make" ]]; then
      configure
    fi
    build
    ;;
esac
