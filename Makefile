CC = cc
CFLAGS = -Wall -g -O3 -Iinclude
LIBS = -lm -lpthread -lavformat -lavcodec -lavutil -lswresample

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
	$(CC) $(SERVER_OBJECTS) -o $@ $(CFLAGS) $(LIBS)

$(BUILD_DIR)/%.o: $(SERVER_SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) -c $< -o $@ $(CFLAGS) $(LIBS)

install: all
	sudo install -m755 $(BINS) $(INSTALL_PATH)

uninstall:
	sudo rm -f $(addprefix $(INSTALL_PATH)/,$(BINS))

clean:
	rm -rf $(BINS) $(BUILD_DIR)

.PHONY: all install uninstall clean
