all: dw

CC=gcc -g -O0
DEPS=
OBJS= dw_iic.o  dw_iic_obj.o  main.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS)

dw: $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS)

clean:
	rm -f $(OBJS) dw
