CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -pthread
LDFLAGS := -pthread -lrt
TARGET := campuscompute

.PHONY: all clean
all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET) campuscompute.log limit_demo.tmp
