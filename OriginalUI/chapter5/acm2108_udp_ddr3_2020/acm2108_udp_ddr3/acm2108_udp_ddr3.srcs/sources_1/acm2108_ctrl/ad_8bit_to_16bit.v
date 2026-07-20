
module ad_8bit_to_16bit(
    clk,
    ad_sample_en,
    ch_sel,
    AD0,
    AD1,
    ad_out,
    ad_out_valid
    );
  input clk;
  input ad_sample_en;
  input [1:0]ch_sel;
  input [7:0] AD0;
  input [7:0] AD1;
  output[15:0] ad_out;
  output ad_out_valid;
  reg[15:0] ad_out;
  reg ad_out_valid;
    
    //用于仿真或产生测试数据
  reg [7:0]adc_test_data;
  //测试数据，当ad_sample_en为1时，锁相环生成的50M时钟每个周期使adc_test_data加1
  always@(posedge clk)
    adc_test_data <= ad_sample_en ? (adc_test_data + 1'b1) : 8'd0;  
    
   wire [7:0]s_ad_in1;
   wire [7:0]s_ad_in2;
   
   assign s_ad_in1 = AD0 + 8'd128;
   assign s_ad_in2 = AD1 + 8'd128;  
	
   always @(posedge clk)
   if(ad_sample_en && ch_sel == 2'b01)
     ad_out<={8'd0,s_ad_in1};//
   else if(ad_sample_en && ch_sel == 2'b10)
     ad_out<={8'd0,s_ad_in2};//
   else if(ad_sample_en && ch_sel == 2'b00)
     ad_out<={8'd0,adc_test_data};
   else
     ad_out <= 16'd0;

   always @(posedge clk)
     ad_out_valid <= ad_sample_en;
endmodule
