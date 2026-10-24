// Writing a new application image into flash, block by block, as it arrives
// over DFU. Hardware access only through common/flash.h, so the logic can be
// tested on a PC (tests/test_bootloader.c).
#ifndef DFU_IMAGE_H
#define DFU_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

#define DFU_BLOCK_SIZE 1024u // must match CFG_TUD_DFU_XFER_BUFSIZE

typedef enum {
  IMG_OK = 0,
  IMG_ERR_TARGET,   // not an application image for this device
  IMG_ERR_ADDRESS,  // does not fit into the application area
  IMG_ERR_WRITE,    // flash erase/program failed
  IMG_ERR_VERIFY,   // read-back mismatch
  IMG_ERR_FIRMWARE, // nothing (valid) was downloaded
} img_status_t;

// Stack pointer and reset vector look like an application linked at APP_BASE
bool img_vectors_valid(const uint32_t* v);
bool img_app_valid(void);

img_status_t img_write_block(uint16_t block_num, const uint8_t* data, uint16_t length);
// End of download: writes the first 8 bytes, making the application bootable
img_status_t img_finish(void);
void img_abort(void);
uint16_t img_read_block(uint16_t block_num, uint8_t* data, uint16_t length);

// Called around long flash operations (watchdog)
void img_feed_watchdog(void);

#endif
