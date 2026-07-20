//////////////////////////////////////////////////////////////////////////////////
// Company: 武汉芯路恒科技有限公司
// Engineer: www.corecourse.cn
// 
// Create Date: 2021/09/20 00:00:00
// Design Name: 
// Module Name: 
// Project Name: 
// Target Devices: xc7z020clg400-2
// Tool Versions: Vivado 2018.3
// Description: gmii转rgmii
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////
module gmii_to_rgmii(
  reset_n,

  gmii_tx_clk,
  gmii_txd,
  gmii_txen,
  gmii_txer,

  rgmii_tx_clk,
  rgmii_txd,
  rgmii_txen
);



  input        reset_n;

  input        gmii_tx_clk;
  input  [7:0] gmii_txd;
  input        gmii_txen;
  input        gmii_txer;
  
  output       rgmii_tx_clk;
  output [3:0] rgmii_txd;
  output       rgmii_txen;

  genvar i;
  generate
    for(i=0;i<4;i=i+1)
    begin: rgmii_txd_o
      ODDR #(
        .DDR_CLK_EDGE("SAME_EDGE"), // "OPPOSITE_EDGE" or "SAME_EDGE" 
        .INIT  (1'b0   ),           // Initial value of Q: 1'b0 or 1'b1
        .SRTYPE("SYNC" )            // Set/Reset type: "SYNC" or "ASYNC" 
      ) ODDR_rgmii_txd (
        .Q   (rgmii_txd[i]     ), // 1-bit DDR output
        .C   (gmii_tx_clk      ), // 1-bit clock input
        .CE  (1'b1             ), // 1-bit clock enable input
        .D1  (gmii_txd[i]      ), // 1-bit data input (positive edge)
        .D2  (gmii_txd[i+4]    ), // 1-bit data input (negative edge)
        .R   (~reset_n         ), // 1-bit reset
        .S   (1'b0             )  // 1-bit set
      );
    end
  endgenerate

  ODDR #(
    .DDR_CLK_EDGE("SAME_EDGE"), // "OPPOSITE_EDGE" or "SAME_EDGE" 
    .INIT  (1'b0   ),           // Initial value of Q: 1'b0 or 1'b1
    .SRTYPE("SYNC" )            // Set/Reset type: "SYNC" or "ASYNC" 
  ) ODDR_rgmii_txd (
    .Q   (rgmii_txen          ), // 1-bit DDR output
    .C   (gmii_tx_clk         ), // 1-bit clock input
    .CE  (1'b1                ), // 1-bit clock enable input
    .D1  (gmii_txen           ), // 1-bit data input (positive edge)
    .D2  (gmii_txen^gmii_txer ), // 1-bit data input (negative edge)
    .R   (~reset_n            ), // 1-bit reset
    .S   (1'b0                )  // 1-bit set
  );

  ODDR #(
    .DDR_CLK_EDGE("SAME_EDGE"), // "OPPOSITE_EDGE" or "SAME_EDGE" 
    .INIT  (1'b0   ),           // Initial value of Q: 1'b0 or 1'b1
    .SRTYPE("SYNC" )            // Set/Reset type: "SYNC" or "ASYNC" 
  ) ODDR_rgmii_clk (
    .Q   (rgmii_tx_clk  ), // 1-bit DDR output
    .C   (gmii_tx_clk   ), // 1-bit clock input
    .CE  (1'b1          ), // 1-bit clock enable input
    .D1  (1'b1          ), // 1-bit data input (positive edge)
    .D2  (1'b0          ), // 1-bit data input (negative edge)
    .R   (~reset_n      ), // 1-bit reset
    .S   (1'b0          )  // 1-bit set
  );

endmodule
