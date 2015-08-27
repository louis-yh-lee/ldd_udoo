# ldd_udoo
linux device driver development for UDOO QUAD

Project Overview

My UDOO SECO board have Freescale i.MX6Q processor.
This ARM cortex-A9 QUAD SOC have VPU for decoding & encoding Video compression functionality.
Already OMXIL & Gstreamer & Android Media Player framework path has validated, 
but I need more enhancement for performance improvement of VPU encoding/decoding feature.
And If I can do that, the finale of this project might be a modified VPU firmware.
I have long-term experience worked for VPU development that was integrated in this SOC model. 

i.e. the purpose of this project are

* VPU Decoder 
1. MXC VPU decoder LDD improvement for embedded ubuntu kernel
2. OMXIL modifcation
3. Ubuntu HTPC Application based on Gstreamer 

* VPU Encoder
4. MXC VPU encoder feature upgrade; rd performance improvement
5. V4L2 driver for capture
6. Gstreamer encoder path improvement

