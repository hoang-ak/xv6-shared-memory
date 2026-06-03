#include "kernel/types.h"
#include "user/user.h"

int
main()
{
  printf("\n===== WRITER =====\n");

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

  char msg[] = "Successfully shared memory";

  int i;

  for(i = 0; msg[i] != 0; i++){
    p[i] = msg[i];
  }

  p[i] = 0;

  printf("WRITE -> %s\n", p);

  // giữ process sống thật sự

  while(1);

  shmdt(p);

  printf("WRITER DETACHED\n");

  exit(0);
}
