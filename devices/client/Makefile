CC = gcc
CFLAGS = -Wall -Wextra -g -Iinclude -I../common
LDFLAGS = -lpthread

SRC_DIR = src
COMMON_DIR = ../common
BIN_DIR = .

# All source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)
TARGET = $(BIN_DIR)/app

# Default target
.PHONY: all clean run help

all: $(TARGET)

# Compile directly without object files
$(TARGET): $(SOURCES) | $(BIN_DIR)
	@echo "🔨 Compiling client..."
	$(CC) $(CFLAGS) $(SOURCES) -o $@ $(LDFLAGS)
	@echo "✓ Client built successfully at $(TARGET)"
	@echo ""

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	@echo "🧹 Cleaning client build files..."
	rm -f $(TARGET)
	@echo "✓ Cleaned successfully"

