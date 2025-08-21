/*------------------------------------------- Module Description : Begin ----------------------------------------------
Name:       ICMLookupArbiter
Author:     YangFan
Function:   Schedule MR requests from different channels.

            We assume there is no access collision, just for performan optimization.
--------------------------------------------- Module Decription : End -----------------------------------------------*/

/*------------------------------------------- Timescale Definition : Begin ------------------------------------------*/
`timescale 1ns / 1ps
/*------------------------------------------- Timescale Definition : End --------------------------------------------*/

/*------------------------------------------- Included Files : Begin ------------------------------------------------*/
`include "protocol_engine_def.vh"
/*------------------------------------------- Included Files : End --------------------------------------------------*/

/*------------------------------------------- Input/Output Definition : Begin ---------------------------------------*/
module ICMLookupArbiter
#(
    parameter           ICM_ENTRY_NUM       =       `ICM_ENTRY_NUM_MPT,
    parameter           ICM_ENTRY_NUM_LOG   =       log2b(ICM_ENTRY_NUM - 1)
)
(
	input 	wire 										    clk,
	input 	wire 										    rst,

    input   wire                                            chnl_0_lookup_valid,
    input   wire    [ICM_ENTRY_NUM_LOG - 1 : 0]             chnl_0_lookup_head,
    output  wire                                            chnl_0_lookup_ready,

    output  wire                                            chnl_0_rsp_valid,
    output  wire    [`ICM_SPACE_ADDR_WIDTH - 1 : 0]         chnl_0_rsp_icm_addr,
    output  wire    [`PHY_SPACE_ADDR_WIDTH - 1 : 0]         chnl_0_rsp_phy_addr,
    input   wire                                            chnl_0_rsp_ready,

    input   wire                                            chnl_1_lookup_valid,
    input   wire    [ICM_ENTRY_NUM_LOG - 1 : 0]             chnl_1_lookup_head,
    output  wire                                            chnl_1_lookup_ready,

    output  wire                                            chnl_1_rsp_valid,
    output  wire    [`ICM_SPACE_ADDR_WIDTH - 1 : 0]         chnl_1_rsp_icm_addr,
    output  wire    [`PHY_SPACE_ADDR_WIDTH - 1 : 0]         chnl_1_rsp_phy_addr,
    input   wire                                            chnl_1_rsp_ready,

    output  wire                                            lookup_valid,
    output  wire    [ICM_ENTRY_NUM_LOG - 1 : 0]             lookup_head,

    input   wire                                            rsp_valid,
    input   wire    [`ICM_SPACE_ADDR_WIDTH - 1 : 0]         rsp_icm_addr,
    input   wire    [`PHY_SPACE_ADDR_WIDTH - 1 : 0]         rsp_phy_addr
);

assign chnl_0_lookup_ready = 'd1;
assign chnl_1_lookup_ready = 'd1;

assign lookup_valid =   chnl_0_lookup_valid ? chnl_0_lookup_valid :
                        chnl_1_lookup_valid ? chnl_1_lookup_valid : 'd0;
assign lookup_head =   chnl_0_lookup_valid ? chnl_0_lookup_head :
                        chnl_1_lookup_valid ? chnl_1_lookup_head : 'd0;

assign chnl_0_rsp_valid = rsp_valid;
assign chnl_0_rsp_icm_addr = rsp_icm_addr;
assign chnl_0_rsp_phy_addr = rsp_phy_addr;

assign chnl_1_rsp_valid = rsp_valid;
assign chnl_1_rsp_icm_addr = rsp_icm_addr;
assign chnl_1_rsp_phy_addr = rsp_phy_addr;

endmodule