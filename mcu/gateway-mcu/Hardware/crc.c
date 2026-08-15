#include "crc.h"

unsigned char Calc_CRC8(unsigned char *message,unsigned char Num)
{
	unsigned char i;
	unsigned char byte;
	unsigned char crc =0xFF;
	for (byte = 0;byte<Num;byte++)
	{
		crc^=(message[byte]);
		for(i=8;i>0;--i)
		{
		if(crc&0x80)
		crc=(crc<<1)^0x31;
		else
		crc=(crc<<1);
		}
	}
	return crc;
}