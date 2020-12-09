#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <getopt.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <errno.h>
#include <error.h>
#include <string.h>
#include <net/if.h>

#include "client.h"
#include <proto.h>  
/*
 * -M --mgroup    指定多播组
 * -P --port      指定接受端口
 * -p --player    指定播放器 小写p
 * -H --help      显示帮助
 *
 * * */
struct client_conf_st  client_conf = {\
			.rcvport=DEFAULT_RCVPORT,\
			.mgroup=DEFAULT_MGROUP,\
			.player_cmd=DEFAULT_PLAYERCMD};

static void  printhelp(){
	printf("-P --port   指定接端口\n\
			-M --mgroup 指定多播组\n\
			-p --player 指定播放器\n\
			-H --help   显示帮助\n");
}

static ssize_t writen(int fd,const char * buf,size_t len){
	int ret;
	int pos=0;
	while(len>0){
		ret=write(fd,buf+pos,len);
		if(ret<0){
			if(errno==EINTR)
				continue;
			perror("write");
			return -1;
		}
		len-=ret;
		pos+=ret;
	}
return pos;
}

int main(int argc, char *argv[])
{
	/*
	 *初始化
	 *级别大小：默认值<配置文件<环境变量<命令行参数
	 */
	int index=0;
	int c;
	int sd;
	int on=1;
	pid_t pid;
	struct ip_mreqn mreq;
	struct sockaddr_in laddr,serveraddr,raddr;
	socklen_t serveraddr_len,raddr_len;
	int len,chosenid;
	int pd[2];
	struct option argarr[]={{"port",1,NULL,'P'},
		{"mgroup",1,NULL,'M'},
		{"player",1,NULL,'p'},
		{"help",1,NULL,'H'},
		{NULL,0,NULL,0}};//这个数组以全NULL结尾 类似于argv
	while(1){
		c=getopt_long(argc,argv,"P:M:p:H",argarr,&index);//c的值为的值
		if(c<0)
			break;
		switch(c){
			case 'P':
				client_conf.rcvport=optarg;//getopt_long的全局变量
				break;
			case 'M':
				client_conf.mgroup=optarg;
				break;
			case 'p':
				client_conf.player_cmd=optarg;
				break;
			case 'H':
				printhelp();
				exit(0);
				break;
			default:
				abort();
				break;
		}
	}

	sd=socket(AF_INET,SOCK_DGRAM,0);//udp
	if(sd<0){
		perror("socket");
		exit(1);
	}
	
	int recvbufsize=1;
	int rcvsize=sizeof(int);
	getsockopt(sd,SOL_SOCKET,SO_RCVBUF,&recvbufsize,&rcvsize);
	printf("recvbufsize: %dKB\n",recvbufsize/1024);
	
	recvbufsize=20*1024*1024;
	setsockopt(sd,SOL_SOCKET,SO_RCVBUF,&recvbufsize,sizeof(recvbufsize));

	getsockopt(sd,SOL_SOCKET,SO_RCVBUF,&recvbufsize,&rcvsize);
	printf("recvbufsize: %dKB\n",recvbufsize/1024);






	inet_pton(AF_INET,client_conf.mgroup,&mreq.imr_multiaddr);
	inet_pton(AF_INET,"0.0.0.0",&mreq.imr_address);
	mreq.imr_ifindex=if_nametoindex("enp5s0");

	//加入多播组
	if(setsockopt(sd,IPPROTO_IP,IP_ADD_MEMBERSHIP,&mreq,sizeof(mreq))<0){
		perror("setsockopt");
		exit(1);
	}

	if(setsockopt(sd,IPPROTO_IP,IP_MULTICAST_LOOP,&on,sizeof(on))<0){
		perror("setsockopt");
		exit(1);

	}


	laddr.sin_family=AF_INET;
	laddr.sin_port=htons(atoi(client_conf.rcvport));
	inet_pton(AF_INET,"0.0.0.0",&laddr.sin_addr);
	if(bind(sd,(void*)&laddr,sizeof(laddr))<0){//多播 必须绑定ip和port
		perror("bind");
		exit(1);

	}

	if(pipe(pd)<0){
		perror("pipe");
		exit(1);
	}

	//父进程：从网络上收包，发送给子进程
	//子进程：调用解码器解析
	pid=fork();
	if(pid<0){
		perror("fork");
		exit(1);
	}
	if(pid==0){
		close(sd);
		close(pd[1]);
		dup2(pd[0],0);//解码器只能从stdin读数据
		if(pd[0]>0)
			close(pd[0]);
		execl("/bin/sh","sh","-c",client_conf.player_cmd,NULL);
		perror("execl");
		exit(1);
	}
	//父进程 
	//收节目单
	struct msg_list_st * msg_list=NULL;
	msg_list=malloc(MSG_LIST_MAX);
	if(msg_list==NULL){
		perror("malloc");
		exit(1);
	}

	serveraddr_len=sizeof(serveraddr);
	while(1){
		len=recvfrom(sd,msg_list,MSG_LIST_MAX,0,(void*)&serveraddr,&serveraddr_len);
		if(len<sizeof(struct msg_list_st)){
			fprintf(stderr,"message is too small\n");
			continue;
		}
		if(msg_list->chnid!=LISTCHNID){//不是节目单的包
			fprintf(stderr,"chnid is not match\n");
			continue;
		}
		break;
	}
	//打印节目单并选择频道
	struct msg_listentry_st *pos;
	for(pos=msg_list->entry;(char *)pos<(((char*)msg_list)+len);pos=(void *)(((char *)pos)+ntohs(pos->len))){
		printf("channel: %d: %s\n",pos->chnid,pos->desc);
	}
	free(msg_list);


	int ret=0;
	puts("Please select chnid:");
	while(ret<1){//选择频道
		ret=scanf("%d",&chosenid);
		if(ret!=1)
			exit(1);
	}
	
	fprintf(stdout,"chosenid=%d\n",ret);

	//收频道包，发送给子进程
	struct msg_channel_st * msg_channel;
	msg_channel=malloc(MSG_CHANNEL_MAX);
	if(msg_channel==NULL){
		perror("malloc");
		exit(1);
	}

	raddr_len=sizeof(raddr);
	while(1){
		len=recvfrom(sd,msg_channel,MSG_CHANNEL_MAX,0,(void *)&raddr,&raddr_len);
		if(raddr.sin_addr.s_addr!=serveraddr.sin_addr.s_addr
		  ||raddr.sin_port!=serveraddr.sin_port){
			fprintf(stderr,"Ignore:address not match \n");
			continue;
		}
		if(len<sizeof(struct msg_channel_st)){
			fprintf(stderr,"Ignore:message too small\n");
			continue;
		}
		
		if(msg_channel->chnid==chosenid){
			fprintf(stdout,"accepted: msg:%d received\n",msg_channel->chnid);
			//传data给子进程
			ret=writen(pd[1],msg_channel->data,len-sizeof(chnid_t));
			if(ret<0)
				exit(1);
		}
	}
	
	free(msg_channel);
	close(sd);
	exit(0);
}
