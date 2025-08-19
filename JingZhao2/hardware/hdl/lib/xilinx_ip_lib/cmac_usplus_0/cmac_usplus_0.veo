// (c) Copyright 1995-2021 Xilinx, Inc. All rights reserved.
// 
// This file contains confidential and proprietary information
// of Xilinx, Inc. and is protected under U.S. and
// international copyright and other intellectual property
// laws.
// 
// DISCLAIMER
// This disclaimer is not a license and does not grant any
// rights to the materials distributed herewith. Except as
// otherwise provided in a valid license issued to you by
// Xilinx, and to the maximum extent permitted by applicable
// law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
// WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
// AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
// BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
// INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
// (2) Xilinx shall not be liable (whether in contract or tort,
// including negligence, or under any other theory of
// liability) for any loss or damage of any kind or nature
// related to, arising under or in connection with these
// materials, including for any direct, or any indirect,
// special, incidental, or consequential loss or damage
// (including loss of data, profits, goodwill, or any type of
// loss or damage suffered as a result of any action brought
// by a third party) even if such damage or loss was
// reasonably foreseeable or Xilinx had been advised of the
// possibility of the same.
// 
// CRITICAL APPLICATIONS
// Xilinx products are not designed or intended to be fail-
// safe, or for use in any application requiring fail-safe
// performance, such as life-support or safety devices or
// systems, Class III medical devices, nuclear facilities,
// applications related to the deployment of airbags, or any
// other applications that could lead to death, personal
// injury, or severe property or environmental damage
// (individually and collectively, "Critical
// Applications"). Customer assumes the sole risk and
// liability of any use of Xilinx products in Critical
// Applications, subject only to applicable laws and
// regulations governing limitations on product liability.
// 
// THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
// PART OF THIS FILE AT ALL TIMES.
// 
// DO NOT MODIFY THIS FILE.

// IP VLNV: xilinx.com:ip:cmac_usplus:2.6
// IP Revision: 0

// The following must be inserted into your Verilog file for this
// core to be instantiated. Change the instance name and port connections
// (in parentheses) to your own signal names.

//----------- Begin Cut here for INSTANTIATION Template ---// INST_TAG
cmac_usplus_0 your_instance_name (
  .gt0_rxp_in(gt0_rxp_in),                                                      // input wire gt0_rxp_in
  .gt0_rxn_in(gt0_rxn_in),                                                      // input wire gt0_rxn_in
  .gt1_rxp_in(gt1_rxp_in),                                                      // input wire gt1_rxp_in
  .gt1_rxn_in(gt1_rxn_in),                                                      // input wire gt1_rxn_in
  .gt2_rxp_in(gt2_rxp_in),                                                      // input wire gt2_rxp_in
  .gt2_rxn_in(gt2_rxn_in),                                                      // input wire gt2_rxn_in
  .gt3_rxp_in(gt3_rxp_in),                                                      // input wire gt3_rxp_in
  .gt3_rxn_in(gt3_rxn_in),                                                      // input wire gt3_rxn_in
  .gt0_txp_out(gt0_txp_out),                                                    // output wire gt0_txp_out
  .gt0_txn_out(gt0_txn_out),                                                    // output wire gt0_txn_out
  .gt1_txp_out(gt1_txp_out),                                                    // output wire gt1_txp_out
  .gt1_txn_out(gt1_txn_out),                                                    // output wire gt1_txn_out
  .gt2_txp_out(gt2_txp_out),                                                    // output wire gt2_txp_out
  .gt2_txn_out(gt2_txn_out),                                                    // output wire gt2_txn_out
  .gt3_txp_out(gt3_txp_out),                                                    // output wire gt3_txp_out
  .gt3_txn_out(gt3_txn_out),                                                    // output wire gt3_txn_out
  .gt_txusrclk2(gt_txusrclk2),                                                  // output wire gt_txusrclk2
  .gt_loopback_in(gt_loopback_in),                                              // input wire [11 : 0] gt_loopback_in
  .gt_ref_clk_out(gt_ref_clk_out),                                              // output wire gt_ref_clk_out
  .gt_rxrecclkout(gt_rxrecclkout),                                              // output wire [3 : 0] gt_rxrecclkout
  .gt_powergoodout(gt_powergoodout),                                            // output wire [3 : 0] gt_powergoodout
  .gtwiz_reset_tx_datapath(gtwiz_reset_tx_datapath),                            // input wire gtwiz_reset_tx_datapath
  .gtwiz_reset_rx_datapath(gtwiz_reset_rx_datapath),                            // input wire gtwiz_reset_rx_datapath
  .ctl_tx_rsfec_enable(ctl_tx_rsfec_enable),                                    // input wire ctl_tx_rsfec_enable
  .ctl_rx_rsfec_enable(ctl_rx_rsfec_enable),                                    // input wire ctl_rx_rsfec_enable
  .ctl_rsfec_ieee_error_indication_mode(ctl_rsfec_ieee_error_indication_mode),  // input wire ctl_rsfec_ieee_error_indication_mode
  .ctl_rx_rsfec_enable_correction(ctl_rx_rsfec_enable_correction),              // input wire ctl_rx_rsfec_enable_correction
  .ctl_rx_rsfec_enable_indication(ctl_rx_rsfec_enable_indication),              // input wire ctl_rx_rsfec_enable_indication
  .stat_rx_rsfec_am_lock0(stat_rx_rsfec_am_lock0),                              // output wire stat_rx_rsfec_am_lock0
  .stat_rx_rsfec_am_lock1(stat_rx_rsfec_am_lock1),                              // output wire stat_rx_rsfec_am_lock1
  .stat_rx_rsfec_am_lock2(stat_rx_rsfec_am_lock2),                              // output wire stat_rx_rsfec_am_lock2
  .stat_rx_rsfec_am_lock3(stat_rx_rsfec_am_lock3),                              // output wire stat_rx_rsfec_am_lock3
  .stat_rx_rsfec_corrected_cw_inc(stat_rx_rsfec_corrected_cw_inc),              // output wire stat_rx_rsfec_corrected_cw_inc
  .stat_rx_rsfec_cw_inc(stat_rx_rsfec_cw_inc),                                  // output wire stat_rx_rsfec_cw_inc
  .stat_rx_rsfec_err_count0_inc(stat_rx_rsfec_err_count0_inc),                  // output wire [2 : 0] stat_rx_rsfec_err_count0_inc
  .stat_rx_rsfec_err_count1_inc(stat_rx_rsfec_err_count1_inc),                  // output wire [2 : 0] stat_rx_rsfec_err_count1_inc
  .stat_rx_rsfec_err_count2_inc(stat_rx_rsfec_err_count2_inc),                  // output wire [2 : 0] stat_rx_rsfec_err_count2_inc
  .stat_rx_rsfec_err_count3_inc(stat_rx_rsfec_err_count3_inc),                  // output wire [2 : 0] stat_rx_rsfec_err_count3_inc
  .stat_rx_rsfec_hi_ser(stat_rx_rsfec_hi_ser),                                  // output wire stat_rx_rsfec_hi_ser
  .stat_rx_rsfec_lane_alignment_status(stat_rx_rsfec_lane_alignment_status),    // output wire stat_rx_rsfec_lane_alignment_status
  .stat_rx_rsfec_lane_fill_0(stat_rx_rsfec_lane_fill_0),                        // output wire [13 : 0] stat_rx_rsfec_lane_fill_0
  .stat_rx_rsfec_lane_fill_1(stat_rx_rsfec_lane_fill_1),                        // output wire [13 : 0] stat_rx_rsfec_lane_fill_1
  .stat_rx_rsfec_lane_fill_2(stat_rx_rsfec_lane_fill_2),                        // output wire [13 : 0] stat_rx_rsfec_lane_fill_2
  .stat_rx_rsfec_lane_fill_3(stat_rx_rsfec_lane_fill_3),                        // output wire [13 : 0] stat_rx_rsfec_lane_fill_3
  .stat_rx_rsfec_lane_mapping(stat_rx_rsfec_lane_mapping),                      // output wire [7 : 0] stat_rx_rsfec_lane_mapping
  .stat_rx_rsfec_uncorrected_cw_inc(stat_rx_rsfec_uncorrected_cw_inc),          // output wire stat_rx_rsfec_uncorrected_cw_inc
  .sys_reset(sys_reset),                                                        // input wire sys_reset
  .gt_ref_clk_p(gt_ref_clk_p),                                                  // input wire gt_ref_clk_p
  .gt_ref_clk_n(gt_ref_clk_n),                                                  // input wire gt_ref_clk_n
  .init_clk(init_clk),                                                          // input wire init_clk
  .rx_axis_tvalid(rx_axis_tvalid),                                              // output wire rx_axis_tvalid
  .rx_axis_tdata(rx_axis_tdata),                                                // output wire [511 : 0] rx_axis_tdata
  .rx_axis_tlast(rx_axis_tlast),                                                // output wire rx_axis_tlast
  .rx_axis_tkeep(rx_axis_tkeep),                                                // output wire [63 : 0] rx_axis_tkeep
  .rx_axis_tuser(rx_axis_tuser),                                                // output wire rx_axis_tuser
  .rx_otn_bip8_0(rx_otn_bip8_0),                                                // output wire [7 : 0] rx_otn_bip8_0
  .rx_otn_bip8_1(rx_otn_bip8_1),                                                // output wire [7 : 0] rx_otn_bip8_1
  .rx_otn_bip8_2(rx_otn_bip8_2),                                                // output wire [7 : 0] rx_otn_bip8_2
  .rx_otn_bip8_3(rx_otn_bip8_3),                                                // output wire [7 : 0] rx_otn_bip8_3
  .rx_otn_bip8_4(rx_otn_bip8_4),                                                // output wire [7 : 0] rx_otn_bip8_4
  .rx_otn_data_0(rx_otn_data_0),                                                // output wire [65 : 0] rx_otn_data_0
  .rx_otn_data_1(rx_otn_data_1),                                                // output wire [65 : 0] rx_otn_data_1
  .rx_otn_data_2(rx_otn_data_2),                                                // output wire [65 : 0] rx_otn_data_2
  .rx_otn_data_3(rx_otn_data_3),                                                // output wire [65 : 0] rx_otn_data_3
  .rx_otn_data_4(rx_otn_data_4),                                                // output wire [65 : 0] rx_otn_data_4
  .rx_preambleout(rx_preambleout),                                              // output wire [55 : 0] rx_preambleout
  .usr_rx_reset(usr_rx_reset),                                                  // output wire usr_rx_reset
  .gt_rxusrclk2(gt_rxusrclk2),                                                  // output wire gt_rxusrclk2
  .rx_lane_aligner_fill_0(rx_lane_aligner_fill_0),                              // output wire [6 : 0] rx_lane_aligner_fill_0
  .rx_lane_aligner_fill_1(rx_lane_aligner_fill_1),                              // output wire [6 : 0] rx_lane_aligner_fill_1
  .rx_lane_aligner_fill_10(rx_lane_aligner_fill_10),                            // output wire [6 : 0] rx_lane_aligner_fill_10
  .rx_lane_aligner_fill_11(rx_lane_aligner_fill_11),                            // output wire [6 : 0] rx_lane_aligner_fill_11
  .rx_lane_aligner_fill_12(rx_lane_aligner_fill_12),                            // output wire [6 : 0] rx_lane_aligner_fill_12
  .rx_lane_aligner_fill_13(rx_lane_aligner_fill_13),                            // output wire [6 : 0] rx_lane_aligner_fill_13
  .rx_lane_aligner_fill_14(rx_lane_aligner_fill_14),                            // output wire [6 : 0] rx_lane_aligner_fill_14
  .rx_lane_aligner_fill_15(rx_lane_aligner_fill_15),                            // output wire [6 : 0] rx_lane_aligner_fill_15
  .rx_lane_aligner_fill_16(rx_lane_aligner_fill_16),                            // output wire [6 : 0] rx_lane_aligner_fill_16
  .rx_lane_aligner_fill_17(rx_lane_aligner_fill_17),                            // output wire [6 : 0] rx_lane_aligner_fill_17
  .rx_lane_aligner_fill_18(rx_lane_aligner_fill_18),                            // output wire [6 : 0] rx_lane_aligner_fill_18
  .rx_lane_aligner_fill_19(rx_lane_aligner_fill_19),                            // output wire [6 : 0] rx_lane_aligner_fill_19
  .rx_lane_aligner_fill_2(rx_lane_aligner_fill_2),                              // output wire [6 : 0] rx_lane_aligner_fill_2
  .rx_lane_aligner_fill_3(rx_lane_aligner_fill_3),                              // output wire [6 : 0] rx_lane_aligner_fill_3
  .rx_lane_aligner_fill_4(rx_lane_aligner_fill_4),                              // output wire [6 : 0] rx_lane_aligner_fill_4
  .rx_lane_aligner_fill_5(rx_lane_aligner_fill_5),                              // output wire [6 : 0] rx_lane_aligner_fill_5
  .rx_lane_aligner_fill_6(rx_lane_aligner_fill_6),                              // output wire [6 : 0] rx_lane_aligner_fill_6
  .rx_lane_aligner_fill_7(rx_lane_aligner_fill_7),                              // output wire [6 : 0] rx_lane_aligner_fill_7
  .rx_lane_aligner_fill_8(rx_lane_aligner_fill_8),                              // output wire [6 : 0] rx_lane_aligner_fill_8
  .rx_lane_aligner_fill_9(rx_lane_aligner_fill_9),                              // output wire [6 : 0] rx_lane_aligner_fill_9
  .rx_ptp_tstamp_out(rx_ptp_tstamp_out),                                        // output wire [79 : 0] rx_ptp_tstamp_out
  .rx_ptp_pcslane_out(rx_ptp_pcslane_out),                                      // output wire [4 : 0] rx_ptp_pcslane_out
  .ctl_rx_systemtimerin(ctl_rx_systemtimerin),                                  // input wire [79 : 0] ctl_rx_systemtimerin
  .stat_rx_bad_code(stat_rx_bad_code),                                          // output wire [2 : 0] stat_rx_bad_code
  .stat_rx_bad_fcs(stat_rx_bad_fcs),                                            // output wire [2 : 0] stat_rx_bad_fcs
  .stat_rx_block_lock(stat_rx_block_lock),                                      // output wire [19 : 0] stat_rx_block_lock
  .stat_rx_fragment(stat_rx_fragment),                                          // output wire [2 : 0] stat_rx_fragment
  .stat_rx_framing_err_0(stat_rx_framing_err_0),                                // output wire [1 : 0] stat_rx_framing_err_0
  .stat_rx_framing_err_1(stat_rx_framing_err_1),                                // output wire [1 : 0] stat_rx_framing_err_1
  .stat_rx_framing_err_10(stat_rx_framing_err_10),                              // output wire [1 : 0] stat_rx_framing_err_10
  .stat_rx_framing_err_11(stat_rx_framing_err_11),                              // output wire [1 : 0] stat_rx_framing_err_11
  .stat_rx_framing_err_12(stat_rx_framing_err_12),                              // output wire [1 : 0] stat_rx_framing_err_12
  .stat_rx_framing_err_13(stat_rx_framing_err_13),                              // output wire [1 : 0] stat_rx_framing_err_13
  .stat_rx_framing_err_14(stat_rx_framing_err_14),                              // output wire [1 : 0] stat_rx_framing_err_14
  .stat_rx_framing_err_15(stat_rx_framing_err_15),                              // output wire [1 : 0] stat_rx_framing_err_15
  .stat_rx_framing_err_16(stat_rx_framing_err_16),                              // output wire [1 : 0] stat_rx_framing_err_16
  .stat_rx_framing_err_17(stat_rx_framing_err_17),                              // output wire [1 : 0] stat_rx_framing_err_17
  .stat_rx_framing_err_18(stat_rx_framing_err_18),                              // output wire [1 : 0] stat_rx_framing_err_18
  .stat_rx_framing_err_19(stat_rx_framing_err_19),                              // output wire [1 : 0] stat_rx_framing_err_19
  .stat_rx_framing_err_2(stat_rx_framing_err_2),                                // output wire [1 : 0] stat_rx_framing_err_2
  .stat_rx_framing_err_3(stat_rx_framing_err_3),                                // output wire [1 : 0] stat_rx_framing_err_3
  .stat_rx_framing_err_4(stat_rx_framing_err_4),                                // output wire [1 : 0] stat_rx_framing_err_4
  .stat_rx_framing_err_5(stat_rx_framing_err_5),                                // output wire [1 : 0] stat_rx_framing_err_5
  .stat_rx_framing_err_6(stat_rx_framing_err_6),                                // output wire [1 : 0] stat_rx_framing_err_6
  .stat_rx_framing_err_7(stat_rx_framing_err_7),                                // output wire [1 : 0] stat_rx_framing_err_7
  .stat_rx_framing_err_8(stat_rx_framing_err_8),                                // output wire [1 : 0] stat_rx_framing_err_8
  .stat_rx_framing_err_9(stat_rx_framing_err_9),                                // output wire [1 : 0] stat_rx_framing_err_9
  .stat_rx_mf_err(stat_rx_mf_err),                                              // output wire [19 : 0] stat_rx_mf_err
  .stat_rx_mf_len_err(stat_rx_mf_len_err),                                      // output wire [19 : 0] stat_rx_mf_len_err
  .stat_rx_mf_repeat_err(stat_rx_mf_repeat_err),                                // output wire [19 : 0] stat_rx_mf_repeat_err
  .stat_rx_packet_small(stat_rx_packet_small),                                  // output wire [2 : 0] stat_rx_packet_small
  .ctl_rx_enable(ctl_rx_enable),                                                // input wire ctl_rx_enable
  .ctl_rx_force_resync(ctl_rx_force_resync),                                    // input wire ctl_rx_force_resync
  .ctl_rx_test_pattern(ctl_rx_test_pattern),                                    // input wire ctl_rx_test_pattern
  .core_rx_reset(core_rx_reset),                                                // input wire core_rx_reset
  .rx_clk(rx_clk),                                                              // input wire rx_clk
  .stat_rx_stomped_fcs(stat_rx_stomped_fcs),                                    // output wire [2 : 0] stat_rx_stomped_fcs
  .stat_rx_synced(stat_rx_synced),                                              // output wire [19 : 0] stat_rx_synced
  .stat_rx_synced_err(stat_rx_synced_err),                                      // output wire [19 : 0] stat_rx_synced_err
  .stat_rx_test_pattern_mismatch(stat_rx_test_pattern_mismatch),                // output wire [2 : 0] stat_rx_test_pattern_mismatch
  .stat_rx_total_bytes(stat_rx_total_bytes),                                    // output wire [6 : 0] stat_rx_total_bytes
  .stat_rx_total_good_bytes(stat_rx_total_good_bytes),                          // output wire [13 : 0] stat_rx_total_good_bytes
  .stat_rx_total_packets(stat_rx_total_packets),                                // output wire [2 : 0] stat_rx_total_packets
  .stat_rx_undersize(stat_rx_undersize),                                        // output wire [2 : 0] stat_rx_undersize
  .stat_rx_pcsl_demuxed(stat_rx_pcsl_demuxed),                                  // output wire [19 : 0] stat_rx_pcsl_demuxed
  .stat_rx_pcsl_number_0(stat_rx_pcsl_number_0),                                // output wire [4 : 0] stat_rx_pcsl_number_0
  .stat_rx_pcsl_number_1(stat_rx_pcsl_number_1),                                // output wire [4 : 0] stat_rx_pcsl_number_1
  .stat_rx_pcsl_number_10(stat_rx_pcsl_number_10),                              // output wire [4 : 0] stat_rx_pcsl_number_10
  .stat_rx_pcsl_number_11(stat_rx_pcsl_number_11),                              // output wire [4 : 0] stat_rx_pcsl_number_11
  .stat_rx_pcsl_number_12(stat_rx_pcsl_number_12),                              // output wire [4 : 0] stat_rx_pcsl_number_12
  .stat_rx_pcsl_number_13(stat_rx_pcsl_number_13),                              // output wire [4 : 0] stat_rx_pcsl_number_13
  .stat_rx_pcsl_number_14(stat_rx_pcsl_number_14),                              // output wire [4 : 0] stat_rx_pcsl_number_14
  .stat_rx_pcsl_number_15(stat_rx_pcsl_number_15),                              // output wire [4 : 0] stat_rx_pcsl_number_15
  .stat_rx_pcsl_number_16(stat_rx_pcsl_number_16),                              // output wire [4 : 0] stat_rx_pcsl_number_16
  .stat_rx_pcsl_number_17(stat_rx_pcsl_number_17),                              // output wire [4 : 0] stat_rx_pcsl_number_17
  .stat_rx_pcsl_number_18(stat_rx_pcsl_number_18),                              // output wire [4 : 0] stat_rx_pcsl_number_18
  .stat_rx_pcsl_number_19(stat_rx_pcsl_number_19),                              // output wire [4 : 0] stat_rx_pcsl_number_19
  .stat_rx_pcsl_number_2(stat_rx_pcsl_number_2),                                // output wire [4 : 0] stat_rx_pcsl_number_2
  .stat_rx_pcsl_number_3(stat_rx_pcsl_number_3),                                // output wire [4 : 0] stat_rx_pcsl_number_3
  .stat_rx_pcsl_number_4(stat_rx_pcsl_number_4),                                // output wire [4 : 0] stat_rx_pcsl_number_4
  .stat_rx_pcsl_number_5(stat_rx_pcsl_number_5),                                // output wire [4 : 0] stat_rx_pcsl_number_5
  .stat_rx_pcsl_number_6(stat_rx_pcsl_number_6),                                // output wire [4 : 0] stat_rx_pcsl_number_6
  .stat_rx_pcsl_number_7(stat_rx_pcsl_number_7),                                // output wire [4 : 0] stat_rx_pcsl_number_7
  .stat_rx_pcsl_number_8(stat_rx_pcsl_number_8),                                // output wire [4 : 0] stat_rx_pcsl_number_8
  .stat_rx_pcsl_number_9(stat_rx_pcsl_number_9),                                // output wire [4 : 0] stat_rx_pcsl_number_9
  .ctl_tx_systemtimerin(ctl_tx_systemtimerin),                                  // input wire [79 : 0] ctl_tx_systemtimerin
  .stat_tx_ptp_fifo_read_error(stat_tx_ptp_fifo_read_error),                    // output wire stat_tx_ptp_fifo_read_error
  .stat_tx_ptp_fifo_write_error(stat_tx_ptp_fifo_write_error),                  // output wire stat_tx_ptp_fifo_write_error
  .tx_ptp_tstamp_valid_out(tx_ptp_tstamp_valid_out),                            // output wire tx_ptp_tstamp_valid_out
  .tx_ptp_pcslane_out(tx_ptp_pcslane_out),                                      // output wire [4 : 0] tx_ptp_pcslane_out
  .tx_ptp_tstamp_tag_out(tx_ptp_tstamp_tag_out),                                // output wire [15 : 0] tx_ptp_tstamp_tag_out
  .tx_ptp_tstamp_out(tx_ptp_tstamp_out),                                        // output wire [79 : 0] tx_ptp_tstamp_out
  .tx_ptp_1588op_in(tx_ptp_1588op_in),                                          // input wire [1 : 0] tx_ptp_1588op_in
  .tx_ptp_tag_field_in(tx_ptp_tag_field_in),                                    // input wire [15 : 0] tx_ptp_tag_field_in
  .stat_tx_bad_fcs(stat_tx_bad_fcs),                                            // output wire stat_tx_bad_fcs
  .stat_tx_broadcast(stat_tx_broadcast),                                        // output wire stat_tx_broadcast
  .stat_tx_frame_error(stat_tx_frame_error),                                    // output wire stat_tx_frame_error
  .stat_tx_local_fault(stat_tx_local_fault),                                    // output wire stat_tx_local_fault
  .stat_tx_multicast(stat_tx_multicast),                                        // output wire stat_tx_multicast
  .stat_tx_packet_1024_1518_bytes(stat_tx_packet_1024_1518_bytes),              // output wire stat_tx_packet_1024_1518_bytes
  .stat_tx_packet_128_255_bytes(stat_tx_packet_128_255_bytes),                  // output wire stat_tx_packet_128_255_bytes
  .stat_tx_packet_1519_1522_bytes(stat_tx_packet_1519_1522_bytes),              // output wire stat_tx_packet_1519_1522_bytes
  .stat_tx_packet_1523_1548_bytes(stat_tx_packet_1523_1548_bytes),              // output wire stat_tx_packet_1523_1548_bytes
  .stat_tx_packet_1549_2047_bytes(stat_tx_packet_1549_2047_bytes),              // output wire stat_tx_packet_1549_2047_bytes
  .stat_tx_packet_2048_4095_bytes(stat_tx_packet_2048_4095_bytes),              // output wire stat_tx_packet_2048_4095_bytes
  .stat_tx_packet_256_511_bytes(stat_tx_packet_256_511_bytes),                  // output wire stat_tx_packet_256_511_bytes
  .stat_tx_packet_4096_8191_bytes(stat_tx_packet_4096_8191_bytes),              // output wire stat_tx_packet_4096_8191_bytes
  .stat_tx_packet_512_1023_bytes(stat_tx_packet_512_1023_bytes),                // output wire stat_tx_packet_512_1023_bytes
  .stat_tx_packet_64_bytes(stat_tx_packet_64_bytes),                            // output wire stat_tx_packet_64_bytes
  .stat_tx_packet_65_127_bytes(stat_tx_packet_65_127_bytes),                    // output wire stat_tx_packet_65_127_bytes
  .stat_tx_packet_8192_9215_bytes(stat_tx_packet_8192_9215_bytes),              // output wire stat_tx_packet_8192_9215_bytes
  .stat_tx_packet_large(stat_tx_packet_large),                                  // output wire stat_tx_packet_large
  .stat_tx_packet_small(stat_tx_packet_small),                                  // output wire stat_tx_packet_small
  .stat_tx_total_bytes(stat_tx_total_bytes),                                    // output wire [5 : 0] stat_tx_total_bytes
  .stat_tx_total_good_bytes(stat_tx_total_good_bytes),                          // output wire [13 : 0] stat_tx_total_good_bytes
  .stat_tx_total_good_packets(stat_tx_total_good_packets),                      // output wire stat_tx_total_good_packets
  .stat_tx_total_packets(stat_tx_total_packets),                                // output wire stat_tx_total_packets
  .stat_tx_unicast(stat_tx_unicast),                                            // output wire stat_tx_unicast
  .stat_tx_vlan(stat_tx_vlan),                                                  // output wire stat_tx_vlan
  .ctl_tx_enable(ctl_tx_enable),                                                // input wire ctl_tx_enable
  .ctl_tx_send_idle(ctl_tx_send_idle),                                          // input wire ctl_tx_send_idle
  .ctl_tx_send_rfi(ctl_tx_send_rfi),                                            // input wire ctl_tx_send_rfi
  .ctl_tx_send_lfi(ctl_tx_send_lfi),                                            // input wire ctl_tx_send_lfi
  .ctl_tx_test_pattern(ctl_tx_test_pattern),                                    // input wire ctl_tx_test_pattern
  .core_tx_reset(core_tx_reset),                                                // input wire core_tx_reset
  .tx_axis_tready(tx_axis_tready),                                              // output wire tx_axis_tready
  .tx_axis_tvalid(tx_axis_tvalid),                                              // input wire tx_axis_tvalid
  .tx_axis_tdata(tx_axis_tdata),                                                // input wire [511 : 0] tx_axis_tdata
  .tx_axis_tlast(tx_axis_tlast),                                                // input wire tx_axis_tlast
  .tx_axis_tkeep(tx_axis_tkeep),                                                // input wire [63 : 0] tx_axis_tkeep
  .tx_axis_tuser(tx_axis_tuser),                                                // input wire tx_axis_tuser
  .tx_ovfout(tx_ovfout),                                                        // output wire tx_ovfout
  .tx_unfout(tx_unfout),                                                        // output wire tx_unfout
  .tx_preamblein(tx_preamblein),                                                // input wire [55 : 0] tx_preamblein
  .usr_tx_reset(usr_tx_reset),                                                  // output wire usr_tx_reset
  .core_drp_reset(core_drp_reset),                                              // input wire core_drp_reset
  .drp_clk(drp_clk),                                                            // input wire drp_clk
  .drp_addr(drp_addr),                                                          // input wire [9 : 0] drp_addr
  .drp_di(drp_di),                                                              // input wire [15 : 0] drp_di
  .drp_en(drp_en),                                                              // input wire drp_en
  .drp_do(drp_do),                                                              // output wire [15 : 0] drp_do
  .drp_rdy(drp_rdy),                                                            // output wire drp_rdy
  .drp_we(drp_we)                                                              // input wire drp_we
);
// INST_TAG_END ------ End INSTANTIATION Template ---------

// You must compile the wrapper file cmac_usplus_0.v when simulating
// the core, cmac_usplus_0. When compiling the wrapper file, be sure to
// reference the Verilog simulation library.

