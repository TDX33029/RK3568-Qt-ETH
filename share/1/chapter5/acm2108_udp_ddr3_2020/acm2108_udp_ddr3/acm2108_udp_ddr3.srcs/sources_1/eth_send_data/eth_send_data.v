module eth_send_data#(
    parameter dst_mac = 48'hFF_FF_FF_FF_FF_FF,
    parameter src_mac = 48'h00_0a_35_01_fe_c0,
    parameter dst_ip = 32'hc0_a8_00_03,
    parameter src_ip = 32'hc0_a8_00_02,
    parameter dst_port = 16'd6102,
    parameter src_port = 16'd5000
)
(
    input reset,
    input clk_50M,
    input clk125m_o,
    input [15:0] eth_fifo_wrdata,
    input eth_fifo_wrreq,
    input RestartReq_0_d1,
    input [31:0]Number_d1,

    output       rgmii_tx_clk,
    output [3:0] rgmii_txd,
    output       rgmii_txen
);
    wire [7:0] dout;
    wire eth_fifo_tx_empty;
    wire eth_fifo_usedw;
    wire [14:0]rd_data_count;

    wire tx_done;
    wire tx_en_pulse;
    wire [15:0]lenth_val;
    wire	payload_req_o; 
    
    wire gmii_tx_clk;
    wire [7:0]gmii_txd;
    wire gmii_txen;
    fifo_tx fifo_tx (
      .rst(reset),                      // input wire rst
      .wr_clk(clk_50M),                // input wire wr_clk
      .rd_clk(clk125m_o),                // input wire rd_clk
      .din({eth_fifo_wrdata[7:0],eth_fifo_wrdata[15:8]}),                      // input wire [15 : 0] din
      .wr_en(eth_fifo_wrreq),                  // input wire wr_en
      .rd_en(payload_req_o),                  // input wire rd_en
      .dout(dout),                    // output wire [7 : 0] dout
      .full(),                    // output wire full
      .empty(eth_fifo_tx_empty),        // output wire empty
      .wr_data_count(eth_fifo_usedw),  // output wire [9 : 0] wr_data_count
      .rd_data_count(rd_data_count),
      .wr_rst_busy(),      // output wire wr_rst_busy
      .rd_rst_busy()      // output wire rd_rst_busy
   );

    eth_send_ctrl eth_send_ctrl(
      .clk125M(clk125m_o),     
      .reset_n(!reset),  
      .eth_tx_done(tx_done),  
      .restart_req(RestartReq_0_d1),
      .fifo_rd_cnt(rd_data_count),
      .total_data_num(Number_d1),  
      .pkt_tx_en(tx_en_pulse),  
      .pkt_length(lenth_val) 
   ); 


    eth_udp_tx_gmii eth_udp_tx_gmii
    (
        .clk125m       (clk125m_o             ),
        .reset_p       (reset                 ),

        .tx_en_pulse   (tx_en_pulse           ),
        .tx_done       (tx_done               ),

        .dst_mac       (dst_mac),
        .src_mac       (src_mac), 
        .dst_ip        (dst_ip      ),
        .src_ip        (src_ip      ),
        .dst_port      (dst_port             ),
        .src_port      (src_port             ),
        

        .data_length   (lenth_val            ),
        
        .payload_req_o (payload_req_o        ),
        .payload_dat_i (dout                 ),

        .gmii_tx_clk   (gmii_tx_clk          ),
        .gmii_txen     (gmii_txen            ),
        .gmii_txd      (gmii_txd             )
    );
  
    gmii_to_rgmii gmii_to_rgmii(
        .reset_n(!reset),
        
        .gmii_tx_clk(gmii_tx_clk),
        .gmii_txd(gmii_txd),
        .gmii_txen(gmii_txen),
        .gmii_txer(1'b0),
        
        .rgmii_tx_clk(rgmii_tx_clk),
        .rgmii_txd(rgmii_txd),
        .rgmii_txen(rgmii_txen)
    );
endmodule 