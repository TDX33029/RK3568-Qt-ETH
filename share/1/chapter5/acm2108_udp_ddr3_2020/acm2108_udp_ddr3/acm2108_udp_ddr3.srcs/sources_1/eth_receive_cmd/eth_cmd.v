`timescale 1ns / 1ps
module eth_cmd(
    clk,
    reset_n,
    rx_empty,
    fifodout,
    
    fifo_rd_req,  
    cmdvalid,      
    address,  
    cmd_data
   );
   
    input clk;
    input reset_n;
    input [7:0]fifodout;
    input rx_empty;
    
    output reg fifo_rd_req;
    output reg cmdvalid;
    output reg [7:0]address;
    output reg [31:0]cmd_data;   
    
    reg [7:0]data_0[7:0]; 
	 
	 always@(posedge clk or negedge reset_n)
    if(!reset_n)
		  fifo_rd_req <= 1'b0; 
	 else if(!rx_empty)    
        fifo_rd_req <= 1'b1;
	 else
		  fifo_rd_req <= 1'b0;
		  
	 always@(posedge clk)
    if(fifo_rd_req)begin
        data_0[7] <= #1 fifodout;
        data_0[6] <= #1 data_0[7];
        data_0[5] <= #1 data_0[6];
        data_0[4] <= #1 data_0[5];
        data_0[3] <= #1 data_0[4];
        data_0[2] <= #1 data_0[3];
        data_0[1] <= #1 data_0[2];
        data_0[0] <= #1 data_0[1];        
    end
    
    reg fifo_rx_done;
    always@(posedge clk)
        fifo_rx_done <= fifo_rd_req;

	  
	 always@(posedge clk or negedge reset_n)
	 if(!reset_n)begin
 		  address <= 0;     
 		  cmd_data <= 32'd0;
 		  cmdvalid <= 1'b0;
	 end 			
		  
	 else if(fifo_rx_done)begin
        if((data_0[0] == 8'h55) && (data_0[1] == 8'hA5) && (data_0[7] == 8'hF0))begin
            cmd_data[7:0] <= #1 data_0[6];
            cmd_data[15:8] <= #1 data_0[5];
            cmd_data[23:16] <= #1 data_0[4];
            cmd_data[31:24] <= #1 data_0[3];
            address <= #1 data_0[2];
            cmdvalid <= #1 1;
        end
       else
            cmdvalid <= #1 0;  
	 end
	else
        cmdvalid <= #1 0;  
            
endmodule
