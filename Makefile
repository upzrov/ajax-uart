CC = $(CROSS_COMPILE)gcc
CFLAGS = -Wall -Wextra -O2
TARGET = mycom
SRC = main.c

DEPLOY_USER ?= xrov
DEPLOY_HOST ?= 172.16.217.116
DEPLOY_PATH ?= /home/$(DEPLOY_USER)/

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET) *.log

deploy: $(TARGET)
	scp $(TARGET) $(DEPLOY_USER)@$(DEPLOY_HOST):$(DEPLOY_PATH)
