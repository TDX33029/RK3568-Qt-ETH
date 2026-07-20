`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 2021/02/23 14:39:01
// Design Name: 
// Module Name: uart_cmd_rx
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////
//本模块的功能是识别初始值是否设置了通道选择（01,10），如果没有设置通道选择，则采用计数器（00）发送
module cmd_rx(
    clk,
    reset_n,
	 
	 cmdvalid,
    cmd_addr,
    cmd_data,
	 
    ChannelSel,
    DataNum,
	 ADC_Speed_Set,
    RestartReq
);
    input clk;
    input reset_n;
    input [31:0]cmd_data;
    input cmdvalid;
    input [7:0]cmd_addr;
	 
    output reg [1:0]ChannelSel;
    output reg [31:0]DataNum;
    output reg [31:0]ADC_Speed_Set;
    output reg RestartReq;
      
    always@(posedge clk or negedge reset_n)
    if(!reset_n)begin
      ChannelSel <= 2'b00;
      DataNum <= 32'd0;
      RestartReq <= 1'b0;
	   ADC_Speed_Set <= 32'd0; //50M采样率
    end
    else if(cmdvalid)begin
      case(cmd_addr)
          0: RestartReq <= 1'b1;
          1: ChannelSel <= cmd_data[1:0];
          2: DataNum <= cmd_data[31:0];
			 3: ADC_Speed_Set <= cmd_data[31:0];
			 
      default:;
      endcase
    end
    else
      RestartReq <= 1'b0;			
endmodule