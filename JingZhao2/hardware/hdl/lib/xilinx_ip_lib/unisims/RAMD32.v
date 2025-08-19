///////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 1995/2014 Xilinx, Inc.
//  All Right Reserved.
///////////////////////////////////////////////////////////////////////////////
//   ____  ____
//  /   /\/   /
// /___/  \  /     Vendor      : Xilinx
// \   \   \/      Version     : 2014.3
//  \   \          Description : Xilinx Timing Simulation Library Component
//  /   /                   Static Dual Port Synchronous RAM 32-Deep by 1-Wide
// /___/   /\      Filename    : RAMD32.v
// \   \  /  \
//  \___\/\___\
//
///////////////////////////////////////////////////////////////////////////////
// Revision:
//    07/02/10 - Initial version.
//    12/13/11 - Added `celldefine and `endcelldefine (CR 524859).
//    04/16/13 - PR683925 - add invertible pin support.
//    10/22/14 - Added #1 to $finish (CR 808642).
// End Revision:
///////////////////////////////////////////////////////////////////////////////

`timescale 1 ps / 1 ps

`celldefine

module RAMD32 #(
`ifdef XIL_TIMING
  parameter LOC = "UNPLACED",
`endif
  parameter [31:0] INIT = 32'h00000000,
  parameter [0:0] IS_CLK_INVERTED = 1'b0
)(
  output O,

  input CLK,
  input I,
  input RADR0,
  input RADR1,
  input RADR2,
  input RADR3,
  input RADR4,
  input WADR0,
  input WADR1,
  input WADR2,
  input WADR3,
  input WADR4,
  input WE
);
  
// define constants
  localparam MODULE_NAME = "RAMD32";

  reg trig_attr = 1'b0;

`ifdef XIL_ATTR_TEST
  reg attr_test = 1'b1;
`else
  reg attr_test = 1'b0;
`endif
  reg attr_err = 1'b0;

  wire IS_CLK_INVERTED_BIN;

  wire [4:0] RADR_in;
  wire CLK_in;
  wire I_in;
  wire [4:0] WADR_in;
  wire WE_in;

  assign  IS_CLK_INVERTED_BIN = IS_CLK_INVERTED;

`ifdef XIL_TIMING
  wire CLK_dly;
  wire I_dly;
  wire [4:0] WADR_dly;
  wire WE_dly;

  reg notifier;

  wire sh_clk_en_p;
  wire sh_clk_en_n;
  wire sh_we_clk_en_p;
  wire sh_we_clk_en_n;

  assign CLK_in = CLK_dly ^ IS_CLK_INVERTED_BIN;
  assign I_in = I_dly;
  assign WADR_in = WADR_dly;
  assign WE_in = (WE === 1'bz) || WE_dly; // rv 1
`else
  assign CLK_in = CLK ^ IS_CLK_INVERTED_BIN;
  assign I_in = I;
  assign WADR_in = {WADR4, WADR3, WADR2, WADR1, WADR0};
  assign WE_in = (WE === 1'bz) || WE; // rv 1
`endif
  assign RADR_in = {RADR4, RADR3, RADR2, RADR1, RADR0};
  reg [31:0] mem;

  initial begin
    mem = INIT;
`ifndef XIL_TIMING
    $display("Error: [Unisim %s-105] SIMPRIM primitive is not intended for direct instantiation in RTL or functional netlists. This primitive is only available in the SIMPRIM library for implemented netlists, please ensure you are pointing to the correct library. Instance %m", MODULE_NAME);
    #100;
    #1 $finish;
`endif
  end

  always @(posedge CLK_in)
    if (WE_in == 1'b1)
      mem[WADR_in] <= #100 I_in;

  assign O = mem[RADR_in];

`ifdef XIL_TIMING
  always @(notifier) mem[WADR_in] <= 1'bx;
  assign sh_clk_en_p = ~IS_CLK_INVERTED_BIN;
  assign sh_clk_en_n = IS_CLK_INVERTED_BIN;
  assign sh_we_clk_en_p = WE_in && ~IS_CLK_INVERTED_BIN;
  assign sh_we_clk_en_n = WE_in && IS_CLK_INVERTED_BIN;

  specify
    (CLK => O) = (0:0:0, 0:0:0);
    (RADR0 => O) = (0:0:0, 0:0:0);
    (RADR1 => O) = (0:0:0, 0:0:0);
    (RADR2 => O) = (0:0:0, 0:0:0);
    (RADR3 => O) = (0:0:0, 0:0:0);
    (RADR4 => O) = (0:0:0, 0:0:0);
    $period (negedge CLK &&& WE, 0:0:0, notifier);
    $period (posedge CLK &&& WE, 0:0:0, notifier);
    $setuphold (negedge CLK, negedge I, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, I_dly);
    $setuphold (negedge CLK, negedge WADR0, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[0]);
    $setuphold (negedge CLK, negedge WADR1, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[1]);
    $setuphold (negedge CLK, negedge WADR2, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[2]);
    $setuphold (negedge CLK, negedge WADR3, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[3]);
    $setuphold (negedge CLK, negedge WADR4, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[4]);
    $setuphold (negedge CLK, negedge WE, 0:0:0, 0:0:0, notifier,sh_clk_en_n,sh_clk_en_n, CLK_dly, WE_dly);
    $setuphold (negedge CLK, posedge I, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, I_dly);
    $setuphold (negedge CLK, posedge WADR0, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[0]);
    $setuphold (negedge CLK, posedge WADR1, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[1]);
    $setuphold (negedge CLK, posedge WADR2, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[2]);
    $setuphold (negedge CLK, posedge WADR3, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[3]);
    $setuphold (negedge CLK, posedge WADR4, 0:0:0, 0:0:0, notifier,sh_we_clk_en_n,sh_we_clk_en_n, CLK_dly, WADR_dly[4]);
    $setuphold (negedge CLK, posedge WE, 0:0:0, 0:0:0, notifier,sh_clk_en_n,sh_clk_en_n, CLK_dly, WE_dly);
    $setuphold (posedge CLK, negedge I, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, I_dly);
    $setuphold (posedge CLK, negedge WADR0, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[0]);
    $setuphold (posedge CLK, negedge WADR1, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[1]);
    $setuphold (posedge CLK, negedge WADR2, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[2]);
    $setuphold (posedge CLK, negedge WADR3, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[3]);
    $setuphold (posedge CLK, negedge WADR4, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[4]);
    $setuphold (posedge CLK, negedge WE, 0:0:0, 0:0:0, notifier,sh_clk_en_p,sh_clk_en_p, CLK_dly, WE_dly);
    $setuphold (posedge CLK, posedge I, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, I_dly);
    $setuphold (posedge CLK, posedge WADR0, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[0]);
    $setuphold (posedge CLK, posedge WADR1, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[1]);
    $setuphold (posedge CLK, posedge WADR2, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[2]);
    $setuphold (posedge CLK, posedge WADR3, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[3]);
    $setuphold (posedge CLK, posedge WADR4, 0:0:0, 0:0:0, notifier,sh_we_clk_en_p,sh_we_clk_en_p, CLK_dly, WADR_dly[4]);
    $setuphold (posedge CLK, posedge WE, 0:0:0, 0:0:0, notifier,sh_clk_en_p,sh_clk_en_p, CLK_dly, WE_dly);
    $width (negedge CLK, 0:0:0, 0, notifier);
    $width (posedge CLK, 0:0:0, 0, notifier);
    specparam PATHPULSE$ = 0;
  endspecify
`endif

endmodule

`endcelldefine
