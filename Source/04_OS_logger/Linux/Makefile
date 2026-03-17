# 경로 설정
MARIADB_BASE = /home/mariadb/mariadb_11.4.3
INCLUDES     = -I$(MARIADB_BASE)/include/server/mysql \
               -I$(MARIADB_BASE)/include/mysql/server

PLUGIN_DIR   = $(MARIADB_BASE)/lib/plugin

# 컴파일 옵션
CXX      = g++
CXXFLAGS = -fPIC -Wall -DMYSQL_DYNAMIC_PLUGIN $(INCLUDES)
LDFLAGS  = -shared -lpthread
TARGET   = os_logger.so

all: $(TARGET)

$(TARGET): os_logger.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $(TARGET) os_logger.cpp

install: $(TARGET)
	sudo cp $(TARGET) $(PLUGIN_DIR)
	@echo ""
	@echo "플러그인 설치 완료. MariaDB에서 아래 명령을 실행하세요:"
	@echo "  INSTALL PLUGIN os_logger SONAME 'os_logger.so';"
	@echo ""
	@echo "플러그인 제거 시:"
	@echo "  UNINSTALL PLUGIN os_logger;"

clean:
	rm -f $(TARGET)
