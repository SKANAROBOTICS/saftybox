#pragma once
#include "lib/protocol_defs.h"
#include <cstdio>
#include <cstring>

// Load MAC_KEY_LEN bytes from a binary key file.
// Returns true on success; prints an error and returns false otherwise.
static inline bool key_file_load(const char* path, uint8_t key[MAC_KEY_LEN])
{
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "error: cannot open key file '%s'\n", path);
        fprintf(stderr, "       generate one with:  ./keygen %s\n", path);
        return false;
    }
    size_t n = fread(key, 1, MAC_KEY_LEN, f);
    fclose(f);
    if (n != MAC_KEY_LEN) {
        fprintf(stderr, "error: key file '%s' is %zu bytes, expected %zu\n",
                path, n, (size_t)MAC_KEY_LEN);
        return false;
    }
    return true;
}
