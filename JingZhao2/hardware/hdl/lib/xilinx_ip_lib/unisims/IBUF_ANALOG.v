///////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 1995/2016 Xilinx, Inc.
//  All Right Reserved.
///////////////////////////////////////////////////////////////////////////////
//   ____  ____
//  /   /\/   /
// /___/  \  /     Vendor      : Xilinx
// \   \   \/      Version     : 2016.2
//  \   \          Description : Xilinx Unified Simulation Library Component
//  /   /                        Analog Auxiliary SYSMON Input Buffer
// /___/   /\      Filename    : IBUF_ANALOG.v
// \   \  /  \
//  \___\/\___\
//
///////////////////////////////////////////////////////////////////////////////
//  Revision:
//
//    10/30/13 - Initial version.
//    02/04/15 - Remove pulldown and strength specification. Fix for CR 845545.
//  End Revision:
///////////////////////////////////////////////////////////////////////////////

`timescale 1 ps / 1 ps

`celldefine

module IBUF_ANALOG
`ifdef XIL_TIMING
#(
  parameter LOC = "UNPLACED"
)
`endif
(
  output O,

  input I
);

// define constants
  localparam MODULE_NAME = "IBUF_ANALOG";

  tri0 glblGSR = glbl.GSR;

  assign O = I;

specify
  (I => O) = (0:0:0, 0:0:0);
  specparam PATHPULSE$ = 0;
endspecify

endmodule

`endcelldefine
