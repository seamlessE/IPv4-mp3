#ifndef PROTO_H__
#define PROTO_H__
#include<site_type.h>

#define DEFAULT_MGROUP  "224.2.2.2"
#define DEFAULT_RCVPORT  "1989"

#define CHNNR   100//100 channel 1 - 100

#define LISTCHNID         0//0 作为特殊的频道 节目单
#define MINCHNID		  1
#define MAXCHNID       (MINCHNID+CHNNR-1)

#define MSG_CHANNEL_MAX  (65535-20-8)//ip:20 byte udp:8 byte (headr)
#define MAX_DATA         (MSG_CHANNEL_MAX-sizeof(chnid_t))

#define MSG_LIST_MAX    (65535-20-8)
#define MAX_ENTRY       (MAG_LIST_MAX-sizeof(chnid_t))

struct msg_channel_st{//频道信息
	chnid_t chnid;		
	uint8_t data[1];
}__attribute__((packed));//不需要对齐

/*
1 music:XXXXXXX
2 sport:XXXXXXXXX
3 xxxx:XXXXXX 
*/

struct msg_listentry_st{
	chnid_t chnid;//频道号
	uint16_t len;//这个结构体的大小
	uint8_t desc[1];//频道描述
}__attribute__((packed));



struct msg_list_st
{/*节目单*/
	chnid_t chnid;  //must be LISTCHNID
	struct msg_listentry_st entry[1];
}__attribute__((packed));

#endif
