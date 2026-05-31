# Makefile for Aurc

LDFLAGS = -lcurl -larchive -ljson-c
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

# Source file
SRC = aurc.c

# Binary file
BIN = aurc

# Base location
PREFIX ?= /usr/

# Binary Folder
INSTALLPATH = ${DESTDIR}${PREFIX}/bin

# Compiler
CC = gcc

install : build

	@echo "installing binary file to ${INSTALLPATH}/${BIN}"
	@cp --remove-destination build/${BIN} ${INSTALLPATH}/${BIN}
	@chmod 0755 ${INSTALLPATH}/${BIN}
	@echo "installed"

# Clean build files
clean:
	rm -rf build

# Build the project
build:
	mkdir -p build
	$(CC) $(CFLAGS) -o build/$(BIN) $(SRC) $(LDFLAGS)


# Run the project
run: build
	./$(BIN)