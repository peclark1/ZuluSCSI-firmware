// Safe host-only regression tests for the online empty-tape name/quiet gate.
// No SD card or target hardware is accessed by these tests.
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../src/ZuluSCSI_tape_create.h"

static void accepts(const char *given, const char *expected)
{
    char output[65] = {};
    assert(tapeCreateNormalizeName(given, output, sizeof(output)));
    assert(strcmp(output, expected) == 0);
}

static void rejects(const char *given)
{
    char output[65] = {'X', '\0'};
    assert(!tapeCreateNormalizeName(given, output, sizeof(output)));
    assert(output[0] == '\0');
}

int main()
{
    accepts("v2r1-dist-tape-created-on-b10.tap",
            "v2r1-dist-tape-created-on-b10.tap");
    accepts("next-backup", "next-backup.tap");
    accepts("B10_SAVE.TAP", "B10_SAVE.TAP");
    accepts("archive.2026", "archive.2026.tap");
    accepts("a", "a.tap");

    rejects("");
    rejects(".");
    rejects("..");
    rejects("../danger.tap");
    rejects("path/name.tap");
    rejects("path\\name.tap");
    rejects(".hidden.tap");
    rejects("-hidden.tap");
    rejects("has space.tap");
    rejects("foo..tap");
    rejects("foo.");
    rejects("foo\nbar");
    rejects("name:device.tap");
    rejects("longlonglonglonglonglonglonglonglonglonglonglonglonglonglonglonglong.tap");
    char tiny[5] = {};
    assert(!tapeCreateNormalizeName("sample", tiny, sizeof(tiny)));

    assert(!tapeCreateQuietEnough(11000, 10000, 3000));
    assert(tapeCreateQuietEnough(13000, 10000, 3000));
    assert(tapeCreateQuietEnough(5, UINT32_MAX - 3000, 3000));
    assert(!tapeCreateQuietEnough(5, UINT32_MAX - 10, 3000));
    return 0;
}
