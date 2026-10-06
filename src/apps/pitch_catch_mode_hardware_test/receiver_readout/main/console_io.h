/*! \file console_io.h
 *
 * \brief Echoing console input over the UART0 serial monitor, for the readout's batch prompts
 * (copied from ir_accuracy_test/receiver, plus console_read_line()). The ESP-IDF UART console is non-blocking (getchar() returns EOF when no byte is
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
 * \brief Prompt for a line of printable ASCII text and read it with the same echoing line editor.
 * Input beyond len - 1 characters is ignored. Re-prompts until the line is non-empty.
 *
 * \param buf  output buffer, always NUL-terminated
 * \param len  size of buf in bytes (must be >= 2)
 */
void console_read_line(const char *prompt, char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CONSOLE_IO_H_ */
