CC = cc
CFLAGS = -Wall -g
LIBS = -lavformat -lavcodec -lavutil -lswresample -lm -lpthread

INSTALL_PATH = /usr/bin

# Server: ALL source files needed
SERVER_SRC = src/main.c src/backend.c src/backend_utils.c src/control.c src/socket.c src/utils.c
SERVER_BIN = tomu

BINS = $(SERVER_BIN)

all: $(BINS)

$(SERVER_BIN): $(SERVER_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(CLI_BIN): $(CLI_SRC)
	$(CC) $(CFLAGS) -o $@ $^

install: all
	sudo install -m755 $(BINS) $(INSTALL_PATH)

uninstall:
	sudo rm -f $(addprefix $(INSTALL_PATH)/,$(BINS))

clean:
	rm -f $(BINS)

.PHONY: all install uninstall clean

