#include "cobs.h"

size_t cobs_encode(const uint8_t *src, size_t src_len, uint8_t *dst) {
    size_t  read_idx  = 0;
    size_t  write_idx = 1;
    size_t  code_idx  = 0;
    uint8_t code      = 1;

    while (read_idx < src_len) {
        if (src[read_idx] == 0) {
            dst[code_idx] = code;
            code          = 1;
            code_idx      = write_idx++;
            read_idx++;
        } else {
            dst[write_idx++] = src[read_idx++];
            code++;
            if (code == 0xFFu) {
                dst[code_idx] = code;
                code          = 1;
                code_idx      = write_idx++;
            }
        }
    }
    dst[code_idx] = code;
    return write_idx;
}

size_t cobs_decode(const uint8_t *src, size_t src_len, uint8_t *dst, bool *ok) {
    size_t read_idx  = 0;
    size_t write_idx = 0;

    while (read_idx < src_len) {
        uint8_t code = src[read_idx];
        if (code == 0 || read_idx + code > src_len) {
            /* A 0x00 code byte is never valid inside a COBS block, and a code that claims more
             * data than remains means the frame was truncated/corrupted - reject rather than
             * read past the end of src. */
            *ok = false;
            return 0;
        }
        read_idx++;

        for (uint8_t i = 1; i < code; i++) {
            dst[write_idx++] = src[read_idx++];
        }

        /* A code of 0xFF means "254 non-zero bytes, no implied zero" (that's how the encoder
         * avoids ever emitting a zero code byte itself); any other code implies a zero byte
         * originally sat right after this block, unless this was the very last block in the
         * frame. */
        if (code != 0xFFu && read_idx != src_len) {
            dst[write_idx++] = 0;
        }
    }

    *ok = true;
    return write_idx;
}
