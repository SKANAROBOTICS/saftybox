#include "../lib/protocol_defs.h"
#include <cstdio>
#include <cstdlib>

int main(int argc, char* argv[])
{
    const char* path = (argc > 1) ? argv[1] : "key.bin";

    uint8_t key[MAC_KEY_LEN];
    FILE* rng = fopen("/dev/urandom", "rb");
    if (!rng) { perror("/dev/urandom"); return 1; }
    if (fread(key, 1, MAC_KEY_LEN, rng) != MAC_KEY_LEN) {
        fprintf(stderr, "error: short read from /dev/urandom\n");
        fclose(rng);
        return 1;
    }
    fclose(rng);

    FILE* out = fopen(path, "wb");
    if (!out) { perror(path); return 1; }
    fwrite(key, 1, MAC_KEY_LEN, out);
    fclose(out);

    printf("wrote %zu-byte key to %s\n", (size_t)MAC_KEY_LEN, path);
    printf("key: ");
    for (size_t i = 0; i < MAC_KEY_LEN; i++) printf("%02X", key[i]);
    printf("\n");
    return 0;
}
