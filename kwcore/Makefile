CFLAGS ?= -std=c99 -Wall -Wextra -O2

all: kwcli test_kw1281 test_health

kwcli: kwcli.c kw1281.c io_serial.c kw1281.h io_serial.h
	$(CC) $(CFLAGS) -o $@ kwcli.c kw1281.c io_serial.c

test_kw1281: test_kw1281.c fake_ecu.c fake_ecu.h fault_table.c fault_table.h $(wildcard fault_types.h) car_profile.c car_profile.h labels.c labels.h dashboard.c dashboard.h wikitext.c wikitext.h kw1281.c kw1281.h
	$(CC) $(CFLAGS) -o $@ test_kw1281.c fake_ecu.c fault_table.c car_profile.c labels.c dashboard.c wikitext.c kw1281.c -lm

test_health: test_health.c health.c health.h dashboard.c dashboard.h fake_ecu.c fake_ecu.h kw1281.c kw1281.h
	$(CC) $(CFLAGS) -o $@ test_health.c health.c dashboard.c fake_ecu.c kw1281.c -lm

test: test_kw1281 test_health
	./test_kw1281
	./test_health

clean:
	rm -f kwcli test_kw1281 test_health

.PHONY: all test clean
