#include "stm32f10x.h"                  // Device header
#include "Delay.h"
#include "OLED.h"
#include "Serial.h"

uint8_t RxData;


  int main(void)
 {
	 
	 OLED_Init();
	 OLED_ShowString(1,1,"RxData:");
	 
	 Serial_Init();
	 
	
	
	 while(1)
	{
		//查询
		//if(USART_GetFlagStatus(USART1,USART_FLAG_RXNE)==SET)//USART_FLAG_RXNE接收非空标志 set标志为1 接收到数据
		//{
			//RxData=USART_ReceiveData(USART1);//接受的字节在RxData里
			//OLED_ShowHexNum(1,1,RxData,2);
		//}
		
		if(Serial_GetRxFlag()==1)
		{
			RxData=Serial_GetRxData();
			Serial_SendByte(RxData);//数据回传
			OLED_ShowHexNum(1,1,RxData,2);
			
		}
		
		
	}
	 
		 

 }
