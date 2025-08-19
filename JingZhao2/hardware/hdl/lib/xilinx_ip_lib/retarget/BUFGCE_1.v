// $Header: $
///////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2009 Xilinx, Inc.
// All Right Reserved.
///////////////////////////////////////////////////////////////////////////////
//   ____  ____
//  /   /\/   /
// /___/  \  /    Vendor : Xilinx
// \   \   \/     Version : 12.1
//  \   \         Description : Xilinx Retarget Simulation Library Component
//  /   /             Global Clock Mux Buffer with Clock Enable and Output State 1 (Discontinue in 13.1)
// /___/   /\     Filename : BUFGCE_1.v
// \   \  /  \    Timestamp : Thu Jun 24 3:00:00 PST 2010

//  \___\/\___\
//
// Revision:
//    06/24/10 - Initial version
`timescale  1 ps / 1 ps

module BUFGCE_1 (O, CE, I);

    output O;

    input  CE, I;

    wire   NCE;
    

BUFGCTRL #(.INIT_OUT(1'b1), .PRESELECT_I0("TRUE"), .PRESELECT_I1("FALSE")) B0 (.O(O), .CE0(CE), .CE1(1'b0), .I0(I), .I1(1'b1), .IGNORE0(1'b0), .IGNORE1(1'b1), .S0(1'b1), .S1(1'b0));

endmodule
