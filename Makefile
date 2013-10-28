CC = $(CROSS_COMPILE)gcc
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
		parsers/parsers.a \
		stm32/stmreset_binary.c \
		-Wall

clean:
	$(MAKE) -C parsers clean
	rm -rf *.o

install: all
	cp cortexflash /usr/local/bin
