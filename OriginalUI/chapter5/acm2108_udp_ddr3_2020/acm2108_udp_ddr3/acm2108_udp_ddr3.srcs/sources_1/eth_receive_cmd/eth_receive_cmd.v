module eth_receive_cmd#(
    parameter local_mac = 48'h00_0a_35_01_fe_c0,
    parameter local_ip = 32'hc0_a8_00_02,
    parameter local_port = 16'd5000
)
(
    input rgmii_rx_clk_i,
    input clk_50M,
    input reset,
    input  [3:0]    rgmii_rxd,
    input           rgmii_rxdv,

    output clk125m_o,
    output [1:0]ChannelSel,
    output [31:0]DataNum,
    output [31:0]ADC_Speed_Set,
    output RestartReq
);
    wire rgmii_rx_clk;
    wire gmii_rx_clk;
    wire [7:0] gmii_rxd;
    wire gmii_rxdv;

    wire [7:0]payload_dat_o;
    wire payload_valid_o;
    wire one_pkt_done;

    //rxfifo 
    wire [7:0]rxdout;
    wire rx_empty;
    wire fifo_rd_req;

    //rxcmd
    wire cmdvalid_0;
    wire [7:0]address_0;
    wire [31:0]cmd_data_0; 

    mmcm mmcm
    (
    // Clock out ports
    .clk_out1(rgmii_rx_clk),     // output clk_out1
   // Clock in ports
    .clk_in1(rgmii_rx_clk_i));      // input clk_in1
    
    rgmii_to_gmii rgmii_to_gmii(
        .reset(reset),
      
        .rgmii_rx_clk(rgmii_rx_clk),
        .rgmii_rxd(rgmii_rxd),
        .rgmii_rxdv(rgmii_rxdv),
      
        .gmii_rx_clk(gmii_rx_clk),
        .gmii_rxdv(gmii_rxdv),
        .gmii_rxd(gmii_rxd),
        .gmii_rxer( )
    );

    eth_udp_rx_gmii eth_udp_rx_gmii(
      .reset_p         (reset                ),
   
      .local_mac       (local_mac             ),
      .local_ip        (local_ip              ),
      .local_port      (local_port            ),
   
      .clk125m_o       (clk125m_o             ),
      .exter_mac       (                      ),
      .exter_ip        (                      ),
      .exter_port      (                      ),
      .rx_data_length  (                      ),
      .data_overflow_i (                      ),
      .payload_valid_o (payload_valid_o       ),
      .payload_dat_o   (payload_dat_o         ),
   
      .one_pkt_done    (one_pkt_done          ),
      .pkt_error       (                      ),
      .debug_crc_check (                      ),
   
      .gmii_rx_clk     (gmii_rx_clk           ),
      .gmii_rxdv       (gmii_rxdv             ),
      .gmii_rxd        (gmii_rxd              )
    );

    fifo_rx fifo_rx (    
      .rst(reset), // input wire rst
      .wr_clk(clk125m_o), // input wire wr_clk
      .rd_clk(clk_50M), // input wire rd_clk
      .din(payload_dat_o), // input wire [7 : 0] din
      .wr_en(payload_valid_o), // input wire wr_en
      .rd_en(fifo_rd_req), // input wire rd_en
      .dout(rxdout), // output wire [7 : 0] dout
      .full(), // output wire full
      .empty(rx_empty) // output wire empty
	);
	
    eth_cmd eth_cmd (
      .clk(clk_50M),
      .reset_n(!reset),
      .fifo_rd_req(fifo_rd_req),
      .rx_empty(rx_empty),
      .fifodout(rxdout),
      .cmdvalid(cmdvalid_0),
      .address(address_0),
      .cmd_data(cmd_data_0)
    );
   
   cmd_rx cmd_rx_0(
		.clk(clk_50M),
		.reset_n(!reset),
		.cmdvalid(cmdvalid_0),
		.cmd_addr(address_0),
		.cmd_data(cmd_data_0),
		
		.ChannelSel(ChannelSel),
		.DataNum(DataNum),
		.ADC_Speed_Set(ADC_Speed_Set),
		.RestartReq(RestartReq)
	);


endmodule
