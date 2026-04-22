CC      := gcc

TARGET  := marching_squares

SRC     := src/main.c
OBJ     := $(SRC:.c=.o)

CFLAGS  := -Wall -Wextra -Wpedantic -std=c99 -g -Iinclude
LDFLAGS := -Llib
LIBS    := -lraylib -lopengl32 -lgdi32 -lwinmm

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	if exist *.o del /Q *.o
	if exist $(TARGET).exe del /Q $(TARGET).exe

