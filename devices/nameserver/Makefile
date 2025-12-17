CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include -I../common -g
TARGET = bin/ns

SRCS = src/*

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p bin
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET) -pthread
	@echo "Build complete: $(TARGET)"

clean:
	rm -rf bin

.PHONY: all clean
