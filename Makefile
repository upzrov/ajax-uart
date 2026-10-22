CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2
TARGET = mycom
SRC = main.c
SERVICE = mycom.service

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SYSTEMD_DIR ?= /etc/systemd/system

DEPLOY_USER ?= xrov
DEPLOY_HOST ?= raspberrypi.local
DEPLOY_PATH ?= /home/$(DEPLOY_USER)/

.PHONY: all clean deploy install uninstall install-remote start stop restart status

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.log

deploy: $(TARGET) $(SERVICE)
	@if [ -z "$(DEPLOY_HOST)" ]; then \
		echo "Error: DEPLOY_HOST is not set. Usage: make deploy DEPLOY_HOST=<ip_or_hostname>"; \
		exit 1; \
	fi
	scp $(TARGET) $(SERVICE) Makefile $(DEPLOY_USER)@$(DEPLOY_HOST):$(DEPLOY_PATH)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/$(TARGET)
	install -d $(DESTDIR)$(SYSTEMD_DIR)
	install -m 644 $(SERVICE) $(DESTDIR)$(SYSTEMD_DIR)/$(SERVICE)
	systemctl daemon-reload
	systemctl enable $(SERVICE)

uninstall:
	-systemctl stop $(SERVICE)
	-systemctl disable $(SERVICE)
	rm -f $(DESTDIR)$(SYSTEMD_DIR)/$(SERVICE)
	systemctl daemon-reload
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)

install-remote: deploy
	ssh -t $(DEPLOY_USER)@$(DEPLOY_HOST) "cd $(DEPLOY_PATH) && sudo make install"

start:
	systemctl start $(SERVICE)

stop:
	systemctl stop $(SERVICE)

restart:
	systemctl restart $(SERVICE)

status:
	systemctl status $(SERVICE)
