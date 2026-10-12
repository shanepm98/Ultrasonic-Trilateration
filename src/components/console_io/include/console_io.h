/*! \file console_io.h
 *
 * \brief Single-keypress console input over the UART0 serial monitor, for app menus (e.g. the IR
 * receiver's menu and timing tuner, the CC1101 transmitter's menu). The ESP-IDF UART console is non-blocking (getchar() returns EOF when no byte is
 * waiting), so console_getc() polls with a short task delay instead of spinning.
 */

#ifndef CONSOLE_IO_H_
#define CONSOLE_IO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! \brief Make stdin unbuffered so each keypress is readable immediately. Call once at startup. */
void console_init(void);

/*! \brief Block until a byte arrives on the console, and return it. Requires console_init(). */
int console_getc(void);

/*! \brief True for the Enter key (CR or LF, depending on the monitor's line-ending setting). */
bool console_is_enter(int c);

/*! \brief True for the Backspace key (BS or DEL, depending on the terminal). */
bool console_is_backspace(int c);

/*!
 * \brief Prompt for an unsigned decimal number and read it with a small echoing line editor
 * (digits, Backspace deletes, Enter commits). Re-prompts until the value is within [min, max].
 */
uint32_t console_read_uint(const char *prompt, uint32_t min, uint32_t max);

/*!
 * \brief Prompt for a line of printable text and read it with the same echoing line editor
 * (Backspace deletes, Enter commits). At most \a size - 1 characters are kept; \a buf is always
 * NUL-terminated. An empty line is allowed.
 *
 * \return the number of characters read
 */
size_t console_read_line(const char *prompt, char *buf, size_t size);

/*!
 * \brief Prompt for a hexadecimal byte string (e.g. "0xAAAAAA" or "aaaaaa"; the "0x" is
 * optional). An odd digit count is left-padded with a zero nibble ("ABC" -> 0x0A 0xBC). Re-prompts
 * on empty entry, a non-hex character, or more than \a max bytes.
 *
 * \return the number of bytes written to \a buf (1..max)
 */
size_t console_read_hex(const char *prompt, uint8_t *buf, size_t max);

/*! \brief Prompt for one hexadecimal value in [min, max] (e.g. "0x1B" or "1b"); re-prompts until valid. */
uint32_t console_read_hex_uint(const char *prompt, uint32_t min, uint32_t max);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_IO_H_ */
