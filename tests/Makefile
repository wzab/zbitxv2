CC ?= cc
CPPFLAGS += -D_GNU_SOURCE -I../ft8_lib
CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -std=c11
LDLIBS += -lm

FT8_TARGET := test_compound_calls
HIST_TARGET := test_hist_disp
TX_TARGET := test_ft8_tx_message
TARGETS := $(FT8_TARGET) $(HIST_TARGET) $(TX_TARGET)
VECTORS := compound_calls_regression.txt
FT8_SOURCES := \
	../ft8_lib/ft8/constants.c \
	../ft8_lib/ft8/crc.c \
	../ft8_lib/ft8/encode.c \
	../ft8_lib/ft8/ldpc.c \
	../ft8_lib/ft8/message.c \
	../ft8_lib/ft8/text.c

.PHONY: all run clean

all: $(TARGETS)

$(FT8_TARGET): test_compound_calls.c $(FT8_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(HIST_TARGET): test_hist_disp.c ../hist_disp.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^

$(TX_TARGET): test_ft8_tx_message.c ../ft8_tx_message.c $(FT8_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDLIBS)

run: $(TARGETS)
	./$(FT8_TARGET) $(VECTORS)
	./$(HIST_TARGET)
	./$(TX_TARGET)

clean:
	rm -f $(TARGETS)
