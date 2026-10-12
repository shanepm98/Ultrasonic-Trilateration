/*! \file console_io.c
 *
 * \brief Implementation of the console input helpers. See console_io.h.
 */

#include "console_io.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#define CONSOLE_POLL_MS    10
#define CONSOLE_MAX_DIGITS 9 /* fits uint32_t without overflow checks */
#define CONSOLE_ENTER_FOLD_US 20000 /* far shorter than a human double-press */

void console_init(void) {
	setvbuf(stdin, NULL, _IONBF, 0);
}

int console_getc(void) {
	static int64_t last_enter_us = INT64_MIN / 2;
	for (;;) {
		int c = getchar();
		if (c != EOF) {
			if (console_is_enter(c)) {
				/* A terminal sending CR+LF for one Enter delivers two Enter bytes back-to-back (and
				 * libc may already have turned the CR into LF), so drop a second one that arrives
				 * within CONSOLE_ENTER_FOLD_US. */
				int64_t now  = esp_timer_get_time();
				bool    fold = (now - last_enter_us) < CONSOLE_ENTER_FOLD_US;
				last_enter_us = now;
				if (fold) {
					continue;
				}
			}
			return c;
		}
		clearerr(stdin); /* non-blocking read with no data sets the EOF flag; reset it */
		vTaskDelay(pdMS_TO_TICKS(CONSOLE_POLL_MS));
	}
}

bool console_is_enter(int c) {
	return c == '\r' || c == '\n';
}

bool console_is_backspace(int c) {
	return c == '\b' || c == 0x7f;
}

uint32_t console_read_uint(const char *prompt, uint32_t min, uint32_t max) {
	for (;;) {
		printf("%s", prompt);
		fflush(stdout);

		char digits[CONSOLE_MAX_DIGITS + 1];
		int  len = 0;
		for (;;) {
			int c = console_getc();
			if (console_is_enter(c)) {
				break;
			}
			if (console_is_backspace(c)) {
				if (len > 0) {
					len--;
					printf("\b \b"); /* erase the last echoed digit */
					fflush(stdout);
				}
				continue;
			}
			if (c >= '0' && c <= '9' && len < CONSOLE_MAX_DIGITS) {
				digits[len++] = (char)c;
				putchar(c); /* the serial monitor doesn't echo locally */
				fflush(stdout);
			}
			/* anything else is ignored */
		}
		printf("\n");

		if (len == 0) {
			continue; /* empty entry - prompt again */
		}
		uint32_t value = 0;
		for (int i = 0; i < len; i++) {
			value = value * 10 + (uint32_t)(digits[i] - '0');
		}
		if (value >= min && value <= max) {
			return value;
		}
		printf("  out of range - enter %" PRIu32 "..%" PRIu32 "\n", min, max);
	}
}

size_t console_read_line(const char *prompt, char *buf, size_t size) {
	printf("%s", prompt);
	fflush(stdout);

	size_t len = 0;
	for (;;) {
		int c = console_getc();
		if (console_is_enter(c)) {
			break;
		}
		if (console_is_backspace(c)) {
			if (len > 0) {
				len--;
				printf("\b \b");
				fflush(stdout);
			}
			continue;
		}
		if (isprint(c) && len + 1 < size) {
			buf[len++] = (char)c;
			putchar(c);
			fflush(stdout);
		}
	}
	printf("\n");
	buf[len] = '\0';
	return len;
}

static int hex_nibble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Strip an optional "0x"/"0X" prefix and check every remaining character is a hex digit. Returns
 * the digit string, or NULL if it is empty or has a non-hex character. */
static const char *hex_digits(const char *s) {
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
	}
	if (*s == '\0') {
		return NULL;
	}
	for (const char *p = s; *p; p++) {
		if (hex_nibble(*p) < 0) {
			return NULL;
		}
	}
	return s;
}

size_t console_read_hex(const char *prompt, uint8_t *buf, size_t max) {
	for (;;) {
		char line[2 * max + 3 + 1]; /* "0x" + 2 digits per byte + one spare to detect overflow */
		console_read_line(prompt, line, sizeof(line));

		const char *d = hex_digits(line);
		if (d == NULL) {
			printf("  enter hex digits, e.g. 0xAAAAAA\n");
			continue;
		}
		size_t ndig   = strlen(d);
		size_t nbytes = (ndig + 1) / 2;
		if (nbytes > max) {
			printf("  too long - at most %u bytes\n", (unsigned)max);
			continue;
		}
		size_t i = 0;
		if (ndig % 2) {
			buf[i++] = (uint8_t)hex_nibble(*d++); /* odd count: leading nibble stands alone */
		}
		for (; i < nbytes; i++, d += 2) {
			buf[i] = (uint8_t)((hex_nibble(d[0]) << 4) | hex_nibble(d[1]));
		}
		return nbytes;
	}
}

uint32_t console_read_hex_uint(const char *prompt, uint32_t min, uint32_t max) {
	for (;;) {
		char        line[2 + 8 + 1];
		console_read_line(prompt, line, sizeof(line));
		const char *d = hex_digits(line);
		if (d == NULL) {
			printf("  enter a hex value, e.g. 0x1B\n");
			continue;
		}
		uint32_t value = 0;
		for (; *d; d++) {
			value = (value << 4) | (uint32_t)hex_nibble(*d);
		}
		if (value >= min && value <= max) {
			return value;
		}
		printf("  out of range - enter 0x%02" PRIX32 "..0x%02" PRIX32 "\n", min, max);
	}
}
