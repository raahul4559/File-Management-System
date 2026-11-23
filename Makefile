# Makefile for Web-Based File Management System (macOS / Linux)

CC       = clang
CFLAGS   = -Wall -Wextra -O2 -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS  = 

SRC_DIR  = backend
SRCS     = $(SRC_DIR)/server.c $(SRC_DIR)/file_operations.c
TARGET   = filemanager

.PHONY: all clean run dirs

all: dirs $(TARGET)

dirs:
	mkdir -p storage frontend

$(TARGET): $(SRCS) $(SRC_DIR)/file_operations.h
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
	rm -rf storage/*

# Quick test of file operations only
test-fo: $(SRC_DIR)/file_operations.c $(SRC_DIR)/file_operations.h
	$(CC) $(CFLAGS) -o test_fo $(SRC_DIR)/file_operations.c -DTEST_FO_MAIN
	@echo "Note: add a main() in a separate test file if needed"
