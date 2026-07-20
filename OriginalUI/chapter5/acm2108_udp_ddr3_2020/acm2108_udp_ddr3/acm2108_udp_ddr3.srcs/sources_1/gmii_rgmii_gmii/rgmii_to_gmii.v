// Company       : 武汉芯路恒科技有限公司
//                 http://xiaomeige.taobao.com
// Web           : http://www.corecourse.cn
// 
// Create Date   : 2021/07/21 00:00:00
// Module Name   : rgmii_to_gmii
// Description   : 以太网接收rgmii转gmii
// 
// Dependencies  : 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
/////////////////////////////////////////////////////////////////////////////////

module rgmii_to_gmii(
  reset,

  rgmii_rx_clk,
  rgmii_rxd,
  rgmii_rxdv,

  gmii_rx_clk,
  gmii_rxdv,
  gmii_rxd,
  gmii_rxer
);



  input         reset;

  input         rgmii_rx_clk;
  input  [3:0]  rgmii_rxd;
  input         rgmii_rxdv;

  output        gmii_rx_clk;
  output [7:0]  gmii_rxd;
  output        gmii_rxdv;
  output        gmii_rxer;

  assign gmii_rx_clk = rgmii_rx_clk;

  genvar i;
  generate
    for(i=0;i<4;i=i+1)
    begin: rgmii_rxd_i
      IDDR #(
        .DDR_CLK_EDGE("SAME_EDGE_PIPELINED"), // "OPPOSITE_EDGE", "SAME_EDGE" 
                                              // or "SAME_EDGE_PIPELINED" 
        .INIT_Q1(1'b0   ),  // Initial value of Q1: 1'b0 or 1'b1
        .INIT_Q2(1'b0   ),  // Initial value of Q2: 1'b0 or 1'b1
        .SRTYPE ("SYNC" )   // Set/Reset type: "SYNC" or "ASYNC" 
      ) IDDR_rxd (
        .Q1   (gmii_rxd[i]   ), // 1-bit output for positive edge of clock
        .Q2   (gmii_rxd[i+4] ), // 1-bit output for negative edge of clock
        .C    (rgmii_rx_clk  ), // 1-bit clock input
        .CE   (1'b1          ), // 1-breset_nit clock enable input
        .D    (rgmii_rxd[i]  ), // 1-bit DDR data input
        .R    (reset         ), // 1-bit reset
        .S    (1'b0          )  // 1-bit set
      );
    end
  endgenerate

  IDDR #(
    .DDR_CLK_EDGE("SAME_EDGE_PIPELINED"), // "OPPOSITE_EDGE", "SAME_EDGE" 
                                          // or "SAME_EDGE_PIPELINED" 
    .INIT_Q1(1'b0   ),  // Initial value of Q1: 1'b0 or 1'b1
    .INIT_Q2(1'b0   ),  // Initial value of Q2: 1'b0 or 1'b1
    .SRTYPE ("SYNC" )   // Set/Reset type: "SYNC" or "ASYNC" 
  ) IDDR_rxdv (
    .Q1   (gmii_rxer    ), // 1-bit output for positive edge of clock
    .Q2   (gmii_rxdv    ), // 1-bit output for negative edge of clock
    .C    (rgmii_rx_clk ), // 1-bit clock input
    .CE   (1'b1         ), // 1-breset_nit clock enable input
    .D    (rgmii_rxdv   ), // 1-bit DDR data input
    .R    (reset        ), // 1-bit reset
    .S    (1'b0         )  // 1-bit set
  );

endmodule