#include <string.h>
#include "enc28j60.h"
#include "ip_arp_udp_tcp.h"
#include "net.h"
#include "simple_server.h"
#include "stm32f10x_flash.h"

#define BUFFER_SIZE 1500

extern unsigned long int iap_ip_address;
extern Flash_Data myip;
const unsigned char equip_id[10] = "csp850k-02";

uint32_t FlashDestination, flash_length; 
uint32_t RamSource;

void simple_server_start(void)
{
	static unsigned char iap_state = 0;
	static unsigned char buf[BUFFER_SIZE+1];
	unsigned int plen;
	unsigned int EraseCounter;
	
	union
	{
		unsigned long int data_32;
		unsigned char data_8[4];
	} package_size;

	//�ж��Ƿ��н��յ���Ч�İ�
	plen = enc28j60PacketReceive(BUFFER_SIZE, buf);
	//����յ���Ч�İ���plen��Ϊ��0ֵ��
	if (plen == 0)
	{
		//û���յ���Ч�İ����˳����¼��
		return;
	}
	//���յ�Ŀ�ĵ�ַΪ����IP��ARP��ʱ������ARP��Ӧ��
	if (eth_type_is_arp_and_my_ip(buf, plen))
	{
		make_arp_answer_from_request(buf);
		return;
	}

	//�ж��Ƿ���յ�Ŀ�ĵ�ַΪ����IP�ĺϷ���IP��
	if (eth_type_is_ip_and_my_ip(buf, plen) == 0)
	{
		return;
	}
	//����յ�ICMP��
	if ((buf[IP_PROTO_P] == IP_PROTO_ICMP_V) && (buf[ICMP_TYPE_P] == ICMP_TYPE_ECHOREQUEST_V))
	{
		// printf("\n\r�յ�����[%d.%d.%d.%d]���͵�ICMP��",buf[ETH_ARP_SRC_IP_P],buf[ETH_ARP_SRC_IP_P+1],
		//                                               buf[ETH_ARP_SRC_IP_P+2],buf[ETH_ARP_SRC_IP_P+3]);
		make_echo_reply_from_request(buf, plen);
		return;
	}

	//UDP��������888�˿ڵ�UDP��
	if ((buf[IP_PROTO_P] == IP_PROTO_UDP_V) && (buf[UDP_DST_PORT_H_P] == 0x03) && (buf[UDP_DST_PORT_L_P] == 0x78))
	{
		if (memcmp(buf+UDP_DATA_P, "rep", 3) == 0)
		{
			memcpy(buf+UDP_DATA_P, myip.data_8, 4);
			memcpy(buf+UDP_DATA_P+4, equip_id, 10);

			make_udp_reply_with_data(buf, 14, 888);
		}
	}
	else if ((buf[IP_PROTO_P] == IP_PROTO_UDP_V) && (buf[UDP_DST_PORT_H_P] == 0xff) && (buf[UDP_DST_PORT_L_P] == 0xee))
	{
		if (memcmp(buf+UDP_DATA_P, "opt", 3) == 0)
		{
			if (iap_state == 0x01)
			{
				iap_state = 0x00;
				memcpy(buf+UDP_DATA_P, "Too Big!!", 9);

				make_udp_reply_with_data(buf, 9, 65518);
			}
			else if (iap_state == 0x05)
			{
				iap_state = 0x00;
				memcpy(buf+UDP_DATA_P, "Not Data!", 9);

				make_udp_reply_with_data(buf, 9, 65518);
			}
			else if (iap_state == 0x12)
			{
				iap_state = 0x10;
				memcpy(buf+UDP_DATA_P, "Flash_Erase_ok", 14);

				make_udp_reply_with_data(buf, 14, 65518);
			}
			else if (iap_state == 0x13)
			{
				iap_state = 0x10;
				memcpy(buf+UDP_DATA_P, "Flash write ok", 14);

				make_udp_reply_with_data(buf, 14, 65518);
			}
			else if (iap_state ==0x14)
			{
				iap_state = 0x10;
				memcpy(buf+UDP_DATA_P, "Flash writeerr", 14);

				make_udp_reply_with_data(buf, 14, 65518);
			}
			else if (iap_state == 0x26)
			{
				FLASH_Status FLASHStatus;
				iap_state = 0x20;
				FLASHStatus = FLASH_ErasePage(iap_ip_address);
				if (FLASHStatus == FLASH_COMPLETE)
				{
					memcpy(buf+UDP_DATA_P, "I will jump ok", 14);

					make_udp_reply_with_data(buf, 14, 65518);
					
					NVIC_SystemReset();
				}
				else
				{
					memcpy(buf+UDP_DATA_P, "Flash writeerr", 14);

					make_udp_reply_with_data(buf, 14, 65518);
				}
			}
		}
		else if (memcmp(buf+UDP_DATA_P, "err", 3) == 0)
		{
			iap_state = 0x00;
		}
		else
		{
			unsigned int payloadlen;
			payloadlen = buf[UDP_LEN_H_P];
			payloadlen = payloadlen<<8;
			payloadlen = payloadlen + buf[UDP_LEN_L_P] - UDP_HEADER_LEN;

			make_udp_reply_with_data(buf, payloadlen, 65518);

			iap_state &= 0xf0;

			if (iap_state == 0x00)
			{
				if (memcmp(buf+UDP_DATA_P, "iap_start", 9) == 0)
				{
					memcpy(package_size.data_8, buf+UDP_DATA_P+9, 4);
					flash_length = package_size.data_32;
					if (package_size.data_32 > 128*1024)//51200
					{
						//too big
						iap_state = 0x01;
					}
					else
					{
						unsigned int NbrOfPage;
						FLASH_Status FLASHStatus = FLASH_COMPLETE;

						//������Ҫ����Flash��ҳ
						NbrOfPage = (package_size.data_32 + PAGE_SIZE - 1) / PAGE_SIZE;

						FlashDestination = ApplicationAddress;

						//����Flash
						for (EraseCounter = 0;
							(EraseCounter < NbrOfPage) && (FLASHStatus == FLASH_COMPLETE);
							EraseCounter++)
						{
							FLASHStatus = FLASH_ErasePage(ApplicationAddress + (PAGE_SIZE * EraseCounter));
						}

						//flash erase ok
						iap_state = 0x12;
					}
				}
			}
			else if (iap_state == 0x10)
			{
				if (memcmp(buf+UDP_DATA_P, "iap_data:", 9) == 0)
				{
					memcpy(package_size.data_8, buf+UDP_DATA_P+9, 4);
					//write flash ok;
					iap_state = 0x13;
					RamSource = (uint32_t)(buf + UDP_DATA_P + 13);

					{
						for (EraseCounter = 0; 
							EraseCounter < package_size.data_32 && FlashDestination <  ApplicationAddress + flash_length;
							EraseCounter+=4)
						{
							//�ѽ��յ������ݱ�д��Flash��
							FLASH_ProgramWord(FlashDestination, *(uint32_t*)RamSource);

							if (*(uint32_t*)FlashDestination != *(uint32_t*)RamSource)
							{
								//write flash err;
								iap_state = 0x14;
							}
							FlashDestination += 4;
							RamSource += 4;
						}
					}
				}
				else if (memcmp(buf+UDP_DATA_P, "iap_end", 7) == 0)
				{
					iap_state = 0x26;
				}
				else
				{
					//write flash ok;
					iap_state = 0x05;
				}
			}
			else
			{
				iap_state = 0x00;
			}
		}
	}
	return;
}