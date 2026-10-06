/*
<!-- BEGIN INVN LICENSE -->
TDK InvenSense 5 Clause License

Copyright (c) 2025, Invensense, Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation and/or
other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
may be used to endorse or promote products derived from this software without
specific prior written permission.

4. This software, with or without modification, must only be used with a
TDK InvenSense sensor.

5. Any software provided in binary form under this license, whether embedded
in source code or provided as a compiled library or application, must not
be reverse engineered, decompiled, modified and/or disassembled.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
<!-- END INVN LICENSE -->
*/

#include "icu_init-ext.h"
#include <invn/icu_interface/shasta_pmut_cmds.h>
#include <invn/soniclib/details/ch_common.h>
#include <invn/soniclib/soniclib.h>
#include <invn/soniclib/ch_log.h>

static const ch_api_funcs_t api_funcs = {
		.set_num_samples      = NULL,
		.get_range            = NULL,
		.get_amplitude        = NULL,
		.get_iq_data          = ch_common_get_iq_data,
		.get_amplitude_data   = NULL,
		.mm_to_samples        = ch_common_mm_to_samples,
		.set_data_output      = NULL,
		.set_target_interrupt = NULL,
		.get_target_interrupt = NULL,
		.set_sample_window    = NULL,
		.get_amplitude_avg    = NULL,
		.set_tx_length        = ch_common_set_tx_length,
		.get_tx_length        = ch_common_get_tx_length,
		.algo_specific_api    = NULL,
};

static const ch_calib_funcs_t calib_funcs = {
		.prepare_pulse_timer = ch_common_prepare_pulse_timer,
		.store_pt_result     = ch_common_store_pt_result,
		.store_op_freq       = ch_common_store_op_freq,
		.store_bandwidth     = ch_common_store_bandwidth,
		.store_scalefactor   = ch_common_store_scale_factor,
		.get_locked_state    = ch_common_get_locked_state,
};

static fw_info_t self = {.api_funcs                   = &api_funcs,
                         .calib_funcs                 = &calib_funcs,
                         .fw_includes_sensor_init     = 1,
                         .fw_includes_tx_optimization = 1,
                         .freqCounterCycles           = ICU_COMMON_FREQCOUNTERCYCLES,
                         .freqLockValue               = ICU_COMMON_READY_FREQ_LOCKED,
                         .oversample                  = 0, /* This firmware does not use oversampling */
                         .max_num_thresholds          = 0};

uint8_t icu_init_ext_init(ch_dev_t *dev_ptr, fw_info_t **fw_info)
{
	(void)dev_ptr;

	/* Init firmware-specific function pointers */
	self.fw_text              = icu_init_ext_fw_text;
	self.fw_text_size         = icu_init_ext_text_size;
	self.fw_vec               = icu_init_ext_fw_vec;
	self.fw_vec_size          = icu_init_ext_vec_size;
	self.fw_version_string    = icu_init_ext_version;
	self.ram_init             = get_ram_icu_init_ext_init_ptr();
	self.get_fw_ram_init_size = get_icu_init_ext_fw_ram_init_size;
	self.get_fw_ram_init_addr = get_icu_init_ext_fw_ram_init_addr;

	*fw_info = &self;

	chdrv_disable_mq_sanitize(dev_ptr);

	return 0;
}

uint16_t icu_init_ext_read_addr(ch_dev_t *dev_ptr, uint8_t instr_seq_idx)
{
	uint16_t addr;
	if (instr_seq_idx == 0)
		addr = ICU_INIT_EXT_INSTR_SEQ_0_ADDR;
	else if (instr_seq_idx == 1)
		addr = ICU_INIT_EXT_INSTR_SEQ_1_ADDR;
	else if (instr_seq_idx == 2)
		addr = ICU_INIT_EXT_INSTR_SEQ_2_ADDR;
	else if (instr_seq_idx == 3)
		addr = ICU_INIT_EXT_INSTR_SEQ_3_ADDR;
	else
		return 0;
	uint16_t seq_addr;
	chdrv_read_word(dev_ptr, addr, &seq_addr);
	return seq_addr;
}

void icu_init_ext_write_addr(ch_dev_t *dev_ptr, uint8_t instr_seq_idx, uint16_t seq_addr)
{
	uint16_t addr;
	if (instr_seq_idx == 0)
		addr = ICU_INIT_EXT_INSTR_SEQ_0_ADDR;
	else if (instr_seq_idx == 1)
		addr = ICU_INIT_EXT_INSTR_SEQ_1_ADDR;
	else if (instr_seq_idx == 2)
		addr = ICU_INIT_EXT_INSTR_SEQ_2_ADDR;
	else if (instr_seq_idx == 3)
		addr = ICU_INIT_EXT_INSTR_SEQ_3_ADDR;
	else
		return;  // don't write anything if address isn't valid
	chdrv_write_word(dev_ptr, addr, seq_addr);
}

int icu_init_ext_write_seq(ch_dev_t *dev_ptr, uint16_t addr, const pmut_transceiver_inst_t *seq)
{
	int eof_idx = -1;
	for (int i = 0; i < ICU_INIT_EXT_MAX_INSTR_SEQ_LEN; i++) {
		if (seq[i].cmd_config == PMUT_CMD_EOF) {
			eof_idx = i;
			break;
		}
	}
	if (eof_idx < 0) {
		return 1;
	}
	uint16_t len = ((uint16_t)eof_idx) + 1;
	chdrv_burst_write(dev_ptr, addr, (uint8_t *)seq, 4 * len);
	return 0;
}

int icu_init_ext_trim_rx_length(pmut_transceiver_inst_t *orig_instr_seq, uint8_t odr,
                                pmut_transceiver_inst_t *ext_instr_seq)
{
	uint32_t rx_len         = 0;
	const int num_cfg_instr = sizeof(((measurement_t *)0)->trx_inst) / sizeof(pmut_transceiver_inst_t);
	if (orig_instr_seq != NULL) {
		int rdy_ien_idx = -1;
		for (int i = 0; i < num_cfg_instr; i++) {
			const uint16_t cmd = orig_instr_seq[i].cmd_config & PMUT_CMD_BITS;
			if (cmd == PMUT_CMD_RX) {
				rx_len += orig_instr_seq[i].length;
			} else if (cmd == PMUT_CMD_EOF) {
				// if we hit EOF, then the extension won't run
				uint8_t err = chdrv_adjust_rx_len(orig_instr_seq, odr, rx_len, i);
				if (err) {
					return -2;
				}
				return rx_len >> (11 - odr);
			}
			if ((orig_instr_seq[i].cmd_config & PMUT_RDY_IEN_BITS)) {
				rdy_ien_idx = i;
				break;
			}
		}
		if (rdy_ien_idx == -1) {
			// No EOF or RDY_IEN was found in meas_config. This is invalid.
			return -1;
		}
	}
	if (ext_instr_seq == NULL) {
		return -3;
	}
	for (int i = 0; i < ICU_INIT_EXT_MAX_INSTR_SEQ_LEN; i++) {
		const uint16_t cmd = ext_instr_seq[i].cmd_config & PMUT_CMD_BITS;
		if (cmd == PMUT_CMD_RX) {
			rx_len += ext_instr_seq[i].length;
		} else if (cmd == PMUT_CMD_EOF) {
			uint8_t err = chdrv_adjust_rx_len(ext_instr_seq, odr, rx_len, i);
			if (err) {
				return -2;
			}
			return rx_len >> (11 - odr);
		}
	}
	// No EOF was found in the extension, and there was a RDY_IEN hit in
	// the original config. This is invalid.
	return -1;
}

uint16_t icu_init_ext_get_iq_start_addr(const ch_dev_t *dev_ptr)
{
	return (uint16_t)(uintptr_t) & ((dev_ptr->sens_cfg_addr)->raw.IQdata);
}

#define ODR_TO_FREQ_DIV(odr) (1 << (7 - odr))

#define SEG_TYPE_TO_STR(segment_type) \
	(segment_type == CH_MEAS_SEG_TYPE_COUNT) ? \
			"Count" : \
			((segment_type == CH_MEAS_SEG_TYPE_RX) ? \
	                 "RX   " : \
	                 ((segment_type == CH_MEAS_SEG_TYPE_TX) ? \
	                          "TX   " : \
	                          ((segment_type == CH_MEAS_SEG_TYPE_EOF) ? "EOF  " : "UNKNOWN")))

uint8_t icu_init_ext_display_config_info(ch_dev_t *dev_ptr, pmut_transceiver_inst_t *instr_ext0,
                                         pmut_transceiver_inst_t *instr_ext1, pmut_transceiver_inst_t *instr_ext2,
                                         pmut_transceiver_inst_t *instr_ext3)
{
	uint8_t ch_err  = 0;
	uint8_t dev_num = ch_get_dev_num(dev_ptr);
	/* Display measurement configuration */
	ch_meas_info_t meas_info = {0};
	ch_meas_seg_info_t seg_info;

	for (uint8_t meas_num = 0; meas_num < 4; meas_num++) {
		if (meas_num < 2) {
			ch_meas_get_info(dev_ptr, meas_num, &meas_info);
		}
		pmut_transceiver_inst_t *orig_inst_ptr = NULL;
		pmut_transceiver_inst_t *inst_ext_ptr  = NULL;
		uint8_t odr                            = 0;
		if (meas_num == 0) {
			orig_inst_ptr = (pmut_transceiver_inst_t *)dev_ptr->meas_queue.meas[0].trx_inst;
			inst_ext_ptr  = instr_ext0;
			odr           = dev_ptr->meas_queue.meas[0].odr;
		} else if (meas_num == 1) {
			orig_inst_ptr = (pmut_transceiver_inst_t *)dev_ptr->meas_queue.meas[1].trx_inst;
			inst_ext_ptr  = instr_ext1;
			odr           = dev_ptr->meas_queue.meas[1].odr;
		} else if (meas_num == 2) {
			inst_ext_ptr = instr_ext2;
			odr          = dev_ptr->meas_queue.meas[1].odr;
		} else if (meas_num == 3) {
			inst_ext_ptr = instr_ext3;
			odr          = dev_ptr->meas_queue.meas[1].odr;
		}
		int num_rx_samples = icu_init_ext_trim_rx_length(orig_inst_ptr, odr, inst_ext_ptr);

		if ((meas_num < 2 && meas_info.num_segments == 0) || num_rx_samples <= 0)
			continue;

		meas_info.num_rx_samples = (num_rx_samples >= 0) ? (uint16_t)num_rx_samples : 0;

		uint16_t max_range_mm = 0;
		// ch_meas_samples_to_mm was designed to work on dev_ptr and meas_num
		// instead of taking ODR directly, so we need to supply the appropriate
		// meas_num for each extension.
		if (meas_num == 0) {
			max_range_mm = ch_meas_samples_to_mm(dev_ptr, 0, meas_info.num_rx_samples);
		} else {
			max_range_mm = ch_meas_samples_to_mm(dev_ptr, 1, meas_info.num_rx_samples);
		}

		ch_log_printf("Device %u: Measurement %u Configuration\r\n", dev_num, meas_num);

		ch_log_printf("  Total Samples = %u  (%u mm max range)\r\n", meas_info.num_rx_samples, max_range_mm);

		uint8_t odr_freq_div = ODR_TO_FREQ_DIV(meas_info.odr);

		uint8_t found_eof   = 0;
		int seg_num         = 0;
		int rdy_ien_seg_num = -1;
		if (meas_num > 1) {
			// ext2 and ext3 don't have a preceeding full meas config
			rdy_ien_seg_num = 0;
		}
		char print_buf[2048];
		int buf_offset = 0;
		while (!found_eof && seg_num < ICU_INIT_EXT_MAX_INSTR_SEQ_LEN) {
			if (rdy_ien_seg_num < 0) {
				ch_meas_get_seg_info(dev_ptr, meas_num, seg_num, &seg_info);
			} else {
				pmut_transceiver_inst_t *inst_ptr = &inst_ext_ptr[seg_num - rdy_ien_seg_num];
				ch_inst_get_seg_info(inst_ptr, meas_info.odr, &seg_info);
			}
			buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset, "   Seg %u  %s ", seg_num,
			                       SEG_TYPE_TO_STR(seg_info.type));
			if (seg_info.type == CH_MEAS_SEG_TYPE_RX) {
				buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset, "%3d sample%s  ",
				                       seg_info.num_rx_samples, (seg_info.num_rx_samples == 1 ? " " : "s"));
			} else {
				buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset, "             ");
			}
			buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset, "%5d cycles  ",
			                       seg_info.num_cycles);
			if (seg_info.type == CH_MEAS_SEG_TYPE_TX) {
				buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset,
				                       "Pulse width = %2u  Phase = %u  ", seg_info.tx_pulse_width, seg_info.tx_phase);
			} else if (seg_info.type == CH_MEAS_SEG_TYPE_RX) {
				buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset,
				                       "Gain reduce = %2u  Atten = %u  ", seg_info.rx_gain, seg_info.rx_atten);
			}
			if (seg_info.rdy_int_en) {
				buf_offset      += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset, "Rdy Int  ");
				rdy_ien_seg_num  = seg_num + 1;
			}
			if (seg_info.done_int_en) {
				buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset, "Done Int");
			}
			//printf("buf_offset=%d\r\n", buf_offset);
			buf_offset += snprintf(print_buf + buf_offset, sizeof(print_buf) - buf_offset, "\r\n");
			if (seg_info.type == CH_MEAS_SEG_TYPE_EOF) {
				found_eof = 1;
			}
			seg_num++;
		} /* for (seg_num...) */
		ch_log_printf("  Active Segments = %u\tRate = CH_ODR_FREQ_DIV_%u\r\n", seg_num, odr_freq_div);
		printf("%s", print_buf);
	} /* for(meas_num...) */
	ch_log_printf("\r\n");

	return ch_err;
}

uint8_t icu_init_ext_write_cfg_sequence(ch_dev_t *dev_ptr, uint8_t *seq, uint8_t seq_len)
{
	uint16_t seq_table0 = 0x5554;
	uint16_t seq_table1 = 0x5555;  // these defaults choose meascfg0 for cur_meas=0 and meascfg1 otherwise
	if (seq_len > 16) {
		return 1;
	}
	uint16_t *seq_table;
	ch_log_printf("seq = { ");
	for (int i = 0; i < seq_len; i++) {
		ch_log_printf("%u ", seq[i]);
		seq_table          = (i > 7) ? &seq_table1 : &seq_table0;
		uint8_t bit_pos    = (i % 8) * 2;
		uint8_t val        = seq[i];
		uint16_t bit_mask  = 3 << bit_pos;
		*seq_table        &= ~bit_mask;
		*seq_table        |= (val << bit_pos) & bit_mask;
	}
	ch_log_printf("}\r\n");
	ch_log_printf("seq_table0 = 0x%04x\r\n", seq_table0);
	ch_log_printf("seq_table1 = 0x%04x\r\n", seq_table1);
	chdrv_write_word(dev_ptr, ICU_INIT_EXT_SEQ_TABLE_0_ADDR, seq_table0);
	chdrv_write_word(dev_ptr, ICU_INIT_EXT_SEQ_TABLE_1_ADDR, seq_table1);
	return 0;
}

uint8_t icu_init_ext_update_metadata_from_iq0(ch_dev_t *dev_ptr, ch_iq_sample_t *iq_data, uint8_t *seq, uint8_t seq_len,
                                              uint8_t *cur_meas)
{
	uint16_t *buf_ptr             = (uint16_t *)iq_data;
	const uint16_t next_buf_addr  = *buf_ptr;
	buf_ptr                      += 1;                  // move to next word
	const uint8_t is_valid        = (*buf_ptr) >> 8;    // get MSByte
	*cur_meas                     = (*buf_ptr) & 0xFF;  // get LSByte
	uint8_t last_meas_num;
	if (*cur_meas < seq_len) {
		last_meas_num = seq[*cur_meas];
	} else {
		last_meas_num = (*cur_meas == 0) ? 0 : 1;
	}
	if (is_valid) {
		dev_ptr->last_measurement = last_meas_num;
		dev_ptr->buf_addr         = next_buf_addr;
	}
	// set the first IQ sample to 0 after extracting metadata since many downstream
	// tools expect the first IQ sample to be 0 (ASIC always writes 0 as first sample).
	iq_data[0].i = 0;
	iq_data[0].q = 0;
	return (is_valid) ? 0 : 1;
}