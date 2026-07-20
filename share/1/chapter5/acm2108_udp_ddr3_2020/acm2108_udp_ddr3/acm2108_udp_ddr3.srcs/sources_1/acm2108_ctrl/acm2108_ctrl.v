module acm2108_ctrl(
    input Clk,
    input reset_n,
    input [31:0] ADC_Speed_Set,
    input [1:0]ChannelSel,
    
    input key_in,
    input [7:0]AD0, 
	input [7:0]AD1,
    input clk125m,
    input AD_Clk,

    output ad_sample_en,
    output adc_data_en,
    output AD0_CLK,
	output AD1_CLK,
	output [7:0]DA0_Data, 
	output [7:0]DA1_Data,
	output DA0_Clk,
	output DA1_Clk,

    output[15:0] ad_out,
    output ad_out_valid

);

	speed_ctrl speed_ctrl_0(
		.clk(Clk),
		.reset_n(reset_n),
		.ad_sample_en(ad_sample_en),
		.adc_data_en(adc_data_en),
		.div_set(ADC_Speed_Set)
	);

    acm2108_test acm2108_test(
        .Clk(Clk),
        .key_in(key_in),
        .AD0(AD0), 
        .AD1(AD1),
        .clk125m(clk125m),
        .clk50m(Clk),
        .AD_Clk(AD_Clk),
        .AD0_CLK(AD0_CLK),
        .AD1_CLK(AD1_CLK),
        .DA0_Data(DA0_Data), 
        .DA1_Data(DA1_Data),
        .DA0_Clk(DA0_Clk),
        .DA1_Clk(DA1_Clk)
     );

    //将acm2108采样的8位数据转换成16位的数据，方便给上位机进行分析
    ad_8bit_to_16bit(
       .clk(Clk),
       .ad_sample_en(ad_sample_en),
       .ch_sel(ChannelSel),
       .AD0(AD0),
       .AD1(AD1),
       .ad_out(ad_out),
       .ad_out_valid(ad_out_valid)
    );
     
endmodule