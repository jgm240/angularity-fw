/*
 * Host-side fuzzer for emulating SLIX / ISO15693 tags. Compiles the real listener sources and
 * throws requests at every tag variant, reporting each distinct furi_check() that would crash
 * a Flipper. See run.sh.
 */
#include "furi.h"

#include <nfc/protocols/slix/slix_listener_i.h>
#include <nfc/protocols/iso15693_3/iso15693_3_listener_i.h>
#include <nfc/protocols/nfc_listener_base.h>
#include <nfc/helpers/iso13239_crc.h>

extern const NfcListenerBase nfc_listener_slix;
extern const NfcListenerBase nfc_listener_iso15693_3;
extern int host_tx_count;

typedef struct {
    const char* name;
    uint8_t manufacturer;
    uint8_t icode_type; /* 1 SLIX/SLIX2, 2 SLIX-S, 3 SLIX-L */
    uint8_t type_indicator; /* 2 SLIX, 1 SLIX2 (only for icode_type 1) */
    uint16_t blocks;
} TagCfg;

static const TagCfg tags[] = {
    {"SLIX", 0x04, 0x01, 0x02, 28},
    {"SLIX-S", 0x04, 0x02, 0x00, 40},
    {"SLIX-L", 0x04, 0x03, 0x00, 8},
    {"SLIX2", 0x04, 0x01, 0x01, 80},
    {"SLIX2 edited UID (type unknown)", 0x04, 0x01, 0x03, 80}, /* NXP UID, unrecognised indicator */
    {"SLIX edited UID (not NXP)", 0x07, 0x01, 0x02, 28}, /* e.g. a UID changed in the NFC app */
    {"SLIX-S big (255 blocks)", 0x04, 0x02, 0x00, 255},
    {"SLIX2 tiny (1 block)", 0x04, 0x01, 0x01, 1},
};

typedef struct {
    Iso15693_3Listener* base;
    SlixListener* slix;
    SlixData* data;
    NfcCommand (*run)(NfcGenericEvent, void*);
} Rig;

static Rig make_rig(const TagCfg* cfg) {
    Rig rig;
    rig.data = slix_alloc();
    Iso15693_3Data* iso = rig.data->iso15693_3_data;
    memset(iso->uid, 0, sizeof(iso->uid));
    iso->uid[0] = 0xE0;
    iso->uid[1] = cfg->manufacturer;
    iso->uid[2] = cfg->icode_type;
    iso->uid[3] = (uint8_t)(cfg->type_indicator << 3);
    iso->uid[4] = 0x11;
    iso->uid[5] = 0x22;
    iso->uid[6] = 0x33;
    iso->uid[7] = 0x44;
    iso->system_info.flags = ISO15693_3_SYSINFO_FLAG_DSFID | ISO15693_3_SYSINFO_FLAG_AFI |
                             ISO15693_3_SYSINFO_FLAG_MEMORY | ISO15693_3_SYSINFO_FLAG_IC_REF;
    iso->system_info.block_count = cfg->blocks;
    iso->system_info.block_size = 4;
    iso->system_info.ic_ref = 0x03;
    simple_array_init(iso->block_data, cfg->blocks * 4);
    simple_array_init(iso->block_security, cfg->blocks);
    for(uint32_t i = 0; i < cfg->blocks * 4u; i++) *(uint8_t*)simple_array_get(iso->block_data, i) = (uint8_t)i;

    /* the same wiring nfc_listener_alloc() does */
    rig.base = nfc_listener_iso15693_3.alloc(NULL, iso);
    rig.slix = nfc_listener_slix.alloc(rig.base, rig.data);
    nfc_listener_iso15693_3.set_callback(rig.base, (NfcGenericCallback)nfc_listener_slix.run, rig.slix);
    return rig;
}

/* Send one request frame (CRC added here). */
static void send(Rig* rig, const uint8_t* bytes, size_t length) {
    static BitBuffer* rx = NULL;
    if(!rx) rx = bit_buffer_alloc(512);
    bit_buffer_reset(rx);
    bit_buffer_append_bytes(rx, bytes, length);
    iso13239_crc_append(Iso13239CrcTypeDefault, rx);

    NfcEvent event = {.type = NfcEventTypeRxEnd};
    event.data.buffer = rx;
    NfcGenericEvent generic = {.protocol = NfcProtocolInvalid, .instance = rig->base, .event_data = &event};
    nfc_listener_iso15693_3.run(generic, rig->base);
}

/* ---------------------------------------------------------------- failure bookkeeping */
#define MAX_FAILS 64
typedef struct {
    char tag[48];
    char message[256];
    uint8_t request[40];
    size_t request_length;
    long count;
} Failure;
static Failure fails[MAX_FAILS];
static int fail_count = 0;

static void record(const TagCfg* cfg, const uint8_t* req, size_t length) {
    for(int i = 0; i < fail_count; i++) {
        if(strcmp(fails[i].tag, cfg->name) == 0 && strcmp(fails[i].message, furi_host_msg) == 0) {
            fails[i].count++;
            return;
        }
    }
    if(fail_count == MAX_FAILS) return;
    Failure* f = &fails[fail_count++];
    snprintf(f->tag, sizeof(f->tag), "%s", cfg->name);
    snprintf(f->message, sizeof(f->message), "%s", furi_host_msg);
    f->request_length = length < sizeof(f->request) ? length : sizeof(f->request);
    memcpy(f->request, req, f->request_length);
    f->count = 1;
}

/* Run a request; returns false when it hit a furi_check. */
static bool attempt(const TagCfg* cfg, Rig* rig, const uint8_t* req, size_t length) {
    furi_host_armed = 1;
    if(setjmp(furi_host_jmp) == 0) {
        send(rig, req, length);
        furi_host_armed = 0;
        return true;
    }
    record(cfg, req, length);
    return false;
}

/* ---------------------------------------------------------------- request generation */
static uint8_t interesting(void) {
    static const uint8_t v[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x07, 0x08, 0x0F, 0x10, 0x1B, 0x1C, 0x27,
                                0x28, 0x4F, 0x50, 0x7F, 0x80, 0xFE, 0xFF};
    return rand() % 3 ? v[rand() % sizeof(v)] : (uint8_t)rand();
}

static size_t build_request(const TagCfg* cfg, Rig* rig, uint8_t* out) {
    static const uint8_t flag_set[] = {0x02, 0x22, 0x12, 0x42, 0x62, 0x06, 0x26, 0x0A, 0x00, 0x32, 0x72};
    size_t n = 0;
    uint8_t flags = rand() % 5 ? flag_set[rand() % sizeof(flag_set)] : (uint8_t)rand();
    uint8_t command;
    int kind = rand() % 6;
    if(kind <= 1) command = 0xA2 + rand() % (0xBE - 0xA2); /* SLIX custom commands */
    else if(kind == 2) command = 0x20 + rand() % 0x0E; /* optional ISO commands */
    else if(kind == 3) command = 0x01 + rand() % 3; /* mandatory */
    else command = (uint8_t)rand();
    out[n++] = flags;
    out[n++] = command;
    if(command >= 0xA0) out[n++] = rand() % 6 ? 0x04 : (uint8_t)rand(); /* manufacturer */
    if(flags & 0x20) { /* addressed: UID on the wire is least significant byte first */
        for(int i = 7; i >= 0; i--) out[n++] = rig->data->iso15693_3_data->uid[i];
        if(rand() % 40 == 0) out[n - 1] ^= 0x55;
    }
    size_t params = rand() % 12;
    for(size_t i = 0; i < params; i++) out[n++] = interesting();
    return n;
}

/* GET_RANDOM_NUMBER then SET_PASSWORD / ENABLE_PRIVACY with every password id */
static void password_sequences(const TagCfg* cfg) {
    for(int id = 0; id < 256; id++) {
        Rig rig = make_rig(cfg);
        uint8_t uid_wire[8];
        for(int i = 0; i < 8; i++) uid_wire[i] = rig.data->iso15693_3_data->uid[7 - i];
        for(int addressed = 0; addressed < 2; addressed++) {
            for(int step = 0; step < 4; step++) {
                uint8_t req[40];
                size_t n = 0;
                req[n++] = addressed ? 0x22 : 0x02;
                static const uint8_t cmds[4] = {0xB2, 0xB3, 0xB4, 0xBA};
                req[n++] = cmds[step];
                req[n++] = 0x04;
                if(addressed) { memcpy(&req[n], uid_wire, 8); n += 8; }
                if(step == 1 || step == 2) req[n++] = (uint8_t)id;
                if(step) { /* a password-sized payload */
                    for(int i = 0; i < 4; i++) req[n++] = (uint8_t)(rand());
                }
                if(!attempt(cfg, &rig, req, n)) { rig = make_rig(cfg); }
            }
        }
    }
}

/* Requests that crashed real devices before, kept as named cases. */
static int regressions(void) {
    int failed = 0;
    for(size_t t = 0; t < sizeof(tags) / sizeof(tags[0]); t++) {
        const TagCfg* cfg = &tags[t];
        Rig rig = make_rig(cfg);
        uint8_t uid_wire[8];
        for(int i = 0; i < 8; i++) uid_wire[i] = rig.data->iso15693_3_data->uid[7 - i];

        struct {
            const char* what;
            uint8_t bytes[16];
            size_t length;
        } cases[] = {
            {"reserved command 0x00", {0x02, 0x00}, 2},
            {"reserved command 0x00, option flag", {0x42, 0x00}, 2},
            {"enable privacy", {0x02, 0xBA, 0x04, 0x11, 0x22, 0x33, 0x44}, 7},
            {"set password (privacy)", {0x02, 0xB3, 0x04, 0x04, 0x11, 0x22, 0x33, 0x44}, 8},
            {"write password (read)", {0x02, 0xB4, 0x04, 0x01, 0x11, 0x22, 0x33, 0x44}, 8},
        };
        for(size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
            if(!attempt(cfg, &rig, cases[c].bytes, cases[c].length)) {
                printf("REGRESSION [%s] %s: %s\n", cfg->name, cases[c].what, furi_host_msg);
                failed++;
                rig = make_rig(cfg);
            }
        }
        (void)uid_wire;
    }
    return failed;
}

int main(int argc, char** argv) {
    long rounds = argc > 1 ? atol(argv[1]) : 200000;
    srand(argc > 2 ? (unsigned)atoi(argv[2]) : 1234);
    long total = 0;
    int regression_failures = regressions();
    fail_count = 0; /* regression failures are reported above; the random run starts clean */

    for(size_t t = 0; t < sizeof(tags) / sizeof(tags[0]); t++) {
        const TagCfg* cfg = &tags[t];
        Rig rig = make_rig(cfg);
        password_sequences(cfg);
        for(long i = 0; i < rounds; i++) {
            uint8_t req[48];
            size_t n = build_request(cfg, &rig, req);
            if(!attempt(cfg, &rig, req, n)) rig = make_rig(cfg);
            total++;
        }
    }

    printf("%ld requests sent to %zu tag variants\n", total, sizeof(tags) / sizeof(tags[0]));
    if(!fail_count) {
        printf("no furi_check failures in the random run\n");
        return regression_failures ? 1 : 0;
    }
    printf("%d distinct furi_check failure(s):\n", fail_count);
    for(int i = 0; i < fail_count; i++) {
        printf("\n[%s] x%ld\n  %s\n  request:", fails[i].tag, fails[i].count, fails[i].message);
        for(size_t b = 0; b < fails[i].request_length; b++) printf(" %02X", fails[i].request[b]);
        printf("\n");
    }
    return 1;
}
