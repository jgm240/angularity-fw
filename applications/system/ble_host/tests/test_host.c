/* Unit tests for furi_ble/host_adv.h and furi_ble/host_gatt.h. Run with tests/run.sh */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#include "host_adv.h"
#include "host_gatt.h"

static int fails = 0;

/* Copy of src that ends exactly where an inaccessible page begins, so reading even one byte
 * past `len` crashes the test. Works without AddressSanitizer. */
static uint8_t* guarded_copy(const uint8_t* src, size_t len) {
    static uint8_t* end = NULL;
    static size_t page = 0;
    if(!end) {
        page = (size_t)sysconf(_SC_PAGESIZE);
        uint8_t* base = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if(base == MAP_FAILED || mprotect(base + page, page, PROT_NONE) != 0) {
            printf("cannot set up guard page\n");
            exit(2);
        }
        end = base + page;
    }
    uint8_t* p = end - len;
    memcpy(p, src, len);
    return p;
}
#define CHECK(c) \
    do { \
        if(!(c)) { \
            printf("FAIL line %d: %s\n", __LINE__, #c); \
            fails++; \
        } \
    } while(0)

/* ---------------------------------------------------------------- advertising */
static void test_adv(void) {
    const uint8_t good[] = {
        2, 0x01, 0x06, /* flags */
        4, 0x08, 'F', 'l', 'p', /* shortened name */
        9, 0x09, 'F', 'l', 'i', 'p', 'p', 'e', 'r', '!', /* complete name */
        3, 0x03, 0x0F, 0x18, /* uuid16 0x180F */
        2, 0x0A, 0xF4, /* tx power -12 */
        4, 0xFF, 0x4C, 0x00, 0x10, /* company 0x004C + 1 byte */
        3, 0x19, 0x41, 0x03, /* appearance 0x0341 */
    };
    BleHostAdv a;
    CHECK(ble_host_adv_parse(good, sizeof(good), &a));
    CHECK(strcmp(a.name, "Flipper!") == 0 && a.name_complete); /* full name beats short name */
    CHECK(a.has_flags && a.flags == 0x06);
    CHECK(a.uuid16_count == 1 && a.uuid16[0] == 0x180F);
    CHECK(a.has_tx_power && a.tx_power == -12);
    CHECK(a.has_mfg && a.mfg_company_id == 0x004C && a.mfg_data_len == 1 && a.mfg_data[0] == 0x10);
    CHECK(a.has_appearance && a.appearance == 0x0341);

    /* a short name after the full name must not replace it */
    const uint8_t order[] = {9, 0x09, 'F', 'l', 'i', 'p', 'p', 'e', 'r', '!', 4, 0x08, 'F', 'l', 'p'};
    CHECK(ble_host_adv_parse(order, sizeof(order), &a));
    CHECK(strcmp(a.name, "Flipper!") == 0);

    /* control characters and 8-bit bytes in a name are neutralised */
    const uint8_t evil[] = {7, 0x09, 'a', 0x1B, '[', 0xFF, 0x00, 'z'};
    CHECK(ble_host_adv_parse(evil, sizeof(evil), &a));
    for(const char* p = a.name; *p; p++) CHECK(*p >= 0x20 && *p < 0x7F);
    CHECK(strlen(a.name) == 6);

    /* a field that claims to be longer than the data: false, but earlier fields are kept */
    const uint8_t bad[] = {2, 0x01, 0x06, 0x20, 0x09, 'x'};
    CHECK(!ble_host_adv_parse(bad, sizeof(bad), &a));
    CHECK(a.has_flags);

    /* a zero length ends the data; trailing padding is ignored */
    const uint8_t pad[] = {2, 0x01, 0x04, 0, 0, 0, 5, 0x09, 'n'};
    CHECK(ble_host_adv_parse(pad, sizeof(pad), &a));
    CHECK(a.name[0] == '\0');

    /* too many UUIDs and over-long names are capped, not overflowed */
    uint8_t many[31];
    many[0] = 30;
    many[1] = 0x03;
    for(int i = 0; i < 29; i++) many[2 + i] = (uint8_t)i;
    CHECK(ble_host_adv_parse(many, 31, &a));
    CHECK(a.uuid16_count == 8);
    uint8_t longname[31];
    longname[0] = 30;
    longname[1] = 0x09;
    memset(&longname[2], 'A', 29);
    CHECK(ble_host_adv_parse(longname, 31, &a) && strlen(a.name) == 29);

    /* 128-bit uuid */
    uint8_t u128[18] = {17, 0x07};
    for(int i = 0; i < 16; i++) u128[2 + i] = (uint8_t)(0xA0 + i);
    CHECK(ble_host_adv_parse(u128, sizeof(u128), &a));
    CHECK(a.uuid128_count == 1 && a.uuid128[0][0] == 0xA0 && a.uuid128[0][15] == 0xAF);

    /* every truncation of a valid buffer is safe */
    for(uint8_t n = 0; n <= sizeof(good); n++) ble_host_adv_parse(guarded_copy(good, n), n, &a);
}

/* ---------------------------------------------------------------- decoders */
static void test_decoders(void) {
    BleHostService s[4];
    const uint8_t sv[] = {0x01, 0x00, 0x05, 0x00, 0x00, 0x18, 0x06, 0x00, 0x0B, 0x00, 0x0F, 0x18};
    CHECK(ble_host_parse_services(sv, sizeof(sv), 6, s, 4) == 2);
    CHECK(s[0].start_handle == 1 && s[0].end_handle == 5 && ble_host_uuid16(&s[0].uuid) == 0x1800);
    CHECK(s[1].start_handle == 6 && s[1].end_handle == 11 && ble_host_uuid16(&s[1].uuid) == 0x180F);
    CHECK(ble_host_parse_services(sv, sizeof(sv), 7, s, 4) == 0); /* wrong item length */
    CHECK(ble_host_parse_services(sv, 11, 6, s, 4) == 1); /* trailing partial entry */
    CHECK(ble_host_parse_services(sv, sizeof(sv), 6, s, 1) == 1); /* output cap */
    const uint8_t badsv[] = {0x00, 0x00, 0x05, 0x00, 0x00, 0x18, 0x09, 0x00, 0x02, 0x00, 0x0F, 0x18};
    CHECK(ble_host_parse_services(badsv, sizeof(badsv), 6, s, 4) == 0); /* handle 0, end < start */

    uint8_t sv128[20] = {0x10, 0x00, 0x20, 0x00};
    for(int i = 0; i < 16; i++) sv128[4 + i] = (uint8_t)i;
    CHECK(ble_host_parse_services(sv128, sizeof(sv128), 20, s, 4) == 1);
    CHECK(s[0].uuid.size == 16 && s[0].uuid.bytes[15] == 15 && ble_host_uuid16(&s[0].uuid) == 0);

    BleHostCharacteristic c[4];
    const uint8_t ch[] = {0x07, 0x00, 0x12, 0x08, 0x00, 0x19, 0x2A, 0x09, 0x00, 0x0A, 0x0A, 0x00, 0x1A, 0x2A};
    CHECK(ble_host_parse_chars(ch, sizeof(ch), 7, c, 4) == 2);
    CHECK(c[0].declaration_handle == 7 && c[0].value_handle == 8 && c[0].properties == 0x12);
    CHECK(ble_host_uuid16(&c[1].uuid) == 0x2A1A && c[1].value_handle == 10);
    const uint8_t badch[] = {0x07, 0x00, 0x12, 0x07, 0x00, 0x19, 0x2A}; /* value handle == declaration */
    CHECK(ble_host_parse_chars(badch, sizeof(badch), 7, c, 4) == 0);

    BleHostDescriptor d[4];
    const uint8_t ds[] = {0x09, 0x00, 0x02, 0x29, 0x0A, 0x00, 0x01, 0x29};
    CHECK(ble_host_parse_descs(ds, sizeof(ds), 1, d, 4) == 2);
    CHECK(d[0].handle == 9 && ble_host_uuid16(&d[0].uuid) == 0x2902);
    CHECK(ble_host_parse_descs(ds, sizeof(ds), 3, d, 4) == 0);

    /* hostile input: random bytes of every length, never reads out of bounds (ASan) */
    uint8_t rnd[255];
    srand(7);
    for(int round = 0; round < 2000; round++) {
        for(int i = 0; i < 255; i++) rnd[i] = (uint8_t)rand();
        uint8_t len = (uint8_t)(rand() % 256);
        BleHostService ss[3];
        BleHostCharacteristic cc[3];
        BleHostDescriptor dd[3];
        ble_host_parse_services(rnd, len, (uint8_t)(rand() % 3 ? 6 : 20), ss, 3);
        ble_host_parse_chars(rnd, len, (uint8_t)(rand() % 3 ? 7 : 21), cc, 3);
        ble_host_parse_descs(rnd, len, (uint8_t)(1 + rand() % 2), dd, 3);
        BleHostAdv a;
        ble_host_adv_parse(rnd, len > 31 ? 31 : len, &a);
    }
    srand(8);
    for(int round = 0; round < 2000; round++) {
        for(int i = 0; i < 255; i++) rnd[i] = (uint8_t)rand();
        uint8_t len = (uint8_t)(rand() % 256);
        const uint8_t* in = guarded_copy(rnd, len);
        BleHostService ss[3];
        BleHostCharacteristic cc[3];
        BleHostDescriptor dd[3];
        ble_host_parse_services(in, len, (uint8_t)(rand() % 3 ? 6 : 20), ss, 3);
        ble_host_parse_chars(in, len, (uint8_t)(rand() % 3 ? 7 : 21), cc, 3);
        ble_host_parse_descs(in, len, (uint8_t)(1 + rand() % 2), dd, 3);
        BleHostAdv a;
        uint8_t adv_len = len > 31 ? 31 : len;
        ble_host_adv_parse(guarded_copy(rnd, adv_len), adv_len, &a);
    }
}

/* ---------------------------------------------------------------- walker */
typedef struct {
    int services, chars, descs;
    uint16_t order[64];
    int n;
} Seen;
#define REC(s, v) \
    do { \
        if((s)->n < 64) (s)->order[(s)->n] = (v); \
        (s)->n++; \
    } while(0)

static void on_item(void* ctx, const HostDiscItem* it) {
    Seen* s = ctx;
    if(it->kind == HostDiscItemService) {
        s->services++;
        REC(s, 0x1000 | it->service.start_handle);
    }
    if(it->kind == HostDiscItemCharacteristic) {
        s->chars++;
        REC(s, 0x2000 | it->characteristic.value_handle);
    }
    if(it->kind == HostDiscItemDescriptor) {
        s->descs++;
        REC(s, 0x3000 | it->descriptor.handle);
    }
}

static void test_walker(void) {
    static HostDisc d;
    Seen seen = {0};
    host_disc_init(&d, on_item, &seen);
    HostStep st = host_disc_start(&d);
    CHECK(st.kind == HostStepServices && st.start == 1 && st.end == 0xFFFF);

    /* services: GAP 1-5, Battery 6-11, Empty 12-12 */
    const uint8_t sv[] = {1, 0, 5, 0, 0x00, 0x18, 6, 0, 11, 0, 0x0F, 0x18, 12, 0, 12, 0, 0x10, 0x18};
    host_disc_on_services(&d, sv, sizeof(sv), 6);
    st = host_disc_on_complete(&d, 0);
    CHECK(seen.services == 3);
    CHECK(st.kind == HostStepChars && st.start == 1 && st.end == 5);

    /* GAP: two characteristics (declaration 2 value 3, declaration 4 value 5): no descriptors
       fit between them, so the walker goes straight to the next service */
    const uint8_t gap_chars[] = {2, 0, 0x02, 3, 0, 0x00, 0x2A, 4, 0, 0x02, 5, 0, 0x01, 0x2A};
    host_disc_on_chars(&d, gap_chars, sizeof(gap_chars), 7);
    st = host_disc_on_complete(&d, 0);
    CHECK(st.kind == HostStepChars && st.start == 6 && st.end == 11);
    CHECK(seen.chars == 2);

    /* Battery: one characteristic (declaration 7, value 8) with a CCCD at 9; the response also
       lists a characteristic declaration, which must be filtered out */
    const uint8_t bat_chars[] = {7, 0, 0x12, 8, 0, 0x19, 0x2A};
    host_disc_on_chars(&d, bat_chars, sizeof(bat_chars), 7);
    st = host_disc_on_complete(&d, 0);
    CHECK(st.kind == HostStepDescs && st.start == 9 && st.end == 11);
    const uint8_t descs[] = {9, 0, 0x02, 0x29, 10, 0, 0x01, 0x29, 11, 0, 0x03, 0x28};
    host_disc_on_descs(&d, descs, sizeof(descs), 1);
    CHECK(seen.descs == 2);
    st = host_disc_on_complete(&d, 0);
    CHECK(st.kind == HostStepChars && st.start == 12 && st.end == 12);

    /* the empty service has no characteristics: done */
    st = host_disc_on_complete(&d, 0);
    CHECK(st.kind == HostStepDone);
    CHECK(!host_disc_active(&d));
    CHECK(!d.truncated);
    {
        const uint16_t want[] = {0x1001, 0x1006, 0x100C, 0x2003, 0x2005, 0x2008, 0x3009, 0x300A};
        CHECK(seen.n == 8);
        for(int i = 0; i < 8 && i < seen.n; i++) CHECK(seen.order[i] == want[i]);
    }

    /* a failure during service discovery */
    host_disc_init(&d, NULL, NULL);
    host_disc_start(&d);
    st = host_disc_on_complete(&d, 0x41);
    CHECK(st.kind == HostStepFailed && st.status == 0x41);
    CHECK(!host_disc_active(&d));

    /* no services at all: done at once */
    host_disc_start(&d);
    st = host_disc_on_complete(&d, 0);
    CHECK(st.kind == HostStepDone);

    /* a descriptor request that fails is skipped, not fatal */
    host_disc_start(&d);
    const uint8_t one[] = {1, 0, 9, 0, 0x00, 0x18};
    host_disc_on_services(&d, one, sizeof(one), 6);
    host_disc_on_complete(&d, 0);
    const uint8_t two[] = {2, 0, 0x12, 3, 0, 0x00, 0x2A, 6, 0, 0x12, 7, 0, 0x01, 0x2A};
    host_disc_on_chars(&d, two, sizeof(two), 7);
    st = host_disc_on_complete(&d, 0);
    CHECK(st.kind == HostStepDescs && st.start == 4 && st.end == 5);
    st = host_disc_on_complete(&d, 0x0E);
    CHECK(st.kind == HostStepDescs && st.start == 8 && st.end == 9);
    st = host_disc_on_complete(&d, 0);
    CHECK(st.kind == HostStepDone);

    /* responses in the wrong phase are ignored */
    host_disc_init(&d, NULL, NULL);
    host_disc_on_services(&d, sv, sizeof(sv), 6);
    CHECK(d.service_count == 0);
    host_disc_on_descs(&d, descs, sizeof(descs), 1);
    CHECK(host_disc_on_complete(&d, 0).kind == HostStepNone);

    /* one response with more entries than any scratch buffer: all of them come through */
    host_disc_init(&d, on_item, &seen);
    seen.n = seen.services = seen.chars = seen.descs = 0;
    host_disc_start(&d);
    uint8_t big[36 * 6];
    for(int i = 0; i < 36; i++) {
        big[i * 6] = (uint8_t)(i * 3 + 1);
        big[i * 6 + 1] = 0;
        big[i * 6 + 2] = (uint8_t)(i * 3 + 3);
        big[i * 6 + 3] = 0;
        big[i * 6 + 4] = 0x00;
        big[i * 6 + 5] = 0x18;
    }
    host_disc_on_services(&d, big, sizeof(big), 6);
    CHECK(d.service_count == 32 && d.truncated); /* 36 sent, 32 kept */
    host_disc_init(&d, on_item, &seen);
    seen.n = seen.services = 0;
    host_disc_start(&d);
    host_disc_on_services(&d, big, 20 * 6, 6);
    CHECK(d.service_count == 20 && seen.services == 20 && !d.truncated);

    /* fuzz: random events and completions in random order never crash */
    srand(11);
    for(int round = 0; round < 3000; round++) {
        uint8_t raw[200];
        for(int i = 0; i < 200; i++) raw[i] = (uint8_t)rand();
        uint8_t len = (uint8_t)(rand() % 200);
        const uint8_t* buf = guarded_copy(raw, len);
        switch(rand() % 5) {
        case 0:
            host_disc_start(&d);
            break;
        case 1:
            host_disc_on_services(&d, buf, len, rand() % 2 ? 6 : 20);
            break;
        case 2:
            host_disc_on_chars(&d, buf, len, rand() % 2 ? 7 : 21);
            break;
        case 3:
            host_disc_on_descs(&d, buf, len, 1 + rand() % 2);
            break;
        default:
            host_disc_on_complete(&d, (uint8_t)(rand() % 4 == 0 ? rand() : 0));
            break;
        }
    }
}

int main(void) {
    test_adv();
    test_decoders();
    test_walker();
    printf(fails ? "FAILED: %d\n" : "all host-mode core tests passed\n", fails);
    return fails != 0;
}
