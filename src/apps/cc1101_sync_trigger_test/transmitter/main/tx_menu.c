/*! \file tx_menu.c
 *
 * \brief Implementation of the transmitter's console menu. See tx_menu.h.
 *
 * The transmit data and bit rate live in statics here and persist until changed. The bit rate is
 * local only (this node's RMT bit time and CC1101 data rate) - the receivers' async-RX output does
 * not depend on their data-rate setting, so it is not broadcast.
 */

#include "tx_menu.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_timer.h"

#include "cc_link.h"
#include "cc_ook.h"
#include "cc_proto.h"
#include "console_io.h"
#include "ook_tx.h"

#define TX_MAX_BYTES      64
#define TX_DUMP_LISTEN_MS 1000 /* receivers reply within ~100 ms; leave plenty of margin */

static uint8_t  tx_data[TX_MAX_BYTES] = {0xAA, 0xAA, 0xAA};
static size_t   tx_len                = 3;
static uint32_t tx_baud               = CC_OOK_DEFAULT_BAUD; /* requested (RMT) rate */
static uint32_t tx_cc_baud            = CC_OOK_DEFAULT_BAUD; /* rate the CC1101 actually runs at */

static void print_data(void) {
	printf("0x");
	for (size_t i = 0; i < tx_len; i++) {
		printf("%02X", tx_data[i]);
	}
}

static void print_menu(void) {
	printf("\n--- CC1101 OOK transmitter ---  data ");
	print_data();
	printf(" (%u bits), %" PRIu32 ".%03" PRIu32 " kBaud (CC1101 %" PRIu32 " Baud)\n", (unsigned)(tx_len * 8),
	       tx_baud / 1000, tx_baud % 1000, tx_cc_baud);
	printf("  1      set transmit data (hex)\n");
	printf("  2      set bit rate (kBaud)\n");
	printf("  SPACE  send\n");
	printf("  3      broadcast a register write to all receivers\n");
	printf("  4      write a local CC1101 register\n");
	printf("  5      read every receiver's registers\n");
	printf("  d      dump local registers\n");
}

static void set_data(void) {
	tx_len = console_read_hex("transmit data (hex, up to 64 bytes): ", tx_data, TX_MAX_BYTES);
	printf("data = ");
	print_data();
	printf("\n");
}

static void set_rate(void) {
	for (;;) {
		char line[16];
		console_read_line("bit rate in kBaud (0.6 - 250): ", line, sizeof(line));
		char  *end;
		double kbaud = strtod(line, &end);
		long   baud  = lround(kbaud * 1000.0);
		if (end == line || *end != '\0' || baud < (long)CC_OOK_MIN_BAUD || baud > (long)CC_OOK_MAX_BAUD) {
			printf("  enter a number from 0.6 to 250\n");
			continue;
		}
		esp_err_t err = cc_ook_set_datarate((uint32_t)baud, &tx_cc_baud);
		if (err == ESP_OK) err = ook_tx_set_baud((uint32_t)baud);
		if (err != ESP_OK) {
			printf("  failed: %s\n", esp_err_to_name(err));
			return;
		}
		tx_baud = (uint32_t)baud;
		printf("rate = %" PRIu32 " Baud (CC1101 %" PRIu32 " Baud)\n", tx_baud, tx_cc_baud);
		return;
	}
}

static void send(void) {
	esp_err_t err = ook_tx_send(tx_data, tx_len);
	if (err == ESP_OK) {
		printf("sent ");
		print_data();
		printf("\n");
	} else {
		printf("send failed: %s\n", esp_err_to_name(err));
	}
}

static void read_reg_value(uint8_t *reg, uint8_t *value) {
	*reg   = (uint8_t)console_read_hex_uint("register (hex, 00-2E, or 3E = PATABLE[1]): ", 0x00, CC1101_PATABLE);
	while (*reg > CC1101_LAST_CONFIG_REG && *reg != CC1101_PATABLE) {
		printf("  0x2F-0x3D are not writable\n");
		*reg = (uint8_t)console_read_hex_uint("register (hex, 00-2E, or 3E = PATABLE[1]): ", 0x00, CC1101_PATABLE);
	}
	*value = (uint8_t)console_read_hex_uint("value (hex): ", 0x00, 0xFF);
}

static void broadcast_write(void) {
	uint8_t reg, value;
	read_reg_value(&reg, &value);
	char      msg[8];
	cc_proto_format_write(msg, reg, value);
	esp_err_t err = cc_link_broadcast(msg);
	printf("broadcast \"%s\": %s\n", msg, esp_err_to_name(err));
}

static void local_write(void) {
	uint8_t reg, value, readback;
	read_reg_value(&reg, &value);
	esp_err_t err = cc_ook_write(reg, value);
	if (err == ESP_OK) err = cc_ook_read(reg, &readback);
	if (err != ESP_OK) {
		printf("write failed: %s\n", esp_err_to_name(err));
		return;
	}
	printf("reg 0x%02X = 0x%02X (read back 0x%02X)\n", reg, value, readback);
}

static void collect_dumps(void) {
	esp_err_t err = cc_link_broadcast(CC_PROTO_DUMP_CMD);
	if (err != ESP_OK) {
		printf("broadcast failed: %s\n", esp_err_to_name(err));
		return;
	}
	printf("waiting %d ms for replies...\n", TX_DUMP_LISTEN_MS);

	int     replies  = 0;
	int64_t deadline = esp_timer_get_time() + (int64_t)TX_DUMP_LISTEN_MS * 1000;
	for (;;) {
		int64_t left_us = deadline - esp_timer_get_time();
		if (left_us <= 0) break;
		static cc_link_msg_t msg; /* large - keep it off the stack */
		if (!cc_link_receive(&msg, pdMS_TO_TICKS(left_us / 1000) + 1)) continue;

		uint8_t dump[CC_OOK_DUMP_LEN];
		if (!cc_proto_parse_regs(msg.text, dump)) continue; /* not a dump reply */
		printf("\nnode %02X:%02X:%02X:%02X:%02X:%02X\n", msg.src[0], msg.src[1], msg.src[2], msg.src[3], msg.src[4],
		       msg.src[5]);
		cc_ook_dump_print_packed(dump);
		replies++;
	}
	printf("%d node(s) replied\n", replies);
}

void tx_menu_run(void) {
	print_menu();
	for (;;) {
		int c = console_getc();
		switch (c) {
		case '1': set_data(); break;
		case '2': set_rate(); break;
		case ' ': send(); break;
		case '3': broadcast_write(); break;
		case '4': local_write(); break;
		case '5': collect_dumps(); break;
		case 'd':
		case 'D': cc_ook_dump_print(); break;
		default:
			if (!console_is_enter(c)) continue; /* ignore stray keys; Enter reprints the menu */
			break;
		}
		if (c != ' ') print_menu(); /* keep SPACE quiet so repeated sends don't scroll the menu */
	}
}
