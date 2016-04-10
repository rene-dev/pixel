CC = gcc
CFLAGS = -Wall -c `pkg-config --cflags sdl2` -Wall -O3
LDFLAGS = `pkg-config --libs sdl2` -lm -lpthread -O3
EXE = pixel

all: $(EXE)

$(EXE): main.o
	$(CC) -o $@ main.o $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf *.o $(EXE)
