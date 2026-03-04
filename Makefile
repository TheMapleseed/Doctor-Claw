# Doctor Claw - Makefile for Clang C23 compilation
# Zero overhead. Zero compromise. 100% C.
#
# Requires ISO C23 (-std=c23). C23 is ISO/IEC 9899:2024, __STDC_VERSION__ 202311L.

CC = clang
CXX = clang++
CFLAGS = -std=c23 -Wall -Wextra -Wpedantic -g
CFLAGS += -Iinclude -Isrc
CFLAGS += -fdebug-prefix-map=$(SRC_DIR)=.
CFLAGS += -fno-omit-frame-pointer
# Reject pre-C23 compatibility extensions when possible (optional)
# CFLAGS += -Wpre-c23-compat

RELEASE_CFLAGS = -O3 -DNDEBUG
DEBUG_CFLAGS = -O0 -g -DDEBUG

LDFLAGS = -lm -lpthread
LDFLAGS += -lcurl
LDFLAGS += -lsqlite3
LDFLAGS += -ldl

SRC_DIR = src
INC_DIR = include
LIB_DIR = lib
OBJ_DIR = obj
BIN_DIR = bin

SOURCES = $(shell find $(SRC_DIR) -name '*.c')
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

TARGET = $(BIN_DIR)/doctorclaw

.PHONY: all clean deps dirs

all: dirs $(TARGET)

dirs:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

run: $(TARGET)
	./$(TARGET) $$(args)

dev: clean all

check: $(TARGET)
	./$(TARGET) --help
