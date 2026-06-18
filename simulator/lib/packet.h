#pragma once
#include "protocol_defs.h"

// Challenge:  robot → home   [ status:4 | nonce:20 ]   3 bytes
// Response:   home  → robot  [ n:4      | R:20     ]   3 bytes

void    packet_encode_challenge(uint8_t status, uint32_t nonce,
                                uint8_t out[PACKET_LEN]);
bool    packet_decode_challenge(const uint8_t in[PACKET_LEN],
                                uint8_t& status_out, uint32_t& nonce_out);

void    packet_encode_response(uint8_t n, uint32_t R,
                               uint8_t out[PACKET_LEN]);
bool    packet_decode_response(const uint8_t in[PACKET_LEN],
                               uint8_t& n_out, uint32_t& R_out);
