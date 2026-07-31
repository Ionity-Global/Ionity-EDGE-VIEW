#ifndef IONITY_OTA_H
#define IONITY_OTA_H

#include <stdint.h>
#include <stdbool.h>

/*
 * Firmware update over WiFi.
 *
 * Two deliberate steps. An image is received into a staging slot in the upper
 * half of flash and checksummed there; nothing touches the running firmware
 * until a separate commit arrives. A failed or interrupted transfer therefore
 * costs nothing but the staging slot.
 *
 * Wire protocol on IONITY_OTA_PORT:
 *   header 16 bytes, little-endian
 *     magic  u32  'IOTA' (0x41544F49)
 *     size   u32  image bytes that follow
 *     crc32  u32  CRC-32 of the image
 *     flags  u32  reserved, send 0
 *   then `size` raw bytes of the flash image (the payload of a UF2, already
 *   unwrapped by the sender, so the device needs no UF2 parser).
 *
 * The device replies with one JSON line per state change:
 *   {"type":"ota","state":"receiving","got":N,"size":M}
 *   {"type":"ota","state":"staged","crc":"ok"}
 *   {"type":"ota","state":"failed","why":"..."}
 *
 * Commit is a stream command, not part of this socket:
 *   {"type":"ota","commit":true}
 *
 * Recovery: an RP2350 cannot be permanently bricked. If a commit is cut short
 * by power loss, hold BOOTSEL and reflash over USB.
 */

#define IONITY_OTA_PORT       4245
#define IONITY_OTA_MAGIC      0x41544F49u

/* Staging lives at 2 MB, well clear of the running image, and stops short of
 * the last sector where the WiFi credentials are kept. */
#define IONITY_OTA_STAGE_OFFSET  (2u * 1024u * 1024u)

typedef enum {
    IONITY_OTA_IDLE = 0,
    IONITY_OTA_RECEIVING,
    IONITY_OTA_STAGED,
    IONITY_OTA_FAILED,
} ionity_ota_state_t;

bool ionity_ota_init(uint16_t port);

ionity_ota_state_t ionity_ota_state(void);
uint32_t ionity_ota_received(void);
uint32_t ionity_ota_expected(void);
const char *ionity_ota_error(void);

/* Largest image the staging slot can hold. */
uint32_t ionity_ota_capacity(void);

/*
 * Copy the staged image over the running one and reboot into it. Only acts
 * when the state is STAGED, so an unverified image can never be committed.
 * Does not return on success.
 */
bool ionity_ota_commit(void);

#endif /* IONITY_OTA_H */
