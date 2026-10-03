/*! \file ir_tuner.c
 *
 * \brief Implementation of the IR timing tuner. See ir_tuner.h.
 */

#include "ir_tuner.h"

#include <inttypes.h>
#include <stdio.h>

#include "console_io.h"
#include "ir_common.h" /* IR_MAX_SEGMENT_US */

/* Ask y/n; returns true for y. Echoes the answer. */
static bool ask_yes_no(const char *prompt) {
	printf("%s", prompt);
	fflush(stdout);
	for (;;) {
		int c = console_getc();
		if (c == 'y' || c == 'Y' || c == 'n' || c == 'N') {
			printf("%c\n", c);
			return c == 'y' || c == 'Y';
		}
	}
}

/* Prompt for every value of a new timing config. */
static ir_timing_t enter_timing(void) {
	ir_timing_t t = {0};

	t.pre_enabled = ask_yes_no("Enable pre-burst? (y/n): ");
	if (t.pre_enabled) {
		t.pre_on_us  = console_read_uint("  pre-burst ON time (us): ", 1, IR_MAX_SEGMENT_US);
		t.pre_off_us = console_read_uint("  pre-burst OFF time (us): ", 1, IR_MAX_SEGMENT_US);
	}
	t.trigger_us = console_read_uint("  trigger burst time (us): ", 1, IR_MAX_SEGMENT_US);
	return t;
}

void ir_tuner_run(ir_timing_t *cfg, ir_tuner_fire_fn fire) {
	printf("\n=== IR timing tuner ===\ncurrent: ");
	ir_timing_print(cfg);
	printf("\n");

	for (;;) {
		ir_timing_t pending = enter_timing();

		printf("pending: ");
		ir_timing_print(&pending);
		printf("\n[Enter] save + menu   [Space] fire test trigger   [Backspace] start over\n");

		uint32_t shots = 0;
		for (;;) {
			int c = console_getc();
			if (console_is_enter(c)) {
				*cfg = pending;
				printf("saved: ");
				ir_timing_print(cfg);
				printf("\n");
				return;
			}
			if (c == ' ') {
				shots++;
				bool responded = fire(&pending);
				printf("  test trigger #%" PRIu32 " fired - receiver INT2: %s\n", shots,
				       responded ? "yes" : "NO (timeout)");
				continue;
			}
			if (console_is_backspace(c)) {
				printf("starting over\n");
				break;
			}
			/* any other key is ignored */
		}
	}
}
