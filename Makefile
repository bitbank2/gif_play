CFLAGS=-c -Wall -O3
# By default on linux, `gp` will default to using the framebuffer.
# For Raspberry Pi, it's recommended to use PIGPIO for fast SPI I/O
#LIBS = -lpigpio -pthread
# For other boards, `gp` will use the Linux SPI driver if SPI_LCD is in CFLAGS:
# `make CLFAGS=-DSPI_LCD all`
LIBS = -lpthread
ifneq (,$(findstring SPI_LCD,$(CFLAGS)))
	LIBS += -lspi_lcd
endif

.PHONY: all
all: gp

.PHONY: application
application: all

gp: main.o mini_pil.o pil_io.o pil_lzw.o
	$(CC) main.o mini_pil.o pil_io.o pil_lzw.o $(LIBS) -o gp

mini_pil.o: mini_pil.c
	$(CC) $(CFLAGS) mini_pil.c

main.o: main.c
	$(CC) $(CFLAGS) main.c

pil_io.o: pil_io.c
	$(CC) $(CFLAGS) pil_io.c

pil_lzw.o: pil_lzw.c
	$(CC) $(CFLAGS) pil_lzw.c

.PHONY: clean
clean:
	rm -f *.o gp
