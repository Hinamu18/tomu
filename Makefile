CC = cc
CFLAGS = -Wall -g -O3 -Iinclude
LIBS = -lavformat -lavcodec -lavutil -lswresample -lm -lpthread

INSTALL_PATH = /usr/bin

SERVER_SRC_DIR := src
BUILD_DIR := build

# Server: ALL source files needed
SERVER_SOURCES := $(wildcard $(SERVER_SRC_DIR)/*.c) # for clients, the source should better exist in a separate directory from the server
SERVER_OBJECTS := $(patsubst $(SERVER_SRC_DIR)/%, $(BUILD_DIR)/%, $(SERVER_SOURCES:.c=.o))

SERVER_BIN = tomu

BINS = $(SERVER_BIN)

all: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(LIBS) $(SERVER_OBJECTS) -o $@

$(BUILD_DIR)/%.o: $(SERVER_SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

install: all
	sudo install -m755 $(BINS) $(INSTALL_PATH)

uninstall:
	sudo rm -f $(addprefix $(INSTALL_PATH)/,$(BINS))

clean:
	rm -f $(BINS)

.PHONY: all install uninstall clean
