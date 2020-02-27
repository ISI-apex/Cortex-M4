all: dw

CC=gcc
DEPS=
OBJS= dw_spi.o  dw_spi_obj.o main.o

%.o: %.c $(DEPS)
	$(CC) -c -O2 -o $@ $< $(CFLAGS)

dw: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(OBJS) dw
