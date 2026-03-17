# MariaDB 설정 경로
MYSQL_CONFIG = /home/mariadb/mariadb_11.4.3/bin/mysql_config
TARGET = median.so
SRC = median.cpp

# 중요: 위 쿼리로 확인한 플러그인 경로를 정확히 입력하세요.
PLUGIN_DIR = /home/mariadb/mariadb_11.4.3/lib/plugin

# MariaDB 프로세스를 실행하는 사용자와 그룹 (보통 mysql 또는 mariadb)
# 시스템 설정에 따라 다를 수 있으니 확인이 필요합니다.
OWNER = mariadb:mariadb

all:
	g++ -fPIC -shared -o $(TARGET) $(SRC) $(shell $(MYSQL_CONFIG) --cflags --libs)

install:
	@echo "Installing $(TARGET) to $(PLUGIN_DIR)..."
	sudo cp $(TARGET) $(PLUGIN_DIR)
	sudo chown $(OWNER) $(PLUGIN_DIR)/$(TARGET)
	sudo chmod 755 $(PLUGIN_DIR)/$(TARGET)
	@echo "Installation complete."

clean:
	rm -f $(TARGET)
