# JingZhao: An Open-Source Framework for Rapid NIC Prototyping

For design considerations and principles, see [our paper](https://arxiv.org/pdf/2410.08476).

## Sub-directories Overview

```
JingZhao2
|---benchmark
|---documents
|---hardware
|   |---fpga
|   |---hdl             # hardware design of JingZhao
|---simulator           # RNIC architecture simulator based on Gem5
|---drivers
|   |---ib_hgrnic       # user-space driver
|   |---libhgrnic       # kernel-space driver
|---verification
    |---bin             # verification configuration scripts
    |---src             # source files of verification
```

## RTL Simulation

JingZhao project provides RTL simulation framework written in SystemVerilog in directory JingZhao2/verification. JingZhao/verification/bin/Makefile defines the parameters for compilation and test case.

To use the framework, make sure your system has VCS and UVM installed. 

Compile the framework by `make com`. Then run the verification by `make run`. This command runs a script defining communication patterns in the verification. The verification generates a log file named `run.log` and a waveform file named `wave.vpd`. The waveform file can be opened using DVE.

## Gem5 Simulation


  The network is becoming domain-specific, which requires on-demand design of the network protocols, as well as the
microarchitecture of the NIC. However, to develop such a NIC is not that easy. Since the scissor gap between network
speed and the growth of CPU frequency is expanding, most of the protocols need to be offloaded to hardware. The process
of designing, verifying and optimizing a domain-specific NIC usually takes great effort, which hinders the rapid iteration
of new protocols and algorithms.

  We propose JingZhao, an open-sourced framework for NIC prototyping, which could be leveraged to
rapidly implement and verify a domain-specific NIC. Using this framework, we implement a fully-functional RDMA NIC
(RNIC). To the best of our knowledge, this represents the first open-source RDMA NIC solution with complete
compatibility to the standard OFED communication library. The RNIC was also taped out using TSMC's 28nm process to
validate our design. Our evaluation results show that new network functions can be easily integrated into the framework,
and achieve nearly line-rate packet processing.
Detailed discussion could be found in our paper:
https://arxiv.org/abs/2410.08476
