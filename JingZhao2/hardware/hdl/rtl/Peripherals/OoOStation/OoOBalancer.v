/*------------------------------------------- Module Description : Begin ----------------------------------------------
Name:       OoOStation
Author:     YangFan
Function:   OoOStation is short for Out-of-Order station.
            This module is a special abstraction aimed at solving Head-Of-Line Problem in Stateful Pipeline.
            OoOStation accepts resources request and manages out-of-order request and resposne.
            User can carry user-defined data with request, when desired resource is available, OoOStation will return resources with user-defined data.
            For example, as for ReqTransEngine, the request is accompanied with WQE metadata, as for RespRecvControl, the request is accompanied with packet.
            OoOStation does not care about what the req and resource are, it just handles out-of-order execution.
            Typically, OoOStation interacts with two three threads: ingress thread, egress thread and resource mgt thread.
            Ingress thread: Asynchronously issues resource request to OoOStation.
            Egress thread: Accept resources from OoOStation and triggers next pipeline stage.
            Resource mgt thread: Manage resources and respond to resource request.
--------------------------------------------- Module Decription : End -----------------------------------------------*/

/*------------------------------------------- Timescale Definition : Begin ------------------------------------------*/
`timescale 1ns / 1ps
/*------------------------------------------- Timescale Definition : End --------------------------------------------*/

/*------------------------------------------- Included Files : Begin ------------------------------------------------*/
`include "common_function_def.vh"
/*------------------------------------------- Included Files : End --------------------------------------------------*/

/*------------------------------------------- Local Macros Definition : Begin ---------------------------------------*/
/*------------------------------------------- Local Macros Definition : End -----------------------------------------*/

/*------------------------------------------- Input/Output Definition : Begin ---------------------------------------*/
module OoOBalancer #(
    parameter       OOO_STATION_NUM             =   1,          //1, 2, 4
    parameter       OOO_STATION_NUM_LOG         =   log2b(OOO_STATION_NUM - 1),

    //TAG_NUM is not equal to SLOT_NUM, since each resource req consumes 1 tag, and it may require more than 1 slot.
    parameter       TAG_NUM                     =   32,
    parameter       TAG_NUM_LOG                 =   log2b(TAG_NUM - 1),


    //RESOURCE_CMD/RESP_WIDTH is resource-specific
    //For example, MR resource cmd format is {PD, LKey, Lengtg, Addr}, MR resource reply format is {PTE-1, PTE-0, indicator}
    parameter       RESOURCE_CMD_HEAD_WIDTH     =   128,
    parameter       RESOURCE_CMD_DATA_WIDTH     =   256,
    parameter       RESOURCE_RESP_HEAD_WIDTH    =   128, 
    parameter       RESOURCE_RESP_DATA_WIDTH    =   128,

    parameter       SLOT_NUM                    =   512,
    parameter       QUEUE_NUM                   =   32,
    parameter       SLOT_NUM_LOG                =   log2b(SLOT_NUM - 1),
    parameter       QUEUE_NUM_LOG               =   log2b(QUEUE_NUM - 1),

    //When issuing cmd to Resource Manager, add tag index
    parameter       OOO_CMD_HEAD_WIDTH          =   `MAX_REQ_TAG_NUM_LOG + RESOURCE_CMD_HEAD_WIDTH,
    parameter       OOO_CMD_DATA_WIDTH          =   RESOURCE_CMD_DATA_WIDTH,
    parameter       OOO_RESP_HEAD_WIDTH         =   `MAX_REQ_TAG_NUM_LOG + RESOURCE_RESP_HEAD_WIDTH,
    parameter       OOO_RESP_DATA_WIDTH         =   RESOURCE_RESP_DATA_WIDTH,

    parameter       INGRESS_HEAD_WIDTH          =   RESOURCE_CMD_HEAD_WIDTH + `INGRESS_COMMON_HEAD_WIDTH,
    //INGRESS_DATA_WIDTH is ingress-thread-specific
    parameter       INGRESS_DATA_WIDTH          =   512,

    parameter       SLOT_WIDTH                  =   INGRESS_DATA_WIDTH,


    //Egress thread
    parameter       EGRESS_HEAD_WIDTH           =   RESOURCE_RESP_HEAD_WIDTH + `EGRESS_COMMON_HEAD_WIDTH,
    parameter       EGRESS_DATA_WIDTH           =   INGRESS_DATA_WIDTH
)
(
    input   wire                                            clk,
    input   wire                                            rst,

    input   wire                                            ingress_valid,
    input   wire        [INGRESS_HEAD_WIDTH - 1 : 0]        ingress_head,
    input   wire        [SLOT_WIDTH - 1 : 0]                ingress_data,
    input   wire                                            ingress_start,
    input   wire                                            ingress_last,
    output  wire                                            ingress_ready,

    output  wire        [SLOT_NUM_LOG : 0]                  available_slot_num,

    output  wire                                            resource_req_valid,
    output  wire        [OOO_CMD_HEAD_WIDTH - 1 : 0]        resource_req_head,
    output  wire        [OOO_CMD_DATA_WIDTH - 1 : 0]        resource_req_data,
    output  wire                                            resource_req_start,
    output  wire                                            resource_req_last,
    input   wire                                            resource_req_ready,

    input   wire                                            resource_resp_valid,
    input   wire        [OOO_RESP_HEAD_WIDTH - 1 : 0]       resource_resp_head,
    input   wire        [OOO_RESP_DATA_WIDTH - 1 : 0]       resource_resp_data,
    input   wire                                            resource_resp_start,
    input   wire                                            resource_resp_last,
    output  wire                                            resource_resp_ready,

    output  wire                                            egress_valid,
    output  wire        [EGRESS_HEAD_WIDTH - 1 : 0]         egress_head,
    output  wire        [EGRESS_DATA_WIDTH - 1 : 0]         egress_data,
    output  wire                                            egress_start,
    output  wire                                            egress_last,
    input   wire                                            egress_ready
);
/*------------------------------------------- Input/Output Definition : End -----------------------------------------*/

/*------------------------------------------- Local Variables Definition : Begin ------------------------------------*/
wire        [OOO_STATION_NUM - 1 : 0]                             st_ingress_valid;
wire        [INGRESS_HEAD_WIDTH * OOO_STATION_NUM - 1 : 0]        st_ingress_head;
wire        [SLOT_WIDTH * OOO_STATION_NUM - 1 : 0]                st_ingress_data;
wire        [OOO_STATION_NUM - 1 : 0]                             st_ingress_start;
wire        [OOO_STATION_NUM - 1 : 0]                             st_ingress_last;
wire        [OOO_STATION_NUM - 1 : 0]                             st_ingress_ready;


wire        [OOO_STATION_NUM - 1 : 0]                             balancer_ingress_valid;
wire        [INGRESS_HEAD_WIDTH * OOO_STATION_NUM - 1 : 0]        balancer_ingress_head;
wire        [SLOT_WIDTH * OOO_STATION_NUM - 1 : 0]                balancer_ingress_data;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_ingress_start;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_ingress_last;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_ingress_ready;

wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_req_valid;
wire        [OOO_CMD_HEAD_WIDTH * OOO_STATION_NUM - 1 : 0]        balancer_resource_req_head;
wire        [OOO_CMD_DATA_WIDTH * OOO_STATION_NUM - 1 : 0]        balancer_resource_req_data;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_req_start;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_req_last;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_req_ready;

wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_resp_valid;
wire        [OOO_RESP_HEAD_WIDTH * OOO_STATION_NUM - 1 : 0]       balancer_resource_resp_head;
wire        [OOO_RESP_DATA_WIDTH * OOO_STATION_NUM - 1 : 0]       balancer_resource_resp_data;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_resp_start;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_resp_last;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_resource_resp_ready;

wire        [OOO_STATION_NUM - 1 : 0]                             balancer_egress_valid;
wire        [EGRESS_HEAD_WIDTH * OOO_STATION_NUM - 1 : 0]         balancer_egress_head;
wire        [EGRESS_DATA_WIDTH * OOO_STATION_NUM - 1 : 0]         balancer_egress_data;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_egress_start;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_egress_last;
wire        [OOO_STATION_NUM - 1 : 0]                             balancer_egress_ready;


/*------------------------------------------- Local Variables Definition : End --------------------------------------*/

/*------------------------------------------- Submodules Instatiation : Begin ---------------------------------------*/
generate
    if (OOO_STATION_NUM == 1) begin
        assign ingress_ready = balancer_ingress_ready[0];

        assign resource_resp_ready = balancer_resource_resp_ready[0];
    end
    else if(OOO_STATION_NUM == 2) begin
        assign ingress_ready =  ingress_head[0] == 0 ? balancer_ingress_ready[0] :
                                ingress_head[0] == 1 ? balancer_ingress_ready[1] : 'd0;

        assign resource_resp_ready =  (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == 0) ? balancer_resource_resp_ready[0] :
                                      (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == 1) ? balancer_resource_resp_ready[1] : 'd0;
    end
    else if(OOO_STATION_NUM == 4) begin
        assign ingress_ready =  ingress_head[1:0] == 0 ? balancer_ingress_ready[0] :
                                ingress_head[1:0] == 1 ? balancer_ingress_ready[1] :
                                ingress_head[1:0] == 2 ? balancer_ingress_ready[2] :
                                ingress_head[1:0] == 3 ? balancer_ingress_ready[3] : 'd0;

        assign resource_resp_ready =  (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == 0) ? balancer_resource_resp_ready[0] :
                                      (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == 1) ? balancer_resource_resp_ready[1] : 
                                      (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == 2) ? balancer_resource_resp_ready[2] :
                                      (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == 3) ? balancer_resource_resp_ready[3] :'d0;
    end
endgenerate

generate
    if(OOO_STATION_NUM == 1) begin
        assign resource_req_valid = balancer_resource_req_valid[0];
        assign resource_req_head = balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (0 + 1) - 1 : OOO_CMD_HEAD_WIDTH * 0];
        assign resource_req_data = balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (0 + 1) - 1 : OOO_CMD_DATA_WIDTH * 0];
        assign resource_req_start = balancer_resource_req_start[0];
        assign resource_req_last = balancer_resource_req_last[0];

        assign egress_valid = balancer_egress_valid[0];
        assign egress_head = balancer_egress_head[EGRESS_HEAD_WIDTH * (0 + 1) - 1 : EGRESS_HEAD_WIDTH * 0];
        assign egress_data = balancer_egress_data[EGRESS_DATA_WIDTH * (0 + 1) - 1 : EGRESS_DATA_WIDTH * 0];
        assign egress_start = balancer_egress_start[0];
        assign egress_last = balancer_egress_last[0];

        assign balancer_resource_req_ready = resource_req_ready;

        assign balancer_egress_ready = egress_ready;     
    end
    else if(OOO_STATION_NUM == 2) begin
        assign resource_req_valid = balancer_resource_req_valid[0] ? balancer_resource_req_valid[0] : 
                                    balancer_resource_req_valid[1] ? balancer_resource_req_valid[1] : 'd0;
        assign resource_req_head =  balancer_resource_req_valid[0] ? balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (0 + 1) - 1 : OOO_CMD_HEAD_WIDTH * 0] :
                                    balancer_resource_req_valid[1] ? balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (1 + 1) - 1 : OOO_CMD_HEAD_WIDTH * 1] : 'd0;
        assign resource_req_data =  balancer_resource_req_valid[0] ? balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (0 + 1) - 1 : OOO_CMD_DATA_WIDTH * 0] :
                                    balancer_resource_req_valid[1] ? balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (1 + 1) - 1 : OOO_CMD_DATA_WIDTH * 1] : 'd0;
        assign resource_req_start = balancer_resource_req_start[0] ? balancer_resource_req_start[0] : 
                                    balancer_resource_req_start[1] ? balancer_resource_req_start[1] : 'd0;
        assign resource_req_last =  balancer_resource_req_last[0] ? balancer_resource_req_last[0] : 
                                    balancer_resource_req_last[1] ? balancer_resource_req_last[1] : 'd0;


        assign egress_valid = balancer_egress_valid[0] ? balancer_egress_valid[0] : 
                                    balancer_egress_valid[1] ? balancer_egress_valid[1] : 'd0;
        assign egress_head =  balancer_egress_valid[0] ? balancer_egress_head[EGRESS_HEAD_WIDTH * (0 + 1) - 1 : EGRESS_HEAD_WIDTH * 0] :
                                    balancer_egress_valid[1] ? balancer_egress_head[EGRESS_HEAD_WIDTH * (1 + 1) - 1 : EGRESS_HEAD_WIDTH * 1] : 'd0;
        assign egress_data =  balancer_egress_valid[0] ? balancer_egress_data[EGRESS_DATA_WIDTH * (0 + 1) - 1 : EGRESS_DATA_WIDTH * 0] :
                                    balancer_egress_valid[1] ? balancer_egress_data[EGRESS_DATA_WIDTH * (1 + 1) - 1 : EGRESS_DATA_WIDTH * 1] : 'd0;
        assign egress_start = balancer_egress_start[0] ? balancer_egress_start[0] : 
                                    balancer_egress_start[1] ? balancer_egress_start[1] : 'd0;
        assign egress_last =  balancer_egress_last[0] ? balancer_egress_last[0] : 
                                    balancer_egress_last[1] ? balancer_egress_last[1] : 'd0;



        assign balancer_resource_req_ready[0] = balancer_resource_req_valid[0] ? resource_req_ready : 'd0;
        assign balancer_resource_req_ready[1] = balancer_resource_req_valid[0] ? 'd0 : 
                                                balancer_resource_req_valid[1] ? resource_req_ready : 'd0;

        assign balancer_egress_ready[0] = balancer_egress_valid[0] ? egress_ready : 'd0;
        assign balancer_egress_ready[1] = balancer_egress_valid[0] ? 'd0 : 
                                          balancer_egress_valid[1] ? egress_ready : 'd0;                                               
    end
    else if(OOO_STATION_NUM == 4) begin
        assign resource_req_valid = balancer_resource_req_valid[0] ? balancer_resource_req_valid[0] : 
                                    balancer_resource_req_valid[1] ? balancer_resource_req_valid[1] : 
                                    balancer_resource_req_valid[2] ? balancer_resource_req_valid[2] : 
                                    balancer_resource_req_valid[3] ? balancer_resource_req_valid[3] : 'd0;
        assign resource_req_head =  balancer_resource_req_valid[0] ? balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (0 + 1) - 1 : OOO_CMD_HEAD_WIDTH * 0] :
                                    balancer_resource_req_valid[1] ? balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (1 + 1) - 1 : OOO_CMD_HEAD_WIDTH * 1] : 
                                    balancer_resource_req_valid[2] ? balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (2 + 1) - 1 : OOO_CMD_HEAD_WIDTH * 2] : 
                                    balancer_resource_req_valid[3] ? balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (3 + 1) - 1 : OOO_CMD_HEAD_WIDTH * 3] : 'd0;
        assign resource_req_data =  balancer_resource_req_valid[0] ? balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (0 + 1) - 1 : OOO_CMD_DATA_WIDTH * 0] :
                                    balancer_resource_req_valid[1] ? balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (1 + 1) - 1 : OOO_CMD_DATA_WIDTH * 1] : 
                                    balancer_resource_req_valid[2] ? balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (2 + 1) - 1 : OOO_CMD_DATA_WIDTH * 2] : 
                                    balancer_resource_req_valid[3] ? balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (3 + 1) - 1 : OOO_CMD_DATA_WIDTH * 3] : 'd0;
        assign resource_req_start = balancer_resource_req_start[0] ? balancer_resource_req_start[0] : 
                                    balancer_resource_req_start[1] ? balancer_resource_req_start[1] : 
                                    balancer_resource_req_start[2] ? balancer_resource_req_start[2] : 
                                    balancer_resource_req_start[3] ? balancer_resource_req_start[3] : 'd0;
        assign resource_req_last =  balancer_resource_req_last[0] ? balancer_resource_req_last[0] : 
                                    balancer_resource_req_last[1] ? balancer_resource_req_last[1] : 
                                    balancer_resource_req_last[2] ? balancer_resource_req_last[2] : 
                                    balancer_resource_req_last[3] ? balancer_resource_req_last[3] : 'd0;


        assign egress_valid = balancer_egress_valid[0] ? balancer_egress_valid[0] : 
                                    balancer_egress_valid[1] ? balancer_egress_valid[1] : 
                                    balancer_egress_valid[2] ? balancer_egress_valid[2] : 
                                    balancer_egress_valid[3] ? balancer_egress_valid[3] : 'd0;
        assign egress_head =  balancer_egress_valid[0] ? balancer_egress_head[EGRESS_HEAD_WIDTH * (0 + 1) - 1 : EGRESS_HEAD_WIDTH * 0] :
                                    balancer_egress_valid[1] ? balancer_egress_head[EGRESS_HEAD_WIDTH * (1 + 1) - 1 : EGRESS_HEAD_WIDTH * 1] : 
                                    balancer_egress_valid[2] ? balancer_egress_head[EGRESS_HEAD_WIDTH * (2 + 1) - 1 : EGRESS_HEAD_WIDTH * 2] : 
                                    balancer_egress_valid[3] ? balancer_egress_head[EGRESS_HEAD_WIDTH * (3 + 1) - 1 : EGRESS_HEAD_WIDTH * 3] : 'd0;
        assign egress_data =  balancer_egress_valid[0] ? balancer_egress_data[EGRESS_DATA_WIDTH * (0 + 1) - 1 : EGRESS_DATA_WIDTH * 0] :
                                    balancer_egress_valid[1] ? balancer_egress_data[EGRESS_DATA_WIDTH * (1 + 1) - 1 : EGRESS_DATA_WIDTH * 1] : 
                                    balancer_egress_valid[2] ? balancer_egress_data[EGRESS_DATA_WIDTH * (2 + 1) - 1 : EGRESS_DATA_WIDTH * 2] : 
                                    balancer_egress_valid[3] ? balancer_egress_data[EGRESS_DATA_WIDTH * (3 + 1) - 1 : EGRESS_DATA_WIDTH * 3] : 'd0;
        assign egress_start = balancer_egress_start[0] ? balancer_egress_start[0] : 
                                    balancer_egress_start[1] ? balancer_egress_start[1] : 
                                    balancer_egress_start[2] ? balancer_egress_start[2] : 
                                    balancer_egress_start[3] ? balancer_egress_start[3] : 'd0;
        assign egress_last =  balancer_egress_last[0] ? balancer_egress_last[0] : 
                                    balancer_egress_last[1] ? balancer_egress_last[1] : 
                                    balancer_egress_last[2] ? balancer_egress_last[2] : 
                                    balancer_egress_last[3] ? balancer_egress_last[3] : 'd0;





        assign balancer_resource_req_ready[0] = balancer_resource_req_valid[0] ? resource_req_ready : 'd0;
        assign balancer_resource_req_ready[1] = balancer_resource_req_valid[0] ? 'd0 : 
                                                balancer_resource_req_valid[1] ? resource_req_ready : 'd0;
        assign balancer_resource_req_ready[2] = balancer_resource_req_valid[0] ? 'd0 : 
                                                balancer_resource_req_valid[1] ? 'd0 : 
                                                balancer_resource_req_valid[2] ? resource_req_ready : 'd0;
        assign balancer_resource_req_ready[3] = balancer_resource_req_valid[0] ? 'd0 : 
                                                balancer_resource_req_valid[1] ? 'd0 : 
                                                balancer_resource_req_valid[2] ? 'd0 :
                                                balancer_resource_req_valid[3] ? resource_req_ready : 'd0;



        assign balancer_egress_ready[0] = balancer_egress_valid[0] ? egress_ready : 'd0;
        assign balancer_egress_ready[1] = balancer_egress_valid[0] ? 'd0 : 
                                          balancer_egress_valid[1] ? egress_ready : 'd0;
        assign balancer_egress_ready[2] = balancer_egress_valid[0] ? 'd0 : 
                                          balancer_egress_valid[1] ? 'd0 :
                                          balancer_egress_valid[2] ? egress_ready : 'd0;
        assign balancer_egress_ready[3] = balancer_egress_valid[0] ? 'd0 : 
                                          balancer_egress_valid[1] ? 'd0 :
                                          balancer_egress_valid[2] ? 'd0 :
                                          balancer_egress_valid[3] ? egress_ready : 'd0;
    end    
endgenerate

generate
    genvar i;
    for(i = 0; i < OOO_STATION_NUM; i++) begin
        if(OOO_STATION_NUM == 1) begin
            assign balancer_ingress_valid[i]                                                        = ingress_valid;
            assign balancer_ingress_head[INGRESS_HEAD_WIDTH * (i + 1) - 1 : INGRESS_HEAD_WIDTH * i] = ingress_head;
            assign balancer_ingress_data[SLOT_WIDTH * (i + 1) - 1 : SLOT_WIDTH * i]                 = ingress_data;
            assign balancer_ingress_start[i]                                                        = ingress_start;
            assign balancer_ingress_last[i]                                                         = ingress_last;
        end
        else begin
            assign balancer_ingress_valid[i]                                                        = (ingress_valid && (ingress_head[OOO_STATION_NUM_LOG - 1 : 0] == i)) ? ingress_valid : 'd0;
            assign balancer_ingress_head[INGRESS_HEAD_WIDTH * (i + 1) - 1 : INGRESS_HEAD_WIDTH * i] = (ingress_valid && (ingress_head[OOO_STATION_NUM_LOG - 1 : 0] == i)) ? ingress_head : 'd0;
            assign balancer_ingress_data[SLOT_WIDTH * (i + 1) - 1 : SLOT_WIDTH * i]                 = (ingress_valid && (ingress_head[OOO_STATION_NUM_LOG - 1 : 0] == i)) ? ingress_data : 'd0;
            assign balancer_ingress_start[i]                                                        = (ingress_valid && (ingress_head[OOO_STATION_NUM_LOG - 1 : 0] == i)) ? ingress_start : 'd0;
            assign balancer_ingress_last[i]                                                         = (ingress_valid && (ingress_head[OOO_STATION_NUM_LOG - 1 : 0] == i)) ? ingress_last : 'd0;
        end

        if(OOO_STATION_NUM == 1) begin
                assign balancer_resource_resp_valid[i]                                                          = resource_resp_valid;
                assign balancer_resource_resp_head[OOO_RESP_HEAD_WIDTH * (i + 1) - 1 : OOO_RESP_HEAD_WIDTH * i] = resource_resp_head;
                assign balancer_resource_resp_data[OOO_RESP_DATA_WIDTH * (i + 1) - 1 : OOO_RESP_DATA_WIDTH * i] = resource_resp_data;
                assign balancer_resource_resp_start[i]                                                          = resource_resp_start;
                assign balancer_resource_resp_last[i]                                                           = resource_resp_last;
        end
        else begin
                assign balancer_resource_resp_valid[i]                                                          = (resource_resp_valid && (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == i)) ? resource_resp_valid : 'd0;
                assign balancer_resource_resp_head[OOO_RESP_HEAD_WIDTH * (i + 1) - 1 : OOO_RESP_HEAD_WIDTH * i] = (resource_resp_valid && (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == i)) ? resource_resp_head : 'd0;
                assign balancer_resource_resp_data[OOO_RESP_DATA_WIDTH * (i + 1) - 1 : OOO_RESP_DATA_WIDTH * i] = (resource_resp_valid && (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == i)) ? resource_resp_data : 'd0;
                assign balancer_resource_resp_start[i]                                                          = (resource_resp_valid && (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == i)) ? resource_resp_start : 'd0;
                assign balancer_resource_resp_last[i]                                                           = (resource_resp_valid && (resource_resp_head[TAG_NUM_LOG - 1 : TAG_NUM_LOG - OOO_STATION_NUM_LOG] == i)) ? resource_resp_last : 'd0;    
        end

        stream_reg #(
            .TUSER_WIDTH(   INGRESS_HEAD_WIDTH  ),
            .TDATA_WIDTH(   SLOT_WIDTH          )
        )
        (
            .clk            (   clk                                                                                     ),
            .rst_n          (   ~rst                                                                                    ),

            .axis_tvalid    (   balancer_ingress_valid[i]                                                               ),
            .axis_tlast     (   balancer_ingress_last[i]                                                                ),
            .axis_tuser     (   balancer_ingress_head[INGRESS_HEAD_WIDTH * (i + 1) - 1 : INGRESS_HEAD_WIDTH * i]        ),
            .axis_tdata     (   balancer_ingress_data[SLOT_WIDTH * (i + 1) - 1 : SLOT_WIDTH * i]                        ),
            .axis_tready    (   balancer_ingress_ready[i]                                                               ),
            .axis_tstart    (   balancer_ingress_start[i]                                                               ),
            .axis_tkeep     (   'd0                                                                                     ),

            .in_reg_tvalid  (   st_ingress_valid[i]                                                                     ),
            .in_reg_tlast   (   st_ingress_last[i]                                                                      ),
            .in_reg_tuser   (   st_ingress_head[INGRESS_HEAD_WIDTH * (i + 1) - 1 : INGRESS_HEAD_WIDTH * i]              ),
            .in_reg_tdata   (   st_ingress_data[SLOT_WIDTH * (i + 1) - 1 : SLOT_WIDTH * i]                              ),
            .in_reg_tkeep   (                                                                                           ),
            .in_reg_tstart  (   st_ingress_start[i]                                                                     ),
            .in_reg_tready  (   st_ingress_ready[i]                                                                     )
        );

        OoOStation #(
                .ID                          (  i                           ),

                .TAG_NUM                     (  TAG_NUM / OOO_STATION_NUM                           ),
                .TAG_NUM_LOG                 (  log2b(TAG_NUM / OOO_STATION_NUM - 1)                ),

                .RESOURCE_CMD_HEAD_WIDTH     (  RESOURCE_CMD_HEAD_WIDTH     ),
                .RESOURCE_CMD_DATA_WIDTH     (  RESOURCE_CMD_DATA_WIDTH     ),
                .RESOURCE_RESP_HEAD_WIDTH    (  RESOURCE_RESP_HEAD_WIDTH    ),
                .RESOURCE_RESP_DATA_WIDTH    (  RESOURCE_RESP_DATA_WIDTH    ),

                .SLOT_NUM                    (  SLOT_NUM                    ),
                .QUEUE_NUM                   (  QUEUE_NUM                   ),
                .SLOT_NUM_LOG                (  SLOT_NUM_LOG                ),
                .QUEUE_NUM_LOG               (  QUEUE_NUM_LOG               ),

                .OOO_CMD_HEAD_WIDTH          (  OOO_CMD_HEAD_WIDTH          ),
                .OOO_CMD_DATA_WIDTH          (  OOO_CMD_DATA_WIDTH          ),
                .OOO_RESP_HEAD_WIDTH         (  OOO_RESP_HEAD_WIDTH         ),
                .OOO_RESP_DATA_WIDTH         (  OOO_RESP_DATA_WIDTH         ),

                .INGRESS_HEAD_WIDTH          (  INGRESS_HEAD_WIDTH          ),
                .INGRESS_DATA_WIDTH          (  INGRESS_DATA_WIDTH          ),

                .SLOT_WIDTH                  (  SLOT_WIDTH                  ),

                .EGRESS_HEAD_WIDTH           (  EGRESS_HEAD_WIDTH           ),
                .EGRESS_DATA_WIDTH           (  EGRESS_DATA_WIDTH           )
        )
        (
            .clk                            (   clk                            ),
            .rst                            (   rst                            ),

            .ingress_valid                  (   st_ingress_valid[i]                                                                   ),
            .ingress_head                   (   st_ingress_head[INGRESS_HEAD_WIDTH * (i + 1) - 1 : INGRESS_HEAD_WIDTH * i]            ),
            .ingress_data                   (   st_ingress_data[SLOT_WIDTH * (i + 1) - 1 : SLOT_WIDTH * i]                            ),
            .ingress_start                  (   st_ingress_start[i]                                                                   ),
            .ingress_last                   (   st_ingress_last[i]                                                                    ),
            .ingress_ready                  (   st_ingress_ready[i]                                                                   ),

            .resource_req_valid             (   balancer_resource_req_valid[i]                                                              ),
            .resource_req_head              (   balancer_resource_req_head[OOO_CMD_HEAD_WIDTH * (i + 1) - 1 : OOO_CMD_HEAD_WIDTH * i]       ),
            .resource_req_data              (   balancer_resource_req_data[OOO_CMD_DATA_WIDTH * (i + 1) - 1 : OOO_CMD_DATA_WIDTH * i]       ),
            .resource_req_start             (   balancer_resource_req_start[i]                                                              ),
            .resource_req_last              (   balancer_resource_req_last[i]                                                               ),
            .resource_req_ready             (   balancer_resource_req_ready[i]                                                              ),

            .resource_resp_valid            (   balancer_resource_resp_valid[i]                                   ),
            .resource_resp_head             (   balancer_resource_resp_head[OOO_RESP_HEAD_WIDTH * (i + 1) - 1 : OOO_RESP_HEAD_WIDTH * i]    ),
            .resource_resp_data             (   balancer_resource_resp_data[OOO_RESP_DATA_WIDTH * (i + 1) - 1 : OOO_RESP_DATA_WIDTH * i]    ),
            .resource_resp_start            (   balancer_resource_resp_start[i]                                                             ),
            .resource_resp_last             (   balancer_resource_resp_last[i]                                                              ),
            .resource_resp_ready            (   balancer_resource_resp_ready[i]                                                             ),

            .egress_valid                   (   balancer_egress_valid[i]                                                                    ),
            .egress_head                    (   balancer_egress_head[EGRESS_HEAD_WIDTH * (i + 1) - 1 : EGRESS_HEAD_WIDTH * i]               ),
            .egress_data                    (   balancer_egress_data[EGRESS_DATA_WIDTH * (i + 1) - 1 : EGRESS_DATA_WIDTH * i]               ),
            .egress_start                   (   balancer_egress_start[i]                                                                    ),
            .egress_last                    (   balancer_egress_last[i]                                                                     ),
            .egress_ready                   (   balancer_egress_ready[i]                                                                    )
        );        
    end
endgenerate

/*------------------------------------------- Submodules Instatiation : End -----------------------------------------*/

/*------------------------------------------- State Machine Definition : Begin --------------------------------------*/
/*------------------------------------------- State Machine Definition : End ----------------------------------------*/

/*------------------------------------------- Variables Decode : Begin ----------------------------------------------*/


/*------------------------------------------- Variables Decode : End ------------------------------------------------*/

endmodule