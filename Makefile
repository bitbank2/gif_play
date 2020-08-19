CFLAGS=-c -Wall -O3
# For Raspberry Pi, it's recommended to use PIGPIO for fast SPI I/O
#LIBS = -lspi_lcd -lpigpio -pthread
# For other boards, use the Linux SPI driver
LIBS = -lspi_lcd -pthread 

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

