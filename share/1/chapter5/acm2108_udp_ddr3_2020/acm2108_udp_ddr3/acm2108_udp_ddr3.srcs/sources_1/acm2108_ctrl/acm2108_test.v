/*
1、本例程使用DDS原理来验证DAC模块的模拟信号输出功能
DAC和DDS单元工作在125MHz的转换速率，可以输出正弦波，三角波，方波

使用开发板上PL侧的按键来切换输出的波形,总共24种波形轮流切换，分别为：

0 	：1.25K 正弦波
1 	：1.25K 方波
2 	：1.25K 三角波

3 	：12.5K 正弦波
4 	：12.5K 方波
5 	：12.5K 三角波

6 	：62.5K 正弦波
7 	：62.5K 方波
8 	：62.5K 三角波

9 	：125K 正弦波
10	：125K 方波
11	：125K 三角波

12	：625K 正弦波
13	：625K 方波
14	：625K 三角波

15	：1.25M 正弦波
16	：1.25M 方波
17	：1.25M 三角波

18	：6.25M 正弦波
19	：6.25M 方波
20	：6.25M 三角波

21	：12.5M 正弦波
22	：12.5M 方波
23	：12.5M 三角波


ADC使用ILA抓取ADC采集到的数据内容。

实验时，可以使用示波器观测DAC通道的输出模拟波形，也可以将DAC的输出连接给ADC，由ADC采集DAC的输出信号。并在ILA中观察模拟波形

*/

module acm2108_test(
	input Clk,
	input key_in,
	input [7:0]AD0, 
	input [7:0]AD1,
	
	input clk125m,
	input clk50m,
	input AD_Clk,
	
	output AD0_CLK,
	output AD1_CLK,
	output [7:0]DA0_Data, 
	output [7:0]DA1_Data,
	output DA0_Clk,
	output DA1_Clk
);

    reg [4:0]MODE = 0;
    wire key_flag;
    wire key_state;
    
    reg [7:0]AD0_Data;
    reg [7:0]AD1_Data;
//    wire clk125m;
    
    wire [7:0]DA_Data;
    wire DA_Clk;
 //   wire AD_Clk;
    
//    wire clk50m;
    
    assign DA0_Data = DA_Data;
    assign DA1_Data = DA_Data;
    
    assign DA0_Clk = DA_Clk;
    assign DA1_Clk = DA_Clk;
		
	assign AD0_CLK = AD_Clk;
	assign AD1_CLK = AD_Clk;
	
	always@(posedge clk50m)begin
	   AD0_Data <= AD0;
	   AD1_Data <= AD1;
	end
	
	DDS_Module DDS_Module(
		.Clk(clk125m),
		.Rst_n(1'd1),
		.EN(1'd1),
		.MODE(MODE),
		.DA_Clk(DA_Clk),
		.DA_Data(DA_Data)
	);
	
	key_filter key_filter0(
		.Clk(Clk),      //50M时钟输入
		.Rst_n(1'd1),    //模块复位
		.key_in(key_in),   //按键输入
		.key_flag(key_flag), //按键标志信号
		.key_state(key_state) //按键状濁信叿
	);
	
	always@(posedge Clk)
	if(key_flag & (!key_state))begin
	   if(MODE >= 23)
	       MODE <= 0;
	   else
	       MODE <= MODE + 1'd1;
	end
	else
	   MODE <= MODE;
	   
//    ila_0 ila_adc0 (
//        .clk(clk50m), // input wire clk
//        .probe0(AD0_Data) // input wire [7:0] probe0
//    );	
    
//    ila_0 ila_adc1 (
//        .clk(clk50m), // input wire clk
//        .probe0(AD1_Data) // input wire [7:0] probe0
//    );	
    
//   ila_0 ila_dac (
//        .clk(DA_Clk), // input wire clk
//        .probe0(DA_Data) // input wire [7:0] probe0
//    );	  
    
endmodule
