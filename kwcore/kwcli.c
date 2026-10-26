#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
/*
 * Command-line front end for kwcore.
 *   kwcli [-b BAUD] PORT scan [all]
 *   kwcli [-b BAUD] PORT ident ADDR
 *   kwcli [-b BAUD] PORT faults ADDR
 *   kwcli [-b BAUD] PORT clear ADDR
 *   kwcli [-b BAUD] PORT group ADDR GROUP [GROUP...]
 * ADDR is hex as VCDS writes it (17 = instrument cluster). GROUP is decimal.
 */
#include "kw1281.h"
#include "io_serial.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const uint8_t COMMON_ADDRESSES[] = {
    0x01, 0x02, 0x03, 0x08, 0x09, 0x15, 0x16, 0x17, 0x19, 0x25,
    0x35, 0x36, 0x37, 0x45, 0x46, 0x55, 0x56, 0x57,
};

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s [-b BAUD] PORT COMMAND [ARGS]\n"
            "  scan [all]              probe common addresses (or 01-7F)\n"
            "  ident ADDR              controller identification\n"
            "  faults ADDR             read fault codes\n"
            "  clear ADDR              clear fault codes\n"
            "  group ADDR GROUP...     read measuring groups\n"
            "ADDR is hex (17 = cluster). Set KW_TRACE=1 to log every byte.\n",
            argv0);
}

static int parse_addr(const char *s)
{
    char *end;
    long v = strtol(s, &end, 16);
    if (*end || v < 1 || v > 0x7F) {
        fprintf(stderr, "Bad address '%s' (hex 01-7F)\n", s);
        exit(2);
    }
    return (int)v;
}

static void print_ident(const KwSession *s)
{
    const char *name = kw_module_name(s->address);
    printf("%02X %s (KW%d)\n", s->address, name ? name : "", s->protocol);
    printf("%s\n", s->ident);
}

static int connect_or_report(KwSession *s, KwIo *io, int addr)
{
    int err = kw_connect(s, io, (uint8_t)addr);
    if (err == KW_OK)
        return 0;
    fprintf(stderr, "%02X: %s", addr, kw_strerror(err));
    if (err == KW_ENOWAKE && s->sync_byte >= 0)
        fprintf(stderr, " (sync byte %02X, try another baud)", s->sync_byte);
    fprintf(stderr, "\n");
    return 1;
}

static int print_faults(const FaultCode *codes, int n)
{
    char text[32];
    if (n == 0)
        printf("No fault codes.\n");
    for (int i = 0; i < n; i++) {
        kw_format_fault(&codes[i], text, sizeof(text));
        printf("%s\n", text);
    }
    return 0;
}

static int do_scan(KwIo *io, int all)
{
    KwSession s;
    int found = 0;
    int count = all ? 0x7F : (int)sizeof(COMMON_ADDRESSES);

    for (int i = 0; i < count; i++) {
        int addr = all ? i + 1 : COMMON_ADDRESSES[i];
        fprintf(stderr, "probing %02X...\r", addr);
        int err = kw_connect_attempts(&s, io, (uint8_t)addr, 1);
        if (err == KW_OK) {
            print_ident(&s);
            kw_end(&s);
            found++;
        } else if (err == KW_EKWP2000) {
            printf("%02X (KWP%d, not supported)\n", addr, s.protocol);
            found++;
        }
    }
    fprintf(stderr, "%d controller(s) found.        \n", found);
    return found ? 0 : 1;
}

int main(int argc, char **argv)
{
    int baud = 9600;
    int opt;
    while ((opt = getopt(argc, argv, "b:h")) != -1) {
        if (opt == 'b') {
            baud = atoi(optarg);
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc < 2) {
        usage("kwcli");
        return 2;
    }

    const char *port = argv[0];
    const char *cmd = argv[1];

    KwIo io;
    if (kw_serial_open(&io, port, baud) != 0)
        return 1;
    kw_reset_interface(&io);

    KwSession s;
    FaultCode codes[64];
    int rc = 0;

    if (strcmp(cmd, "scan") == 0) {
        rc = do_scan(&io, argc >= 3 && strcmp(argv[2], "all") == 0);
    } else if (strcmp(cmd, "ident") == 0 && argc >= 3) {
        rc = connect_or_report(&s, &io, parse_addr(argv[2]));
        if (!rc) {
            print_ident(&s);
            kw_end(&s);
        }
    } else if (strcmp(cmd, "faults") == 0 && argc >= 3) {
        rc = connect_or_report(&s, &io, parse_addr(argv[2]));
        if (!rc) {
            int n = kw_read_fault_codes(&s, codes, 64);
            if (n < 0) {
                fprintf(stderr, "read faults: %s\n", kw_strerror(n));
                rc = 1;
            } else {
                print_faults(codes, n);
            }
            kw_end(&s);
        }
    } else if (strcmp(cmd, "clear") == 0 && argc >= 3) {
        rc = connect_or_report(&s, &io, parse_addr(argv[2]));
        if (!rc) {
            int n = kw_clear_fault_codes(&s, codes, 64);
            if (n < 0) {
                fprintf(stderr, "clear faults: %s\n", kw_strerror(n));
                rc = 1;
            } else {
                printf("Cleared. Remaining:\n");
                print_faults(codes, n);
            }
            kw_end(&s);
        }
    } else if (strcmp(cmd, "group") == 0 && argc >= 4) {
        rc = connect_or_report(&s, &io, parse_addr(argv[2]));
        for (int g = 3; !rc && g < argc; g++) {
            SensorValue values[16];
            int group = atoi(argv[g]);
            int n = kw_group_read(&s, group, values, 16);
            if (n < 0) {
                printf("Group %d: %s\n", group, kw_strerror(n));
                if (n != KW_ENAK)
                    rc = 1;
                continue;
            }
            printf("Group %d:", group);
            for (int i = 0; i < n; i++) {
                char text[64];
                kw_format_sensor(&values[i], text, sizeof(text));
                printf("  %s", text);
            }
            printf("\n");
        }
        kw_end(&s);
    } else {
        usage("kwcli");
        rc = 2;
    }

    int echo_errors = kw_serial_echo_errors(&io);
    if (echo_errors)
        fprintf(stderr, "warning: %d byte(s) without a clean echo; check cable and ignition\n", echo_errors);
    kw_serial_close(&io);
    return rc;
}
