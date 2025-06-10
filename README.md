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