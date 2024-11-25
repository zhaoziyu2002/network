CC = g++
CFLAGS = -Wall -std=c++11

all: httpd

httpd: main.cpp
    $(CC) $(CFLAGS) -o httpd main.cpp

clean:
    rm -f httpd