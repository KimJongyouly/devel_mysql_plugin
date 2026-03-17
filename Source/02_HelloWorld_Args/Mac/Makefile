CXX      = g++
TARGET   = helloworld_udf.so
SRC      = helloworld_udf.cpp

MYSQL_CONFIG  = /usr/local/mysql/bin/mysql_config
MYSQL_CFLAGS := $(shell $(MYSQL_CONFIG) --cflags)
MYSQL_PLUGINDIR := $(shell $(MYSQL_CONFIG) --plugindir)

# macOS / Linux 공통 플래그
CXXFLAGS = $(MYSQL_CFLAGS) -fPIC -O2 -Wall

# macOS는 -dynamiclib, Linux는 -shared
UNAME := $(shell uname)
ifeq ($(UNAME), Darwin)
    SHARED_FLAG = -dynamiclib
else
    SHARED_FLAG = -shared
endif

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(SHARED_FLAG) $(CXXFLAGS) -o $@ $<

install: $(TARGET)
	cp $(TARGET) $(MYSQL_PLUGINDIR)/
	@echo "Installed to $(MYSQL_PLUGINDIR)/$(TARGET)"
	@echo ""
	@echo "Run in MySQL:"
	@echo "  CREATE FUNCTION helloworld RETURNS STRING SONAME '$(TARGET)';"
	@echo ""
	@echo "Usage:"
	@echo "  SELECT helloworld('YourName');"

uninstall:
	rm -f $(MYSQL_PLUGINDIR)/$(TARGET)
	@echo "Uninstalled $(TARGET)"

clean:
	rm -f $(TARGET)
