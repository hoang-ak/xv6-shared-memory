#include "kernel/types.h"
#include "user/user.h"

int
main()
{
  printf("\n===== READER =====\n");

  int shmid = shmget(1, 4096);

  if(shmid < 0){
    printf("shmget failed\n");
    exit(1);
  }

  char *p = (char*)shmat(shmid);

  if((uint64)p == (uint64)-1){
    printf("shmat failed\n");
    exit(1);
  }

  printf("VA = %p\n", p);

  printf("READ BEFORE -> %s\n", p);

  // tìm cuối chuỗi
  int len = 0;

  while(p[len] != 0){
    len++;
  }

  // append " OK"
  p[len] = ' ';
  p[len+1] = 'O';
  p[len+2] = 'K';
  p[len+3] = 0;

  printf("READ AFTER  -> %s\n", p);
  
  shmdt(p);

  printf("READER DETACHED\n");

  exit(0);
}
