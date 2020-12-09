#include<stdio.h>
#include<stdlib.h>
#include"mytbf.h"
#include<unistd.h>
#include<fcntl.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<pthread.h>
#include<errno.h>
#include<error.h>
#include<syslog.h>
#include<string.h>



struct mytbf_st{
	int cps;
	int burst;
	int token;
	int pos;
	pthread_mutex_t mut;
	pthread_cond_t cond;
};

static struct mytbf_st* job[MYTBF_MAX];
static pthread_mutex_t mut_job=PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t init_once=PTHREAD_ONCE_INIT;
pthread_t tid;

static void * thr_alrm(void * p){
	while(1){
		pthread_mutex_lock(&mut_job);
		for(int i=0;i<MYTBF_MAX;i++){
			if(job[i]!=NULL){
				pthread_mutex_lock(&job[i]->mut);

				job[i]->token+=job[i]->cps;
				if(job[i]->token>job[i]->burst)
					job[i]->token=job[i]->burst;

				pthread_cond_broadcast(&job[i]->cond);							    pthread_mutex_unlock(&job[i]->mut);
			}
		}
		pthread_mutex_unlock(&mut_job);
		sleep(1);
	}

}
static void module_unload(){//钩子函数 所以在程序结束时才执行
	pthread_cancel(tid);
	pthread_join(tid,NULL);
	int i;
	pthread_mutex_lock(&mut_job);//所以也可不加锁因为程序已经退出了
	for(i=0;i<MYTBF_MAX;i++){
		if(job[i]!=NULL)
			mytbf_destroy(job[i]);
	}
	pthread_mutex_unlock(&mut_job);
	pthread_mutex_destroy(&mut_job);

}
static void module_load(){
	int err;
	err=pthread_create(&tid,NULL,thr_alrm,NULL);
	if(err){
		//fprintf(stderr,"pthread_create():%s\n",strerror(errno));
		syslog(LOG_ERR,"pthread_create() failed!");
		exit(1);
	}
	atexit(module_unload);//钩子函数
}
static int get_free_pos_unlocked(){
	for(int i=0;i<MYTBF_MAX;i++){
		if(job[i]==NULL)
			return i;
	}
	return -1;
}
mytbf_t *mytbf_init(int cps, int burst){

	pthread_once(&init_once, module_load);

	struct mytbf_st *me;
	int pos;
	me=malloc(sizeof(*me));
	if(me==NULL)
		return NULL;
	me->cps=cps;
	me->burst=burst;
	me->token=0; 
	pthread_mutex_init(&me->mut,NULL);
	pthread_cond_init(&me->cond,NULL);

	pthread_mutex_lock(&mut_job);//
	pos=get_free_pos_unlocked();//该函数访问job数组 所以先加锁
	if(pos<0){
		pthread_mutex_unlock(&mut_job);//出问题也得先解锁
		free(me);//
		return NULL;
	}
	me->pos=pos;
	job[pos]=me;
	pthread_mutex_unlock(&mut_job);

	return me;

}

int mytbf_fetchtoken(mytbf_t *ptr, int size){
	struct mytbf_st *me=ptr;
	int n=0;
	pthread_mutex_lock(&me->mut);

	while(me->token==0){
		pthread_cond_wait(&me->cond,&me->mut);
	}
	n=(me->token>=size)?size:me->token;
	me->token-=n;

	pthread_mutex_unlock(&me->mut);
	return n;

}

int mytbf_returntoken(mytbf_t *ptr, int size ){	
	struct mytbf_st *me=ptr;
	pthread_mutex_lock(&me->mut);
	me->token+=size;
	if(me->token>me->burst)
		me->token=me->burst;
	pthread_cond_broadcast(&me->cond);
	pthread_mutex_unlock(&me->mut);

	return 0;


}

int mytbf_destroy(mytbf_t *ptr){
	struct mytbf_st *me=ptr;

	pthread_mutex_lock(&mut_job);
	job[me->pos]=NULL;
	pthread_mutex_unlock(&mut_job);

	pthread_mutex_destroy(&me->mut);
	pthread_cond_destroy(&me->cond);
	free(me);

	return 0;

}

