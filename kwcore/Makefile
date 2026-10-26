CFLAGS ?= -std=c99 -Wall -Wextra -O2

all: kwcli test_kw1281

kwcli: kwcli.c kw1281.c io_serial.c kw1281.h io_serial.h
	$(CC) $(CFLAGS) -o $@ kwcli.c kw1281.c io_serial.c

test_kw1281: test_kw1281.c fake_ecu.c fake_ecu.h fault_table.c fault_table.h $(wildcard fault_types.h) car_profile.c car_profile.h labels.c labels.h kw1281.c kw1281.h
	$(CC) $(CFLAGS) -o $@ test_kw1281.c fake_ecu.c fault_table.c car_profile.c labels.c kw1281.c

test: test_kw1281
	./test_kw1281

clean:
	rm -f kwcli test_kw1281

.PHONY: all test clean
