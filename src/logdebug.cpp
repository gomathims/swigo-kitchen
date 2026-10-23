#include "logdebug.h"
#include "logging_manager.h"
#include "mqtt.h"
#include <mbedtls/base64.h>


// Function implementations

void clearDebugLogs() {
    // File logFile = LittleFS.open(debugFileName, "w");
    // if (!logFile) {
    //     Serial.println("Failed to open log file for clearing");
    //     return;
    // }
    // logFile.close();

    // // Reset debugPointers
    // debugPointers.start = 0;
    // debugPointers.end = 0;
    // debugPointers.count = 0;
    // saveDebugPointers();
}

void logDebugAction(DebugAction mode, char *message) {
    // Use the unified logging system
    queueDebugLog(mode, message);
}

const char *getResetReasonString(esp_reset_reason_t reason) {
    switch (reason) {
    case ESP_RST_POWERON:
        return "Power On Reset";
    case ESP_RST_EXT:
        return "External Reset";
    case ESP_RST_SW:
        return "Software Reset";
    case ESP_RST_PANIC:
        return "Exception/Panic Reset";
    case ESP_RST_INT_WDT:
        return "Interrupt Watchdog Reset";
    case ESP_RST_TASK_WDT:
        return "Task Watchdog Reset";
    case ESP_RST_WDT:
        return "Other Watchdog Reset";
    case ESP_RST_DEEPSLEEP:
        return "Deep Sleep Reset";
    case ESP_RST_BROWNOUT:
        return "Brownout Reset";
    case ESP_RST_SDIO:
        return "SDIO Reset";
    default:
        return "Unknown Reset";
    }
}

void logRestartReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    const char *reason_str = getResetReasonString(reason);

    // Write log message to file
    logDebugAction(DebugAction::ESP_RESTART, (char *)reason_str);
    if (esp_reset_reason() == ESP_RST_PANIC) {
        saveCoreDumpToFile();
    }
}

void saveCoreDumpToFile() {
    const char *dumpFileName = "/logs/gen2-dump.bin";
    const esp_partition_t *coredump_part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_COREDUMP, "coredump");
    if (!coredump_part) {
        Serial.println("Core dump partition not found");
        return;
    }

    // Check available space before writing
    size_t totalBytes = LittleFS.totalBytes();
    size_t usedBytes = LittleFS.usedBytes();
    size_t freeBytes = (totalBytes > usedBytes) ? (totalBytes - usedBytes) : 0;
    size_t totalSize = coredump_part->size;

    if (freeBytes < totalSize + 4096) {
        Serial.printf("Not enough space for core dump. Free: %u, Need: %u\n", freeBytes, totalSize);
        // Remove old dump if exists to make space
        LittleFS.remove(dumpFileName);
        freeBytes = (LittleFS.totalBytes() > LittleFS.usedBytes()) ? (LittleFS.totalBytes() - LittleFS.usedBytes()) : 0;
        if (freeBytes < totalSize + 4096) {
            Serial.println("Still not enough space after cleanup, skipping core dump");
            return;
        }
    }

    // Overwrite previous dump instead of appending
    File file = LittleFS.open(dumpFileName, FILE_WRITE);
    if (!file) {
        Serial.println("Failed to open coredump.bin for writing");
        return;
    }

    uint8_t buffer[512];
    size_t bytesWritten = 0;

    for (size_t offset = 0; offset < totalSize; offset += sizeof(buffer)) {
        size_t toRead = min(sizeof(buffer), totalSize - offset);
        esp_err_t err = esp_partition_read(coredump_part, offset, buffer, toRead);
        if (err != ESP_OK) {
            Serial.printf("Error reading partition at offset %u: %s\n", offset, esp_err_to_name(err));
            break;
        }
        size_t written = file.write(buffer, toRead);
        if (written != toRead) {
            Serial.printf("Write failed at offset %u (wrote %u of %u)\n", offset, written, toRead);
            break;
        }
        bytesWritten += written;
    }

    file.close();
    Serial.printf("Core dump saved to %s (%u bytes)\n", dumpFileName, bytesWritten);
}

// ---------- Core-dump MQTT streaming ----------
// Chunk size chosen so base64-encoded payload + JSON wrapper fits in
// MAX_PAYLOAD_LENGTH (1024). 512 raw -> 684 b64 chars + ~50 wrapper.
static const size_t COREDUMP_CHUNK_RAW = 512;
static const char  *kCoreDumpFileName  = "/logs/gen2-dump.bin";

static bool     cdActive       = false;
static size_t   cdTotalBytes   = 0;
static size_t   cdOffset       = 0;
static uint16_t cdSeq          = 0;
static uint16_t cdTotalChunks  = 0;
static bool     cdFinalSent    = false;

bool hasCoreDumpFile() {
    return LittleFS.exists(kCoreDumpFileName);
}

size_t coreDumpFileSize() {
    if (!hasCoreDumpFile()) return 0;
    File f = LittleFS.open(kCoreDumpFileName, "r");
    if (!f) return 0;
    size_t s = f.size();
    f.close();
    return s;
}

bool isCoreDumpReplayPending() {
    return cdActive;
}

bool queueCoreDumpReplay() {
    if (cdActive) {
        Serial.println("[CDUMP] reject - already streaming");
        return false;
    }
    if (!hasCoreDumpFile()) {
        Serial.println("[CDUMP] no dump file present");
        return false;
    }
    cdTotalBytes  = coreDumpFileSize();
    if (cdTotalBytes == 0) {
        Serial.println("[CDUMP] dump file empty");
        return false;
    }
    cdOffset       = 0;
    cdSeq          = 0;
    cdTotalChunks  = (uint16_t)((cdTotalBytes + COREDUMP_CHUNK_RAW - 1) / COREDUMP_CHUNK_RAW);
    cdFinalSent    = false;
    cdActive       = true;

    // Start marker tells the cloud assembler total size + chunk count up-front.
    char start[160];
    snprintf(start, sizeof(start),
             "{\"coredump\":{\"start\":true,\"size\":%u,\"total\":%u,\"chunk\":%u}}",
             (unsigned)cdTotalBytes, (unsigned)cdTotalChunks, (unsigned)COREDUMP_CHUNK_RAW);
    queueMQTTMessage(propTopic, start, false);

    Serial.printf("[CDUMP] queued: bytes=%u chunks=%u\n",
                  (unsigned)cdTotalBytes, (unsigned)cdTotalChunks);
    return true;
}

void serviceCoreDumpReplay() {
    if (!cdActive) return;
    if (mqttQueue == NULL) return;
    // Live MQTT traffic always wins.
    if (uxQueueMessagesWaiting(mqttQueue) != 0) return;
    if (!mqttClient.connected()) return;
    // Don't fight log replay either — let it drain first.
    if (isReplayPending()) return;

    // Final marker, sent after the last data chunk has cleared the queue.
    if (cdOffset >= cdTotalBytes) {
        if (!cdFinalSent) {
            char done[80];
            snprintf(done, sizeof(done),
                     "{\"coredump\":{\"done\":true,\"total\":%u}}",
                     (unsigned)cdTotalChunks);
            if (queueMQTTMessage(propTopic, done, false)) {
                cdFinalSent = true;
                cdActive    = false;
                Serial.println("[CDUMP] completed");
            }
        }
        return;
    }

    File file = LittleFS.open(kCoreDumpFileName, "r");
    if (!file) {
        Serial.println("[CDUMP] open failed mid-stream — aborting");
        cdActive = false;
        return;
    }
    if (!file.seek(cdOffset)) {
        Serial.printf("[CDUMP] seek failed at offset %u — aborting\n", (unsigned)cdOffset);
        file.close();
        cdActive = false;
        return;
    }

    uint8_t raw[COREDUMP_CHUNK_RAW];
    size_t  toRead = (cdTotalBytes - cdOffset);
    if (toRead > COREDUMP_CHUNK_RAW) toRead = COREDUMP_CHUNK_RAW;
    int read = file.read(raw, toRead);
    file.close();
    if (read <= 0) {
        Serial.printf("[CDUMP] read returned %d — retrying next tick\n", read);
        return;
    }

    // Base64 encode into a stack buffer; mbedtls writes a NUL terminator.
    unsigned char b64[((COREDUMP_CHUNK_RAW + 2) / 3) * 4 + 4];
    size_t b64Len = 0;
    int rc = mbedtls_base64_encode(b64, sizeof(b64), &b64Len, raw, (size_t)read);
    if (rc != 0) {
        Serial.printf("[CDUMP] base64 encode failed rc=%d — aborting\n", rc);
        cdActive = false;
        return;
    }
    b64[b64Len] = 0;

    // Build payload. JSON wrapper ~50 chars + 684 b64 chars => well under MAX_PAYLOAD_LENGTH.
    char payload[MAX_PAYLOAD_LENGTH];
    int n = snprintf(payload, sizeof(payload),
                     "{\"coredump\":{\"seq\":%u,\"total\":%u,\"data\":\"%s\"}}",
                     (unsigned)cdSeq, (unsigned)cdTotalChunks, (const char *)b64);
    if (n <= 0 || n >= (int)sizeof(payload)) {
        Serial.println("[CDUMP] payload overflow — aborting");
        cdActive = false;
        return;
    }

    if (!queueMQTTMessage(propTopic, payload, false)) {
        // Queue rejected (full); leave state untouched and retry next tick.
        return;
    }
    cdOffset += (size_t)read;
    cdSeq++;
}

void publishDumpAvailableAttribute() {
    if (!isCloudConnected) return;
    bool present = hasCoreDumpFile();
    char buf[64];
    if (present) {
        snprintf(buf, sizeof(buf),
                 "{\"dumpAvailable\":true,\"dumpSize\":%u}",
                 (unsigned)coreDumpFileSize());
    } else {
        snprintf(buf, sizeof(buf), "{\"dumpAvailable\":false}");
    }
    queueMQTTMessage(attribTopic, buf, false);
}

// Writes a raw schedule body to the debug log, splitting it across several
// entries only when it does not fit in one.
//
// `mode` ("Cloud", "Portal") is stamped on the FIRST entry only - that is where
// a reader looks for the source of the save, and repeating it on every part
// would spend payload room that the continuation parts need. Continuations
// carry just the [n/m] counter so they can be matched up and a missing tail is
// obvious. A schedule never needs more than 9 parts, so n and m stay single
// digits.
//
// `data` is a raw network buffer with no null terminator, so every read here is
// bounded by len and it is never strlen()-ed.
void logDebugActionPartSplit(const uint8_t *data, size_t len, const char *mode) {
    char chunk[DEBUG_MESSAGE_SIZE];
    const size_t cap = sizeof(chunk) - 1; // usable chars, excluding terminator
    const size_t modeLen = strlen(mode);

    // Fits in one entry: no counter needed, just "mode: <body>".
    if (modeLen + 2 + len <= cap) {
        int headerLen = snprintf(chunk, sizeof(chunk), "%s: ", mode);
        memcpy(chunk + headerLen, data, len);
        chunk[headerLen + len] = '\0';
        logDebugAction(DebugAction::API_SET_SCHEDULE, chunk);
        return;
    }

    // Does not fit - work out the part count. "[n/m] " is 6 chars, and the
    // first part also carries "mode " ahead of it.
    const size_t counterLen = 6;
    const size_t firstCap = cap - (modeLen + 1 + counterLen);
    const size_t restCap = cap - counterLen;
    size_t numParts = 1 + (len - firstCap + restCap - 1) / restCap;

    // Driven by `offset`, not by numParts, so no byte can be dropped if the
    // count above is ever off by one (a 10th part would widen the counter).
    size_t offset = 0;
    for (size_t i = 1; offset < len; i++) {
        int headerLen = (i == 1) ? snprintf(chunk, sizeof(chunk), "%s [%u/%u] ", mode, (unsigned)i, (unsigned)numParts)
                                 : snprintf(chunk, sizeof(chunk), "[%u/%u] ", (unsigned)i, (unsigned)numParts);
        size_t room = cap - headerLen;
        if (room == 0) {
            break; // Header alone fills the entry - cannot make progress.
        }
        size_t chunkLen = (len - offset < room) ? (len - offset) : room;
        memcpy(chunk + headerLen, data + offset, chunkLen);
        chunk[headerLen + chunkLen] = '\0';
        logDebugAction(DebugAction::API_SET_SCHEDULE, chunk);
        offset += chunkLen;
    }
}