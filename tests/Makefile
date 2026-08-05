CC ?= cc
CPPFLAGS += -D_GNU_SOURCE -I../ft8_lib
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -std=c11
LDLIBS += -lm

TARGET := test_compound_calls
VECTORS := compound_calls_regression.txt
FT8_SOURCES := \
	../ft8_lib/ft8/constants.c \
	../ft8_lib/ft8/crc.c \
	../ft8_lib/ft8/encode.c \
	../ft8_lib/ft8/ldpc.c \
	../ft8_lib/ft8/message.c \
	../ft8_lib/ft8/text.c

.PHONY: all run clean

all: $(TARGET)

$(TARGET): test_compound_calls.c $(FT8_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDLIBS)

run: $(TARGET)
	./$(TARGET) $(VECTORS)

clean:
	rm -f $(TARGET)
