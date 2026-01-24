#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#define BUFF_SIZE 10

typedef struct 
{
    int buf[BUFF_SIZE];
    int read;
    int write;
}TypeFifo;

int fifo_init(TypeFifo*fifo)
{
    fifo->read=0;
    fifo->write=0;
    return 0;
}

int fifo_push(TypeFifo*fifo,int pushvalue)
{
    if(((fifo->write+1)%BUFF_SIZE)==fifo->read)
    {
        return -1;
    }
    fifo->buf[fifo->write++]=pushvalue;
    if(fifo->write==BUFF_SIZE)fifo->write=0;
    return 0;
}


int fifo_pop(TypeFifo*fifo,int *popvalue)
{
    if(fifo->write==fifo->read)
    {
        return -1;  
    }
    *popvalue=fifo->buf[fifo->read++];
    if(fifo->read==BUFF_SIZE)fifo->read=0;
    return 0;
}

int fifo_getlen(TypeFifo fifo)
{
    return (fifo.write-fifo.read+BUFF_SIZE)%BUFF_SIZE;
}




//input fifo  2 3 4 5 6 8 9 10 1 22
int
main(int argc, char *argv[])
{
  int i;
  TypeFifo fifo;

  for(i = 1; i < argc; i++){
    fifo_push(&fifo,atoi(argv[i]));
  }
  int pop_val;
  fifo_pop(&fifo,&pop_val);
  printf("pop val is %d\n",pop_val);
  fifo_push(&fifo,10);
  fifo_push(&fifo,10);
  fifo_push(&fifo,10);
  printf("now fifo len=%d\n",fifo_getlen(fifo));
  int len=fifo_getlen(fifo);
  for(int i=0;i<len;i++)
  {
    fifo_pop(&fifo,&pop_val);
    printf("pop val is %d\n",pop_val);
  }
  printf("now fifo len=%d\n",fifo_getlen(fifo));
  
  exit(0);
}