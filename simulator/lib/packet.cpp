#include "packet.h"

// Challenge: [ status(3:0):4 | nonce(19:16):4 ] [ nonce(15:8):8 ] [ nonce(7:0):8 ]
void packet_encode_challenge(uint8_t status, uint32_t nonce,
                             uint8_t out[PACKET_LEN])
{
    out[0] = ((status  & 0x0F) << 4) | ((nonce >> 16) & 0x0F);
    out[1] = (nonce >> 8) & 0xFF;
    out[2] =  nonce       & 0xFF;
}

bool packet_decode_challenge(const uint8_t in[PACKET_LEN],
                             uint8_t& status_out, uint32_t& nonce_out)
{
    status_out = (in[0] >> 4) & 0x0F;
    nonce_out  = ((uint32_t)(in[0] & 0x0F) << 16)
               | ((uint32_t) in[1]         <<  8)
               |  (uint32_t) in[2];
    return true;
}

// Response: [ n(3:0):4 | R(19:16):4 ] [ R(15:8):8 ] [ R(7:0):8 ]
void packet_encode_response(uint8_t n, uint32_t R,
                            uint8_t out[PACKET_LEN])
{
    out[0] = ((n & 0x0F) << 4) | ((R >> 16) & 0x0F);
    out[1] = (R >> 8) & 0xFF;
    out[2] =  R       & 0xFF;
}

bool packet_decode_response(const uint8_t in[PACKET_LEN],
                            uint8_t& n_out, uint32_t& R_out)
{
    n_out = (in[0] >> 4) & 0x0F;
    R_out = ((uint32_t)(in[0] & 0x0F) << 16)
          | ((uint32_t) in[1]         <<  8)
          |  (uint32_t) in[2];
    return true;
}
