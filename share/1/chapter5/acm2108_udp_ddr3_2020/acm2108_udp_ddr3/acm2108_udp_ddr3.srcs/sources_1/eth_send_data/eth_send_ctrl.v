
`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
module eth_send_ctrl(
  input clk125M,     
  input reset_n,  //模块的复位信号
  input eth_tx_done,    //以太网一个包发送完毕信号
  input restart_req,
  input [10:0]fifo_rd_cnt, //从FIFO中读取的数据个数
  input [31:0]total_data_num,  //需要发送的数据总数
  
  output reg pkt_tx_en ,   //以太网发送使能信号
  output reg [15:0]pkt_length  //以太网需要发送的数据的长度
); 
  
  //采集数据最大字节数：1500-IP报文头部（20字节）-UDP报文头部（8字节）= 1472字节

  reg[3:0]state;
  reg [31:0]data_num;
  
  reg [28:0]cnt_dly_time;

  parameter cnt_dly_min = 16'd128;
    
  always@(posedge clk125M or negedge reset_n)
  if(!reset_n) begin
    pkt_tx_en <= 1'd0;
    pkt_length <= 16'd0;
    data_num <= 32'd0;
    state <= 0;
    cnt_dly_time <= 28'd0;
  end
  else begin
    case(state)
        0:
            begin
                if(restart_req)begin
                    data_num <= total_data_num;
                    if((total_data_num << 1) >= 16'd1472)begin
                        pkt_length <= 16'd1472;	//一个数据2个字节
                        state <= 1;
                    end
                    else if((total_data_num << 1) > 0)begin
                        pkt_length <= total_data_num << 1; //一个数据2个字节
                        state <= 1;
                    end
                    else begin
                        state <= 0;
                    end
				end
            end
         1: 
            begin
                if(fifo_rd_cnt >= (pkt_length -2)) begin
                    pkt_tx_en <= 1'd1;
                    state <= 2;
                end
                else begin
                    state <= 1;
                    pkt_tx_en <= 1'd0;
                end
            end
         2:
            begin
                pkt_tx_en <= 1'd0;
                if(eth_tx_done)begin
					data_num <= data_num - pkt_length/2;
					state <= 3;
				end
            end
         
        3:
			if(cnt_dly_time >= cnt_dly_min)begin
               state <= 4;
               cnt_dly_time <= 28'd0;
            end
            else begin
               cnt_dly_time <= cnt_dly_time + 1'b1;
			   state <= 3;
            end
         4:
            begin
                if(data_num * 2 >= 16'd1472)begin
					pkt_length <= 16'd1472;
					state <= 1;
				end
				else if(data_num * 2 > 0)begin
					pkt_length <= data_num * 2;
					state <= 1;
				end
				else begin
					state <= 0;
				end
            end
          
          default:state <= 0;
         
    endcase
  end
    
endmodule
