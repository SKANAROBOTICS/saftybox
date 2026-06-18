#include "test_util.h"
#include "packet.h"

int main()
{
    uint8_t buf[PACKET_LEN];
    uint8_t s; uint32_t nonce;
    uint8_t n; uint32_t R;

    // ── Challenge encode / decode ─────────────────────────────────────────────
    SUITE("packet: challenge round-trip");

    packet_encode_challenge(0x5, 0xABCDE, buf);
    packet_decode_challenge(buf, s, nonce);
    CHECK_EQ(s,     0x5);
    CHECK_EQ(nonce, 0xABCDEu);

    packet_encode_challenge(0x0, 0x00000, buf);
    packet_decode_challenge(buf, s, nonce);
    CHECK_EQ(s,     0x0);
    CHECK_EQ(nonce, 0x00000u);

    packet_encode_challenge(0xF, 0xFFFFF, buf);
    packet_decode_challenge(buf, s, nonce);
    CHECK_EQ(s,     0xFu);
    CHECK_EQ(nonce, 0xFFFFFu);

    // Status only uses 4 bits; top nibble of status must not bleed into nonce
    packet_encode_challenge(0xF, 0x00001, buf);
    packet_decode_challenge(buf, s, nonce);
    CHECK_EQ(s,     0xFu);
    CHECK_EQ(nonce, 0x00001u);

    // Nonce only uses 20 bits; overflow bit must not bleed into status
    packet_encode_challenge(0x0, 0xFFFFF, buf);
    packet_decode_challenge(buf, s, nonce);
    CHECK_EQ(nonce, 0xFFFFFu);
    CHECK_EQ(s,     0x0);

    // ── Response encode / decode ──────────────────────────────────────────────
    SUITE("packet: response round-trip");

    packet_encode_response(9, 0x12345, buf);
    packet_decode_response(buf, n, R);
    CHECK_EQ(n, 9u);
    CHECK_EQ(R, 0x12345u);

    packet_encode_response(0, 0x00000, buf);
    packet_decode_response(buf, n, R);
    CHECK_EQ(n, 0u);
    CHECK_EQ(R, 0x00000u);

    packet_encode_response(0xF, 0xFFFFF, buf);
    packet_decode_response(buf, n, R);
    CHECK_EQ(n, 0xFu);
    CHECK_EQ(R, 0xFFFFFu);

    // n and R must not bleed into each other
    packet_encode_response(0xF, 0x00001, buf);
    packet_decode_response(buf, n, R);
    CHECK_EQ(n, 0xFu);
    CHECK_EQ(R, 0x00001u);

    packet_encode_response(0x0, 0xFFFFF, buf);
    packet_decode_response(buf, n, R);
    CHECK_EQ(n, 0x0u);
    CHECK_EQ(R, 0xFFFFFu);

    // ── Both directions use exactly 3 bytes ───────────────────────────────────
    SUITE("packet: wire size");
    CHECK_EQ(PACKET_LEN, 3u);

    RESULTS();
}
