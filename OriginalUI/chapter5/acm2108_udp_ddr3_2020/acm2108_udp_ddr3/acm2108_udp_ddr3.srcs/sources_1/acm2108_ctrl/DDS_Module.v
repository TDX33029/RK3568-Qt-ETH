/***************************************************
*	Module Name		:	DDS_Module		   
*	Engineer		:	小梅哥
*	Target Device	:	EP4CE10F17C8
*	Tool versions	:	Quartus II 13.0
*	Create Date		:	2017-06-25
*	Revision		:	v1.1
*	Description		:  DDS功能模块，根据频率控制字和相位控制字产生对应正弦数据输出
**************************************************/

module DDS_Module(
	Clk,
	Rst_n,
	EN,
	MODE,
	DA_Clk,
	DA_Data
);

	input Clk;/*系统时钟*/
	input Rst_n;/*系统复位*/
	input EN;/*DDS模块使能*/
	input [4:0]MODE;
	
	output DA_Clk;/*DA数据输出时钟*/
	output reg [7:0] DA_Data;/*D输出数字波形数据*/
	
	wire [7:0] wave_sin, wave_square, wave_triangle;
	
	reg [31:0]Fword;/*频率控制字*//*Fword = 2^^N * Fout / Fclk;*/
	reg [31:0]Fre_acc;	
	reg [7:0]Rom_Addr;
	
	reg [1:0] wave_a, wave_b;//当前通道的波形种类
	
	always @ (posedge Clk or negedge Rst_n)
	begin
		if (!Rst_n)
			Fword <= 32'd42950;
		else
			case (MODE)
				5'd0,  5'd1,  5'd2	:	Fword <= 32'd42950;		//1.25k				
				5'd3,  5'd4,  5'd5	:	Fword <= 32'd429500;	//12.5k
				5'd6,  5'd7,  5'd8	:	Fword <= 32'd2147484;	//62.5k
				5'd9,  5'd10, 5'd11	:	Fword <= 32'd4295000;	//125k
				5'd12, 5'd13, 5'd14	:	Fword <= 32'd21474840;	//625k
				5'd15, 5'd16, 5'd17	:	Fword <= 32'd42950000;	//1.25m
				5'd18, 5'd19, 5'd20	:	Fword <= 32'd214748400;	//6.25m
				5'd21, 5'd22, 5'd23	:	Fword <= 32'd429500000;	//12.5m
			endcase
	end
	
	always @ (posedge Clk or negedge Rst_n)
	begin
		if (!Rst_n)
            DA_Data <= wave_sin;
		else
			case (MODE)
				5'd0,5'd3,5'd6,5'd9,5'd12,5'd15,5'd18,5'd21: DA_Data <= wave_sin;
				5'd1,5'd4,5'd7,5'd10,5'd13,5'd16,5'd19,5'd22: DA_Data <= wave_square;
				5'd2,5'd5,5'd8,5'd11,5'd14,5'd17,5'd20,5'd23: DA_Data <= wave_triangle;
			endcase
	end

/*---------------相位累加器------------------*/	
	always @(posedge Clk or negedge Rst_n)
	if(!Rst_n)
		Fre_acc <= 32'd0;
	else if(!EN)
		Fre_acc <= 32'd0;	
	else 
		Fre_acc <= Fre_acc + Fword;

/*----------生成查找表地址---------------------*/		
	always @(posedge Clk or negedge Rst_n)
	if(!Rst_n)
		Rom_Addr <= 8'd0;
	else if(!EN)
		Rom_Addr <= 8'd0;
	else
		Rom_Addr <= Fre_acc[31:24];	

/*----------例化查找表ROM-------*/		
	sin_rom_a8d8 ddsrom_sin(
		.addr(Rom_Addr),
		.clk(Clk),
		.q(wave_sin)
	);
	
	square_wave_rom_a8d8 ddsrom_square(
		.addr(Rom_Addr),
		.clk(Clk),
		.q(wave_square)
	);
	
	triangular_rom_a8d8 ddsrom_triangle(
		.addr(Rom_Addr),
		.clk(Clk),
		.q(wave_triangle)
	);

/*----------输出DA时钟----------*/	
	assign DA_Clk = Clk;

endmodule
