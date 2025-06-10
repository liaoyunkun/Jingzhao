# JingZhao: An Open-Source Framework for Rapid NIC Prototyping

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
