# Makefile for Storage Server

CC = gcc
CFLAGS = -Wall -Wextra -pthread -I./include -I../common -g
LDFLAGS = -pthread

all:
	@mkdir -p bin
	$(CC) $(CFLAGS) \
		src/* \
		-o bin/ss $(LDFLAGS)
	@echo "Build complete: bin/ss"

clean:
	@rm -rf $(BIN_DIR)
	@echo "Clean complete"

run:
	./bin/ss ./storage_data 8001

.PHONY: all clean run