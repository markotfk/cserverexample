TARGET = client
LIBS = -lpthread
INCLUDE = ../include
CC = gcc
CFLAGS = -g -Wall -I$(INCLUDE)

.PHONY: default all clean

default: $(TARGET)
all: default

OBJECTS = $(patsubst %.c, %.o, $(wildcard *.c))
HEADERS = $(wildcard *.h)

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@ 

.PRECIOUS: $(TARGET) $(OBJECTS)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -Wall $(LIBS) -o $@

run:
	./$(TARGET) localhost 1200

clean:
	-rm -f *.o
	-rm -f $(TARGET)
