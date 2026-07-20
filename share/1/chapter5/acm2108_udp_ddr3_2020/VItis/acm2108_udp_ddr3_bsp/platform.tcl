# 
# Usage: To re-create this platform project launch xsct with below options.
# xsct C:\Users\24165\Desktop\acm2108_udp_ddr3_2020\VItis\acm2108_udp_ddr3_bsp\platform.tcl
# 
# OR launch xsct and run below command.
# source C:\Users\24165\Desktop\acm2108_udp_ddr3_2020\VItis\acm2108_udp_ddr3_bsp\platform.tcl
# 
# To create the platform in a different location, modify the -out option of "platform create" command.
# -out option specifies the output directory of the platform project.

platform create -name {acm2108_udp_ddr3_bsp}\
-hw {C:\Users\24165\Desktop\acm2108_udp_ddr3_2020\acm2108_udp_ddr3\acm2108_udp_ddr3.xsa}\
-proc {ps7_cortexa9_0} -os {standalone} -fsbl-target {psu_cortexa53_0} -out {C:/Users/24165/Desktop/acm2108_udp_ddr3_2020/VItis}

platform write
platform generate -domains 
platform active {acm2108_udp_ddr3_bsp}
platform generate
