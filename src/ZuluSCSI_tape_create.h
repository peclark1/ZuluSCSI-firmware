// Safe, small, host-testable helpers for online creation of EMPTY tape images.
// No filesystem access, SCSI commands, dynamic allocation or delays here.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Permit only simple basename characters: no paths, control bytes, spaces,
// special DOS/FAT names or accidental traversal. Accept ".tap" or add it.
// The input is intentionally bounded to 60 characters for USB console use.
inline bool tapeCreateNormalizeName(const char *input, char *output, size_t capacity)
{
    if (!output || capacity == 0)
        return false;
    output[0] = '\0';
    if (!input || !input[0] || input[0] == '.' || input[0] == '-')
        return false;

    const size_t n = strlen(input);
    if (n > 60 || n == 0)
        return false;

    for (size_t i = 0; i < n; i++)
    {
        const char c = input[i];
        const bool allowed = (c >= 'a' && c <= 'z') ||
                             (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') ||
                             c == '_' || c == '-' || c == '.';
        if (!allowed || (c == '.' && i > 0 && input[i - 1] == '.'))
            return false;
    }

    // Reject trailing dots/ambiguous names rather than letting FAT normalize.
    if (input[n - 1] == '.')
        return false;

    const bool has_tap = n >= 4 &&
        input[n - 4] == '.' &&
        (input[n - 3] == 't' || input[n - 3] == 'T') &&
        (input[n - 2] == 'a' || input[n - 2] == 'A') &&
        (input[n - 1] == 'p' || input[n - 1] == 'P');
    const size_t result_len = n + (has_tap ? 0 : 4);
    if (result_len + 1 > capacity)
        return false;
    memcpy(output, input, n);
    if (!has_tap)
        memcpy(output + n, ".tap", 4);
    output[result_len] = '\0';
    return true;
}

// Handle timer rollover naturally. A request waits for a quiet image I/O
// period, not merely for BUS_FREE between two consecutive host commands.
// This is a conservative heuristic; it is NOT a real-time latency guarantee.
inline bool tapeCreateQuietEnough(uint32_t now_ms, uint32_t last_change_ms,
                                  uint32_t required_quiet_ms)
{
    return (uint32_t)(now_ms - last_change_ms) >= required_quiet_ms;
}
