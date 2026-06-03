#ifndef _SHM_H_
#define _SHM_H_

#include "types.h"

#define MAX_SHM 64
#define MAX_PAGES 16

struct shm_slot {
  int used;
  int key;
  uint64 pa[MAX_PAGES];  // nhiều page
  int num_pages;         // số page thực tế
  int ref_count;
};

struct shmtable_t {
  struct spinlock lock;
  struct shm_slot slots[MAX_SHM];
};

extern struct shmtable_t shmtable;

void shminit(void);

#endif
