///////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 1995/2014 Xilinx, Inc.
//  All Right Reserved.
///////////////////////////////////////////////////////////////////////////////
//   ____  ____
//  /   /\/   /
// /___/  \  /     Vendor      : Xilinx
// \   \   \/      Version     : 2014.2
//  \   \          Description : Xilinx Timing Simulation Library Component
//  /   /                        48-bit Multi-Functional Arithmetic Block
// /___/   /\      Filename    : DSP_PREADD.v
// \   \  /  \
//  \___\/\___\
//
///////////////////////////////////////////////////////////////////////////////
//  Revision:
//  07/15/12 - Migrate from E1.
//  12/10/12 - Add dynamic registers
//  01/11/13 - DIN, D_DATA data width change (26/24) sync4 yml
//  10/22/14 - 808642 - Added #1 to $finish
//  End Revision:
///////////////////////////////////////////////////////////////////////////////

`timescale 1 ps / 1 ps

`celldefine

module DSP_PREADD
`ifdef XIL_TIMING
#(
  parameter LOC = "UNPLACED"
)
`endif
(
  output [26:0] AD,

  input ADDSUB,
  input [26:0] D_DATA,
  input INMODE2,
  input [26:0] PREADD_AB
);
  
// define constants
  localparam MODULE_NAME = "DSP_PREADD";

  tri0 glblGSR = glbl.GSR;

  wire [26:0] AD_out;

  wire ADDSUB_in;
  wire INMODE2_in;
  wire [26:0] D_DATA_in;
  wire [26:0] PREADD_AB_in;

`ifdef XIL_TIMING
  wire ADDSUB_delay;
  wire INMODE2_delay;
  wire [26:0] D_DATA_delay;
  wire [26:0] PREADD_AB_delay;
`endif
  
  wire [26:0] D_DATA_mux;

  assign AD = AD_out;

// inputs with no timing checks
`ifdef XIL_TIMING
  assign ADDSUB_delay = ADDSUB;
  assign D_DATA_delay = D_DATA;
  assign INMODE2_delay = INMODE2;
  assign PREADD_AB_delay = PREADD_AB;
`endif


`ifdef XIL_TIMING
  assign ADDSUB_in = ADDSUB_delay;
  assign D_DATA_in = D_DATA_delay;
  assign INMODE2_in = INMODE2_delay;
  assign PREADD_AB_in = PREADD_AB_delay;
`else
  assign ADDSUB_in = ADDSUB;
  assign D_DATA_in = D_DATA;
  assign INMODE2_in = INMODE2;
  assign PREADD_AB_in = PREADD_AB;
`endif

`ifndef XIL_TIMING
  initial begin
    $display("Error: [Unisim %s-140] SIMPRIM primitive is not intended for direct instantiation in RTL or functional netlists. This primitive is only available in the SIMPRIM library for implemented netlists, please ensure you are pointing to the correct library. Instance %m", MODULE_NAME);
    #1 $finish;
  end
`endif

//*********************************************************
//*** Preaddsub AD
//*********************************************************
  assign D_DATA_mux = INMODE2_in ? D_DATA_in : 27'b0;
  assign AD_out = ADDSUB_in ? (D_DATA_mux - PREADD_AB_in) : (D_DATA_mux + PREADD_AB_in);


  specify
    (ADDSUB *> AD) = (0:0:0, 0:0:0);
    (D_DATA *> AD) = (0:0:0, 0:0:0);
    (INMODE2 *> AD) = (0:0:0, 0:0:0);
    (PREADD_AB *> AD) = (0:0:0, 0:0:0);
    specparam PATHPULSE$ = 0;
  endspecify

endmodule

`endcelldefine
