UNAME := $(shell uname)

ifeq ($(UNAME), Darwin)
CC = $(CROSS_COMPILE)gcc -arch i386 -arch x86_64  -mmacosx-version-min=10.6
else
CC = $(CROSS_COMPILE)gcc
endif

AR = $(CROSS_COMPILE)ar
export CC
export AR

all:
	$(MAKE) -C parsers
	$(CC) -o cortexflash -I./ \
		main.c \
		utils.c \
		stm32.c \
		serial_common.c \
		serial_platform.c \
		stm32/stmreset_binary.c \
		parsers/*.o \
		-Wall

clean:
	$(MAKE) -C parsers clean
	rm -rf *.o

install: all
	cp cortexflash /usr/local/bin
