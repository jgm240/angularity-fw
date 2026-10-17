#pragma once

// Minimal FlipperHTTP client for the WiFi Devboard (ESP32 running FlipperHTTP), used by the
// Standby app. Header-only (static inline) so each app compiles only what it uses.
// Protocol: every command is one line ending in \n. "[PING]" -> "[PONG]". A request is
// "[GET/BYTES]{"url":...,"headers":{...}}" and is answered by
// "[GET/SUCCESS]{"Status-Code":200,"Content-Length":N}" followed by exactly N raw bytes and
// "[GET/END]".

#include <furi.h>
#include <furi_hal.h>
#include <expansion/expansion.h>
#include <momentum/settings.h>
#include <stdlib.h>
#include <string.h>

#define DEVBOARD_BAUD       115200
#define DEVBOARD_RX_SIZE    4096
#define DEVBOARD_IDLE_MS    15000

typedef struct {
    FuriHalSerialHandle* serial;
    FuriStreamBuffer* rx;
    Expansion* expansion;
    volatile uint32_t overflow;
    volatile bool* cancel; // optional: set by the app to abort a request
    char error[64]; // last failure reason
} DevBoard;

static inline void devboard_rx_callback(
    FuriHalSerialHandle* handle,
    FuriHalSerialRxEvent event,
    void* context) {
    DevBoard* d = context;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        if(furi_stream_buffer_send(d->rx, &byte, 1, 0) != 1) d->overflow++;
    }
}

static inline bool devboard_cancelled(const DevBoard* d) {
    return d->cancel && *d->cancel;
}

static inline void devboard_set_error(DevBoard* d, const char* text) {
    snprintf(d->error, sizeof(d->error), "%.60s", text);
}

static inline bool devboard_open(DevBoard* d, volatile bool* cancel) {
    memset(d, 0, sizeof(*d));
    d->cancel = cancel;
    FuriHalSerialId channel = momentum_settings.uart_esp_channel;
    d->expansion = furi_record_open(RECORD_EXPANSION);
    expansion_disable(d->expansion);
    if(furi_hal_serial_control_is_busy(channel)) {
        devboard_set_error(d, "Serial port busy");
        goto fail_expansion;
    }
    d->rx = furi_stream_buffer_alloc(DEVBOARD_RX_SIZE, 1);
    d->serial = furi_hal_serial_control_acquire(channel);
    if(!d->serial) {
        devboard_set_error(d, "Serial port unavailable");
        furi_stream_buffer_free(d->rx);
        d->rx = NULL;
        goto fail_expansion;
    }
    furi_hal_serial_init(d->serial, DEVBOARD_BAUD);
    furi_hal_serial_enable_direction(d->serial, FuriHalSerialDirectionRx);
    furi_hal_serial_async_rx_start(d->serial, devboard_rx_callback, d, false);
    furi_hal_serial_tx_wait_complete(d->serial);
    return true;

fail_expansion:
    expansion_enable(d->expansion);
    furi_record_close(RECORD_EXPANSION);
    d->expansion = NULL;
    return false;
}

static inline void devboard_close(DevBoard* d) {
    if(d->serial) {
        furi_hal_serial_async_rx_stop(d->serial);
        furi_hal_serial_disable_direction(d->serial, FuriHalSerialDirectionRx);
        furi_hal_serial_control_release(d->serial);
        furi_hal_serial_deinit(d->serial);
        d->serial = NULL;
    }
    if(d->rx) {
        furi_stream_buffer_free(d->rx);
        d->rx = NULL;
    }
    if(d->expansion) {
        expansion_enable(d->expansion);
        furi_record_close(RECORD_EXPANSION);
        d->expansion = NULL;
    }
}

static inline void devboard_flush(DevBoard* d) {
    uint8_t tmp[64];
    while(furi_stream_buffer_receive(d->rx, tmp, sizeof(tmp), 0) > 0) {
    }
}

static inline void devboard_send(DevBoard* d, const char* command) {
    furi_hal_serial_tx(d->serial, (const uint8_t*)command, strlen(command));
    furi_hal_serial_tx(d->serial, (const uint8_t*)"\n", 1);
    furi_hal_serial_tx_wait_complete(d->serial);
}

static inline bool devboard_read_line(DevBoard* d, char* out, size_t cap, uint32_t timeout_ms) {
    size_t n = 0;
    uint32_t start = furi_get_tick();
    while(true) {
        uint32_t elapsed = furi_get_tick() - start;
        if(elapsed >= timeout_ms || devboard_cancelled(d)) return false;
        uint8_t byte;
        if(furi_stream_buffer_receive(d->rx, &byte, 1, furi_ms_to_ticks(timeout_ms - elapsed)) == 1) {
            if(byte == '\n') {
                while(n > 0 && out[n - 1] == '\r')
                    n--;
                out[n] = '\0';
                return true;
            }
            if(n + 1 < cap) out[n++] = (char)byte;
        }
    }
}

static inline bool devboard_ping(DevBoard* d) {
    char line[96];
    for(int attempt = 0; attempt < 3 && !devboard_cancelled(d); attempt++) {
        devboard_flush(d);
        devboard_send(d, "[PING]");
        uint32_t start = furi_get_tick();
        while(furi_get_tick() - start < 2000) {
            if(devboard_read_line(d, line, sizeof(line), 500) && strstr(line, "[PONG]")) {
                return true;
            }
        }
    }
    devboard_set_error(d, "No WiFi Devboard answer");
    return false;
}

/** GET a URL and return the body as text (NUL terminated). The body must fit in cap - 1 bytes. */
static inline bool devboard_get_text(DevBoard* d, const char* url, char* out, size_t cap) {
    char cmd[480];
    int n = snprintf(
        cmd,
        sizeof(cmd),
        "[GET/BYTES]{\"url\":\"%s\",\"headers\":{\"Content-Type\":\"application/octet-stream\"}}",
        url);
    if(n < 0 || n >= (int)sizeof(cmd)) {
        devboard_set_error(d, "URL too long");
        return false;
    }
    devboard_flush(d);
    d->overflow = 0;
    devboard_send(d, cmd);

    char line[200];
    uint32_t status = 0, length = 0;
    bool header = false;
    uint32_t start = furi_get_tick();
    while(furi_get_tick() - start < 45000 && !devboard_cancelled(d)) {
        if(!devboard_read_line(d, line, sizeof(line), 1000)) continue;
        if(strstr(line, "[GET/SUCCESS]")) {
            const char* sc = strstr(line, "\"Status-Code\":");
            const char* cl = strstr(line, "\"Content-Length\":");
            status = sc ? (uint32_t)atoi(sc + 14) : 0;
            length = cl ? (uint32_t)atoi(cl + 17) : 0;
            header = true;
            break;
        }
        if(strstr(line, "ERROR") || strstr(line, "Failed")) {
            devboard_set_error(d, line);
            return false;
        }
    }
    if(!header) {
        devboard_set_error(d, devboard_cancelled(d) ? "Cancelled" : "No reply (WiFi down?)");
        return false;
    }
    if(status != 200) {
        char msg[40];
        snprintf(msg, sizeof(msg), "Server answered %lu", (unsigned long)status);
        devboard_set_error(d, msg);
        return false;
    }
    if(length == 0 || length >= cap) {
        devboard_set_error(d, "Unexpected reply size");
        return false;
    }

    size_t got = 0;
    uint32_t last = furi_get_tick();
    while(got < length && !devboard_cancelled(d)) {
        size_t r = furi_stream_buffer_receive(
            d->rx, (uint8_t*)out + got, length - got, furi_ms_to_ticks(500));
        if(r > 0) {
            got += r;
            last = furi_get_tick();
        } else if(furi_get_tick() - last > DEVBOARD_IDLE_MS) {
            devboard_set_error(d, "Timed out");
            return false;
        }
    }
    out[got] = '\0';
    if(got != length || d->overflow) {
        devboard_set_error(d, "Data lost in transfer");
        return false;
    }
    // trailer
    for(int i = 0; i < 4; i++) {
        if(!devboard_read_line(d, line, sizeof(line), 1200) || strstr(line, "[GET/END]")) break;
    }
    return true;
}
