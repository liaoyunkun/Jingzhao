///////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 1995/2014 Xilinx, Inc.
//  All Right Reserved.
///////////////////////////////////////////////////////////////////////////////
//   ____  ____
//  /   /\/   /
// /___/  \  /     Vendor      : Xilinx
// \   \   \/      Version     : 2014.2
//  \   \          Description : Xilinx Unified Simulation Library Component
//  /   /                        _no_description_
// /___/   /\      Filename    : OBUFDS_GTE4.v
// \   \  /  \
//  \___\/\___\
//
///////////////////////////////////////////////////////////////////////////////
//  Revision:
//
//  End Revision:
///////////////////////////////////////////////////////////////////////////////

`timescale 1 ps / 1 ps

`celldefine

module OBUFDS_GTE4 #(
`ifdef XIL_TIMING
  parameter LOC = "UNPLACED",
`endif
  parameter [0:0] REFCLK_EN_TX_PATH = 1'b0,
  parameter [4:0] REFCLK_ICNTL_TX = 5'b00000
)(
  output O,
  output OB,

  input CEB,
  input I
);
  
// define constants
  localparam MODULE_NAME = "OBUFDS_GTE4";
  localparam in_delay    = 0;
  localparam out_delay   = 0;
  localparam inclk_delay    = 0;
  localparam outclk_delay   = 0;

// include dynamic registers - XILINX test only
`ifdef XIL_DR
  `include "OBUFDS_GTE4_dr.v"
`else
  localparam [0:0] REFCLK_EN_TX_PATH_REG = REFCLK_EN_TX_PATH;
  localparam [4:0] REFCLK_ICNTL_TX_REG = REFCLK_ICNTL_TX;
`endif

  wire REFCLK_EN_TX_PATH_BIN;
  wire [4:0] REFCLK_ICNTL_TX_BIN;

  tri0 GTS = glbl.GTS;
  tri0 glblGSR = glbl.GSR;

  wire OB_out;
  wire O_out;

  wire OB_delay;
  wire O_delay;

  wire CEB_in;
  wire I_in;

  wire CEB_delay;
  wire I_delay;

  assign #(out_delay) O = O_delay;
  assign #(out_delay) OB = OB_delay;


// inputs with no timing checks
  assign #(in_delay) CEB_delay = CEB;
  assign #(in_delay) I_delay = I;

  assign OB_delay = OB_out;
  assign O_delay = O_out;

  assign CEB_in = (CEB !== 1'bz) && CEB_delay; // rv 0
  assign I_in = I_delay;

  assign REFCLK_EN_TX_PATH_BIN = REFCLK_EN_TX_PATH_REG;

  assign REFCLK_ICNTL_TX_BIN = REFCLK_ICNTL_TX_REG;

    wire t1;
    wire t2;
   
    or O1 (t1, GTS, CEB);
    or O2 (t2, ~REFCLK_EN_TX_PATH_BIN, t1);
    bufif0 B1 (O, I, t2);
    notif0 N1 (OB, I, t2);
   
  specify
    (I => O) = (0:0:0, 0:0:0);
    (I => OB) = (0:0:0, 0:0:0);
    specparam PATHPULSE$ = 0;
  endspecify

endmodule

`endcelldefine
