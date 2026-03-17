MYSQL_CONFIG   := /usr/local/mysql/bin/mysql_config
MYSQL_CFLAGS   := $(shell $(MYSQL_CONFIG) --cflags)
MYSQL_PLUGIN_DIR := $(shell $(MYSQL_CONFIG) --plugindir)

CXX      := g++
CXXFLAGS := -shared -fPIC -O2 -std=c++17 $(MYSQL_CFLAGS)

TARGET := my_median.so
SRC    := my_median.cpp

.PHONY: all clean install uninstall reload

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

# MySQL 플러그인 디렉터리에 복사하고 UDF 등록
install: $(TARGET)
	sudo cp $(TARGET) $(MYSQL_PLUGIN_DIR)/
	mysql -u root -p -e "DROP FUNCTION IF EXISTS my_median; CREATE AGGREGATE FUNCTION my_median RETURNS REAL SONAME 'my_median.so';"

# .so 파일은 그대로 두고 MySQL에서만 DROP→CREATE (메모리 리로드)
reload:
	mysql -u root -p -e "DROP FUNCTION IF EXISTS my_median; CREATE AGGREGATE FUNCTION my_median RETURNS REAL SONAME 'my_median.so';"

# UDF 해제 및 플러그인 파일 삭제
uninstall:
	mysql -u root -p -e "DROP FUNCTION IF EXISTS my_median;"
	sudo rm -f $(MYSQL_PLUGIN_DIR)/$(TARGET)

clean:
	rm -f $(TARGET)
