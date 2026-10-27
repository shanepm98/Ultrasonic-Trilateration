/*! \file cc_proto.c
 *
 * \brief Implementation of the ESP-NOW message format. See cc_proto.h.
 */

#include "cc_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_nibble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Parse one hex field (optional "0x") ending at \a stop; returns the end pointer or NULL. */
static const char *parse_hex_field(const char *s, char stop, unsigned long max, unsigned long *out) {
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	if (hex_nibble(*s) < 0) return NULL;
	char         *end;
	unsigned long v = strtoul(s, &end, 16);
	if (*end != stop || v > max) return NULL;
	*out = v;
	return end;
}

size_t cc_proto_format_write(char *out, uint8_t reg, uint8_t value) {
	return (size_t)sprintf(out, "%02X:%02X", reg, value);
}

bool cc_proto_parse_write(const char *msg, uint8_t *reg, uint8_t *value) {
	unsigned long r, v;
	const char   *p = parse_hex_field(msg, ':', 0xFF, &r);
	if (p == NULL || parse_hex_field(p + 1, '\0', 0xFF, &v) == NULL) return false;
	if (!cc_radio_addr_writable((uint8_t)r)) return false;
	*reg   = (uint8_t)r;
	*value = (uint8_t)v;
	return true;
}

bool cc_proto_is_dump(const char *msg) {
	return strcmp(msg, CC_PROTO_DUMP_CMD) == 0;
}

size_t cc_proto_format_regs(char *out, const uint8_t dump[CC_RADIO_DUMP_LEN]) {
	char *p = out + sprintf(out, "%s", CC_PROTO_REGS_PREFIX);
	for (int i = 0; i < CC_RADIO_DUMP_LEN; i++) {
		p += sprintf(p, "%02X", dump[i]);
	}
	return (size_t)(p - out);
}

bool cc_proto_parse_regs(const char *msg, uint8_t dump[CC_RADIO_DUMP_LEN]) {
	size_t plen = strlen(CC_PROTO_REGS_PREFIX);
	if (strlen(msg) != CC_PROTO_REGS_LEN || strncmp(msg, CC_PROTO_REGS_PREFIX, plen) != 0) return false;
	const char *h = msg + plen;
	for (int i = 0; i < CC_RADIO_DUMP_LEN; i++, h += 2) {
		int hi = hex_nibble(h[0]), lo = hex_nibble(h[1]);
		if (hi < 0 || lo < 0) return false;
		dump[i] = (uint8_t)((hi << 4) | lo);
	}
	return true;
}
