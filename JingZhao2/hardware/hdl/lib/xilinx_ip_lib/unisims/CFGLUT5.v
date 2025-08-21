///////////////////////////////////////////////////////////////////////////////
// Copyright (c) 1995/2016 Xilinx, Inc.
// All Right Reserved.
///////////////////////////////////////////////////////////////////////////////
//   ____  ____
//  /   /\/   /
// /___/  \  /    Vendor      : Xilinx
// \   \   \/     Version     : 2016.4
//  \   \         Description : Xilinx Unified Simulation Library Component
//  /   /                  5-input Dynamically Reconfigurable Look-Up-Table with Carry and Clock Enable
// /___/   /\     Filename    : CFGLUT5.v
// \   \  /  \
//  \___\/\___\
//
///////////////////////////////////////////////////////////////////////////////
// Revision
//    12/27/05 - Initial version.
//    12/13/11 - 524859 - Added `celldefine and `endcelldefine
//    05/13/13 - add IS_CLK_INVERTED
// End Revision
///////////////////////////////////////////////////////////////////////////////

`timescale 1 ps/1 ps

`celldefine

module CFGLUT5 #(
  `ifdef XIL_TIMING
  parameter LOC = "UNPLACED",
  `endif
  parameter [31:0] INIT = 32'h00000000,
  parameter [0:0] IS_CLK_INVERTED = 1'b0
)(
  output reg CDO,
  output reg O5,
  output reg O6,
  
  input CDI,
  input CE,
  input CLK,
  input I0,
  input I1,
  input I2,
  input I3,
  input I4
);

`ifdef XIL_TIMING
  wire CDI_dly;
  wire CE_dly;
  wire CLK_dly;
`endif

  reg  [31:0] data = INIT;
  reg first_time = 1'b1;

  initial
  begin
    assign  data = INIT;
    assign  CDO = INIT[31];
    first_time <= #100000 1'b0;
`ifdef XIL_TIMING
    while ((((CLK_dly !== 1'b0) && (IS_CLK_INVERTED == 1'b0)) ||
            ((CLK_dly !== 1'b1) && (IS_CLK_INVERTED == 1'b1))) &&
           (first_time == 1'b1)) #1000;
`else
    while ((((CLK !== 1'b0) && (IS_CLK_INVERTED == 1'b0)) ||
            ((CLK !== 1'b1) && (IS_CLK_INVERTED == 1'b1))) &&
           (first_time == 1'b1)) #1000;
`endif
    deassign data;
    deassign  CDO;
  end

`ifdef XIL_TIMING
  always @(negedge (CLK_dly && IS_CLK_INVERTED)) begin
    if (CE_dly == 1'b1) begin
      data[31:0] <= #100 {data[30:0], CDI_dly};
    end
  end
`else
  always @(negedge (CLK && IS_CLK_INVERTED)) begin
    if (CE == 1'b1) begin
      data[31:0] <= #100 {data[30:0], CDI};
    end
  end
`endif
`ifdef XIL_TIMING
  always @(posedge (CLK_dly && ~IS_CLK_INVERTED)) begin
    if (CE_dly == 1'b1) begin
      data[31:0] <= #100 {data[30:0], CDI_dly};
    end
  end
`else
  always @(posedge (CLK && ~IS_CLK_INVERTED)) begin
    if (CE == 1'b1) begin
      data[31:0] <= #100 {data[30:0], CDI};
    end
  end
`endif

  always @ (data or I3 or I2 or I1 or I0) begin
    if (O5 !== data[{I3,I2,I1,I0}]) O5 = data[{I3,I2,I1,I0}]; //    O5 = data[A_in[3:0]];
  end
  always @ (data or I4 or I3 or I2 or I1 or I0) begin
    if (O6 !== data[{I4,I3,I2,I1,I0}]) O6 = data[{I4,I3,I2,I1,I0}]; //    O6 = data[A_in[4:0]];
  end
  always @ (data[31]) begin
    if (CDO !== data[31]) CDO = data[31]; //    CDO = data[31];
  end

`ifdef XIL_TIMING

  reg notifier;

  wire sh_clk_en_p;
  wire sh_clk_en_n;
  wire sh_ce_clk_en_p;
  wire sh_ce_clk_en_n;

  always @(notifier) 
    data[0] = 1'bx;

  assign sh_clk_en_p = ~IS_CLK_INVERTED;
  assign sh_clk_en_n = IS_CLK_INVERTED;
  assign sh_ce_clk_en_p = CE && ~IS_CLK_INVERTED;
  assign sh_ce_clk_en_n = CE && IS_CLK_INVERTED;

  specify
    (CLK => CDO) = (0:0:0, 0:0:0);
    (CLK => O5) = (0:0:0, 0:0:0);
    (CLK => O6) = (0:0:0, 0:0:0);
    (I0 => CDO) = (0:0:0, 0:0:0);
    (I0 => O5) = (0:0:0, 0:0:0);
    (I0 => O6) = (0:0:0, 0:0:0);
    (I1 => CDO) = (0:0:0, 0:0:0);
    (I1 => O5) = (0:0:0, 0:0:0);
    (I1 => O6) = (0:0:0, 0:0:0);
    (I2 => CDO) = (0:0:0, 0:0:0);
    (I2 => O5) = (0:0:0, 0:0:0);
    (I2 => O6) = (0:0:0, 0:0:0);
    (I3 => CDO) = (0:0:0, 0:0:0);
    (I3 => O5) = (0:0:0, 0:0:0);
    (I3 => O6) = (0:0:0, 0:0:0);
    (I4 => CDO) = (0:0:0, 0:0:0);
    (I4 => O5) = (0:0:0, 0:0:0);
    (I4 => O6) = (0:0:0, 0:0:0);
    $period (negedge CLK, 0:0:0, notifier);
    $period (posedge CLK, 0:0:0, notifier);
    $setuphold (negedge CLK, negedge CDI, 0:0:0, 0:0:0, notifier,sh_ce_clk_en_n,sh_ce_clk_en_n,CLK_dly,CDI_dly);
    $setuphold (negedge CLK, negedge CE, 0:0:0, 0:0:0, notifier,sh_clk_en_n,sh_clk_en_n,CLK_dly,CE_dly);
    $setuphold (negedge CLK, posedge CDI, 0:0:0, 0:0:0, notifier,sh_ce_clk_en_n,sh_ce_clk_en_n,CLK_dly,CDI_dly);
    $setuphold (negedge CLK, posedge CE, 0:0:0, 0:0:0, notifier,sh_clk_en_n,sh_clk_en_n,CLK_dly,CE_dly);
    $setuphold (posedge CLK, negedge CDI, 0:0:0, 0:0:0, notifier,sh_ce_clk_en_p,sh_ce_clk_en_p,CLK_dly,CDI_dly);
    $setuphold (posedge CLK, negedge CE, 0:0:0, 0:0:0, notifier,sh_clk_en_p,sh_clk_en_p,CLK_dly,CE_dly);
    $setuphold (posedge CLK, posedge CDI, 0:0:0, 0:0:0, notifier,sh_ce_clk_en_p,sh_ce_clk_en_p,CLK_dly,CDI_dly);
    $setuphold (posedge CLK, posedge CE, 0:0:0, 0:0:0, notifier,sh_clk_en_p,sh_clk_en_p,CLK_dly,CE_dly);
    $width (negedge CLK, 0:0:0, 0, notifier);
    $width (posedge CLK, 0:0:0, 0, notifier);
    specparam PATHPULSE$ = 0;
  endspecify
`endif

endmodule

`endcelldefine
