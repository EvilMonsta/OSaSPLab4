CC = gcc
CFLAGS = -W -Wall -D_POSIX_C_SOURCE=200809L -Wextra -std=c11 -pedantic -Wno-unused-parameter -Wno-unused-variable
SRC_DIR = src
BUILD_DIR = build
TARGET = $(BUILD_DIR)/prod_cons


SRCS = $(SRC_DIR)/main.c


.PHONY: all clean run debug


all: $(BUILD_DIR) $(TARGET)


$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)


$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^


run: all
	./$(TARGET)


debug: CFLAGS += -g
debug: all


clean:
	rm -rf $(BUILD_DIR)