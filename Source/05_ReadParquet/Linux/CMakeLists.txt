# CMakeLists.txt for READ_PARQUET storage engine plugin (MariaDB / Ubuntu 22.04)
#
# 이 파일은 MariaDB 빌드 시스템(cmake/plugin.cmake)의 CONFIGURE_PLUGINS()에 의해
# storage/ 하위 디렉터리 자동 탐색으로 포함됩니다.
#
# 사전 설치 패키지:
#   sudo apt install libarrow-dev libparquet-dev
#
# 설치:
#   INSTALL PLUGIN read_parquet SONAME 'ha_parquet.so';

# ─── Apache Arrow / Parquet 경로 (apt 설치 기준) ─────────────────────────────
# Ubuntu 22.04 에서 libarrow-dev / libparquet-dev 설치 시 기본 경로:
#   헤더: /usr/include/arrow/
#   라이브러리: /usr/lib/x86_64-linux-gnu/libarrow.so
#              /usr/lib/x86_64-linux-gnu/libparquet.so
#
# find_package(Arrow) 를 사용하지 않고 경로를 직접 지정합니다.
# 이유: Arrow cmake config 가 zstd 를 재탐색하면서 MariaDB 빌드 시스템이
#       이미 정의한 zstd cmake 타겟(zstd::libzstd_static 등)과 충돌합니다.

SET(ARROW_INCLUDE "/usr/include/arrow")
SET(ARROW_LIB    "/usr/lib/x86_64-linux-gnu/libarrow.so")
SET(PARQUET_LIB  "/usr/lib/x86_64-linux-gnu/libparquet.so")

# 라이브러리 존재 확인
FOREACH(LIB ${ARROW_LIB} ${PARQUET_LIB})
  IF(NOT EXISTS "${LIB}")
    MESSAGE(FATAL_ERROR
      "라이브러리를 찾을 수 없습니다: ${LIB}\n"
      "  sudo apt install libarrow-dev libparquet-dev")
  ENDIF()
ENDFOREACH()

# ─── MariaDB 플러그인 등록 ────────────────────────────────────────────────────
# MODULE_ONLY  : 동적 로드 전용 .so 로 빌드
# STORAGE_ENGINE : 스토리지 엔진 플러그인임을 명시
# LINK_LIBRARIES 에 cmake 타겟 대신 .so 경로를 직접 전달
#   → Arrow cmake config 를 로드하지 않으므로 zstd 타겟 충돌 없음
MYSQL_ADD_PLUGIN(read_parquet
  ha_parquet.cc
  MODULE_ONLY
  STORAGE_ENGINE
  LINK_LIBRARIES
    "${ARROW_LIB}"
    "${PARQUET_LIB}"
)

# C++17 필요 (Arrow headers) + Arrow 헤더 경로 추가
IF(TARGET read_parquet)
  TARGET_COMPILE_FEATURES(read_parquet PRIVATE cxx_std_20)
  TARGET_INCLUDE_DIRECTORIES(read_parquet PRIVATE "${ARROW_INCLUDE}")
ENDIF()
