/*! \file console_io.c
 *
 * \brief Implementation of the receiver's console input helpers. See console_io.h.
 */

#include "console_io.h"

#include <inttypes.h>
#include <stdio.h>

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
