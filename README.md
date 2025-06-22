# JingZhao: An Open-Source Framework for Rapid NIC Prototyping

JingZhao is an open-sourced framework for NIC prototyping, which could be leveraged to
rapidly implement and verify a domain-specific NIC. JingZhao is the first open-source RDMA NIC solution completely compatible to the standard OFED communication library. The RNIC was also taped out using TSMC's 28nm process to validate our design.

For design considerations and principles in detail, see [our paper](https://arxiv.org/pdf/2410.08476).

## Sub-directories Overview

```
|---JingZhao1               # first version
|---JingZhao2
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
        |---src             # source files of verification framework
```

## Hardware Design

## RTL Simulation

JingZhao project provides RTL simulation framework written in SystemVerilog in directory `JingZhao2/verification`. `JingZhao2/verification/bin/Makefile` defines the parameters for compilation and test case.

To use the framework, make sure your system has VCS and UVM installed. 

Compile the framework by `make com`. Then run the verification by `make run`. This command runs a script defining communication patterns in the verification. The verification generates a log file named `run.log` and a waveform file named `wave.vpd`. The waveform file can be opened using DVE, a waveform viewer embedded in VCS.

## Gem5 Simulation

To quickly evaluate the performance of a NIC hardware design before implemented in RTL, JingZhao provides a NIC simulator. The project is in directory `JingZhao2/simulator/RNIC_Simulator`. The simulator is written mainly in C++, based on Gem5. For details, see the simulator submodule.

## LICENSE

Copyright © 2020-2025 Institute of Computing Technology, Chinese Academy of Sciences.