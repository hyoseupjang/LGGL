#ifndef LGGL_PROTOCOL_H
#define LGGL_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
/* Return payload length, or zero for unrecognized input. Output needs 283 bytes. */
size_t lggl_reply(uint16_t protocol, const uint8_t *in, size_t len,
                  const uint8_t mac[6], uint64_t uptime, uint8_t out[283],
                  uint16_t *tx_protocol);
#endif
