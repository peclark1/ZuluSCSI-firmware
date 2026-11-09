// Experimental, read-only USB CDC SIMH tape export.
// Copyright (c) 2026, licensed under GPL-3.0-or-later.
// A stop-and-wait host protocol: one OPEN or READ command at a time.
// All SD reads run in small bounded chunks on the normal SCSI service loop.

#include "ZuluSCSI_usb_tape_export.h"
#include "ZuluSCSI_tape_export_protocol.h"
#include "ZuluSCSI_live_monitor.h"
#include "ZuluSCSI_platform.h"
#include "ZuluSCSI_control_api.h"
#include <scsi.h>
#include <scsi2sd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool s_active = false;
static bool s_exit_after_drain = false;
static uint32_t s_last_command_ms = 0;
static uint32_t s_last_image_activity_ms = 0;
static LiveImageIoCounters s_prior[S2S_MAX_TARGETS] = {};

static uint8_t s_tx[ZTAPE_EXPORT_HEADER_SIZE + ZTAPE_EXPORT_MAX_CHUNK] = {};
static size_t s_tx_len = 0;
static size_t s_tx_pos = 0;

static char s_command[160] = {};
static size_t s_command_len = 0;
static bool s_command_overflow = false;

enum TapeExportPending { PENDING_NONE, PENDING_OPEN, PENDING_READ };
static TapeExportPending s_pending = PENDING_NONE;
static uint8_t s_pending_id = 0;
static char s_pending_name[121] = {};
static uint64_t s_pending_offset = 0;
static uint16_t s_pending_length = 0;

// This small gate is a heuristic, not an interrupt-safe SD arbiter.
// A new SCSI selection can still arrive during the synchronous FAT read.
static const uint32_t TAPE_EXPORT_QUIET_MS = 20;
static const uint32_t TAPE_EXPORT_SESSION_TIMEOUT_MS = 180000;

static void queueReply(const char *format, const char *value = nullptr)
{
    if (s_tx_len != s_tx_pos) return;
    const int count = value ?
        snprintf((char *)s_tx, sizeof(s_tx), format, value) :
        snprintf((char *)s_tx, sizeof(s_tx), "%s", format);
    s_tx_pos = 0;
    s_tx_len = (count > 0 && (size_t)count < sizeof(s_tx)) ?
               (size_t)count : 0;
}

static const char *failureCode(ControlTapeExportResult result)
{
    switch (result)
    {
        case CONTROL_TAPE_EXPORT_BAD_REQUEST: return "BAD_REQUEST";
        case CONTROL_TAPE_EXPORT_NOT_TAPE: return "NOT_TAPE";
        case CONTROL_TAPE_EXPORT_NOT_FOUND: return "NOT_FOUND";
        case CONTROL_TAPE_EXPORT_IN_USE: return "IN_USE";
        case CONTROL_TAPE_EXPORT_IO_ERROR: return "IO_ERROR";
        case CONTROL_TAPE_EXPORT_CHANGED: return "CHANGED";
        default: return "UNKNOWN";
    }
}

static void stopExport()
{
    controlTapeExportClose();
    s_active = false;
    s_exit_after_drain = false;
    s_pending = PENDING_NONE;
    s_tx_len = s_tx_pos = 0;
    s_command_len = 0;
    s_command_overflow = false;
    log_unlock(); // USB log messages are not mixed with data packets.
}

bool serialTapeExportActive()
{
    return s_active;
}

void serialTapeExportEnter()
{
    if (s_active) return;
    controlTapeExportClose();
    s_active = true;
    s_exit_after_drain = false;
    s_pending = PENDING_NONE;
    s_command_len = 0;
    s_command_overflow = false;
    s_tx_len = s_tx_pos = 0;
    s_last_command_ms = s_last_image_activity_ms = millis();
    for (uint8_t id = 0; id < S2S_MAX_TARGETS; ++id)
        s_prior[id] = liveImageIoSnapshot(id);

    // The ordinary log ring still records log messages. Only its USB
    // transmission is suspended so a binary packet cannot be corrupted.
    log_lock();
    queueReply("ZT1 READY\n");
}

static void processCommand(const char *line)
{
    if (strcmp(line, "QUIT") == 0)
    {
        controlTapeExportClose();
        s_pending = PENDING_NONE;
        s_exit_after_drain = true;
        queueReply("ZT1 BYE\n");
        return;
    }
    if (strcmp(line, "CLOSE") == 0)
    {
        controlTapeExportClose();
        s_pending = PENDING_NONE;
        queueReply("ZT1 CLOSED\n");
        return;
    }
    if (strcmp(line, "PING") == 0)
    {
        queueReply("ZT1 PONG\n");
        return;
    }

    char trailing;
    unsigned id = 0;
    char filename[121];
    if (sscanf(line, "OPEN %u %120s %c", &id, filename, &trailing) == 2 &&
        id < S2S_MAX_TARGETS)
    {
        controlTapeExportClose();
        s_pending = PENDING_OPEN;
        s_pending_id = (uint8_t)id;
        snprintf(s_pending_name, sizeof(s_pending_name), "%s", filename);
        return;
    }

    unsigned long long offset;
    unsigned length;
    if (sscanf(line, "READ %llu %u %c", &offset, &length, &trailing) == 2 &&
        length > 0 && length <= ZTAPE_EXPORT_MAX_CHUNK)
    {
        s_pending = PENDING_READ;
        s_pending_offset = (uint64_t)offset;
        s_pending_length = (uint16_t)length;
        return;
    }
    queueReply("ZT1 ERR BAD_REQUEST\n");
}

void serialTapeExportInput(char c)
{
    if (!s_active) return;
    s_last_command_ms = millis();

    // Stop-and-wait: a new command is not accepted until the prior response
    // has fully left the USB CDC transmit queue.
    if (s_pending != PENDING_NONE || s_tx_pos < s_tx_len ||
        s_exit_after_drain)
        return;

    if (c == '\r') return;
    if (c == '\n')
    {
        s_command[s_command_len] = '\0';
        if (s_command_overflow || s_command_len == 0)
            queueReply("ZT1 ERR BAD_REQUEST\n");
        else
            processCommand(s_command);
        s_command_len = 0;
        s_command_overflow = false;
        return;
    }

    if ((unsigned char)c < 32 || (unsigned char)c > 126 ||
        s_command_len >= sizeof(s_command) - 1)
    {
        s_command_overflow = true;
        return;
    }
    if (!s_command_overflow)
        s_command[s_command_len++] = c;
}

void serialTapeExportPoll()
{
    if (!s_active) return;

    const uint32_t now = millis();
    if (!platform_serial_connected() ||
        (uint32_t)(now - s_last_command_ms) > TAPE_EXPORT_SESSION_TIMEOUT_MS)
    {
        stopExport();
        return;
    }

    // The actual serial transmission is nonblocking, at most one CDC
    // fragment per poll and only when the SCSI bus is free.
    if (s_tx_pos < s_tx_len)
    {
        if (scsiDev.phase == BUS_FREE)
        {
            s_tx_pos += platform_write_to_serial(
                s_tx + s_tx_pos, (uint32_t)(s_tx_len - s_tx_pos));
        }
        return;
    }
    if (s_exit_after_drain)
    {
        stopExport();
        return;
    }

    // Track backing-image traffic on *every* target (including all B10 disks).
    bool changed = false;
    for (uint8_t id = 0; id < S2S_MAX_TARGETS; ++id)
    {
        const LiveImageIoCounters current = liveImageIoSnapshot(id);
        if (current.readBytes != s_prior[id].readBytes ||
            current.writeBytes != s_prior[id].writeBytes)
            changed = true;
        s_prior[id] = current;
    }
    if (scsiDev.phase != BUS_FREE || changed)
        s_last_image_activity_ms = now;

    if (s_pending == PENDING_NONE || scsiDev.phase != BUS_FREE ||
        (uint32_t)(now - s_last_image_activity_ms) < TAPE_EXPORT_QUIET_MS)
        return;

    if (s_pending == PENDING_OPEN)
    {
        uint64_t size = 0;
        const ControlTapeExportResult result =
            controlTapeExportOpen(s_pending_id, s_pending_name, &size);
        s_pending = PENDING_NONE;
        if (result != CONTROL_TAPE_EXPORT_OK)
        {
            queueReply("ZT1 ERR %s\n", failureCode(result));
        }
        else
        {
            const int count = snprintf((char *)s_tx, sizeof(s_tx),
                                        "ZT1 SIZE %llu\n",
                                        (unsigned long long)size);
            s_tx_pos = 0;
            s_tx_len = (count > 0 && (size_t)count < sizeof(s_tx)) ?
                       (size_t)count : 0;
        }
        return;
    }

    if (s_pending == PENDING_READ)
    {
        // No additional allocation, seek or SD access while SCSI is busy.
        const ControlTapeExportResult result = controlTapeExportRead(
            s_pending_offset, s_tx + ZTAPE_EXPORT_HEADER_SIZE, s_pending_length);
        if (result == CONTROL_TAPE_EXPORT_OK)
        {
            const uint32_t crc = ztapeCrc32(
                s_tx + ZTAPE_EXPORT_HEADER_SIZE, s_pending_length);
            ztapeEncodeHeader(s_tx, sizeof(s_tx),
                              s_pending_offset, s_pending_length, crc);
            s_tx_len = ZTAPE_EXPORT_HEADER_SIZE + s_pending_length;
            s_tx_pos = 0;
        }
        else
        {
            if (result == CONTROL_TAPE_EXPORT_CHANGED ||
                result == CONTROL_TAPE_EXPORT_IN_USE ||
                result == CONTROL_TAPE_EXPORT_IO_ERROR)
                controlTapeExportClose();
            queueReply("ZT1 ERR %s\n", failureCode(result));
        }
        s_pending = PENDING_NONE;
        return;
    }
}
