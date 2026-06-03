#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "shm.h"

struct shmtable_t shmtable;

void
shminit(void)
{
  initlock(&shmtable.lock, "shm");

  for(int i = 0; i < MAX_SHM; i++){
    shmtable.slots[i].used = 0;
    shmtable.slots[i].ref_count = 0;
    shmtable.slots[i].num_pages = 0;
    memset(shmtable.slots[i].pa, 0, sizeof(shmtable.slots[i].pa));
  }
}

uint64
sys_shmget(void)
{
  int key, size;
  int index = -1;

  // 1. Lấy tham số từ user
  argint(0, &key);
  argint(1, &size);
  if(key < 0 || size <= 0)
    return -1;

  // 2. Tính số page cần
  int num_pages = (size + PGSIZE - 1) / PGSIZE;

  if(num_pages > MAX_PAGES)
    return -1;

  acquire(&shmtable.lock);

  // 3. Tìm key đã tồn tại
  for(int i = 0; i < MAX_SHM; i++){
    if(shmtable.slots[i].used && shmtable.slots[i].key == key){

      // Nếu size khác → reject
      if(shmtable.slots[i].num_pages != num_pages){
        index = -1;
      } else {
        index = i;
      }

      goto out;
    }
  }

  // 4. Tìm slot trống để cấp phát mới
  for(int i = 0; i < MAX_SHM; i++){
    if(!shmtable.slots[i].used){

      // reset pa[] để tránh rác cũ
      memset(shmtable.slots[i].pa, 0, sizeof(shmtable.slots[i].pa));

      int j;
      for(j = 0; j < num_pages; j++){
        char *mem = kalloc();
        if(mem == 0){
          // rollback nếu cấp phát lỗi
          for(int k = 0; k < j; k++){
            kfree((void*)shmtable.slots[i].pa[k]);
          }

          shmtable.slots[i].num_pages = 0;
          index = -1;
          goto out;
        }

        memset(mem, 0, PGSIZE);
        shmtable.slots[i].pa[j] = (uint64)mem;
      }

      // khởi tạo slot
      shmtable.slots[i].used = 1;
      shmtable.slots[i].key = key;
      shmtable.slots[i].num_pages = num_pages;
      shmtable.slots[i].ref_count = 0;

      index = i;
      goto out;
    }
  }

  // 5. Không còn slot trống
  index = -1;

out:
  release(&shmtable.lock);
  return index;
}

uint64
sys_shmat(void)
{
  int shmid;

  // lấy shmid từ user
  argint(0, &shmid);

  // kiểm tra range
  if(shmid < 0 || shmid >= MAX_SHM)
    return -1;

  acquire(&shmtable.lock);

  // kiểm tra slot hợp lệ
  if(!shmtable.slots[shmid].used){
    release(&shmtable.lock);
    return -1;
  }

  struct proc *p = myproc();

  int num_pages = shmtable.slots[shmid].num_pages;

  // lấy VA bắt đầu map
  uint64 va = PGROUNDUP(p->sz);

  // kiểm tra vượt giới hạn VA
  uint64 next_sz = va + num_pages * PGSIZE;

  if(next_sz >= MAXVA){
    release(&shmtable.lock);
    return -1;
  }

  // map từng physical page
  for(int i = 0; i < num_pages; i++){

    uint64 pa = shmtable.slots[shmid].pa[i];

    if(mappages(
        p->pagetable,
        va + i * PGSIZE,
        PGSIZE,
        pa,
        PTE_R | PTE_W | PTE_U
      ) != 0){

      // rollback các page đã map trước đó
      uvmunmap(
          p->pagetable,
          va / PGSIZE,
          i,
          0
      );

      release(&shmtable.lock);
      return -1;
    }

    // DEBUG PRINT
    printf(
      "[KERNEL] pid=%d map VA=%p -> PA=%p\n",
      p->pid,
      (void *)(va + i * PGSIZE),
      (void *)pa
    );
  }

  // cập nhật size process
  p->sz = next_sz;
  // attach count
  shmtable.slots[shmid].ref_count++;
  printf("[SHM] pid=%d attach shmid=%d ref=%d\n",p->pid,shmid,shmtable.slots[shmid].ref_count);

  // lưu metadata vào proc
  for(int i = 0; i < MAX_ATTACH; i++){

    if(!p->shm[i].used){
      p->shm[i].used = 1;
      p->shm[i].shmid = shmid;
      p->shm[i].va = va;
      break;
    }
  }

  release(&shmtable.lock);

  // trả về virtual address cho user
  return va;
}

uint64
sys_shmdt(void)
{
  uint64 va;

  argaddr(0, &va);

  shm_detach(myproc(), va);

  return 0;
}

void
shm_detach(struct proc *p, uint64 va)
{
  acquire(&shmtable.lock);

  for(int i = 0; i < MAX_ATTACH; i++){

    if(!p->shm[i].used)
      continue;

    if(p->shm[i].va != va)
      continue;

    int id = p->shm[i].shmid;

    if(id < 0 || id >= MAX_SHM)
      break;

    struct shm_slot *s = &shmtable.slots[id];

    // unmap shared pages
    uvmunmap(
      p->pagetable,
      va,
      s->num_pages,
      0
    );

    // giảm ref
    s->ref_count--;

    printf(
      "[SHM] pid=%d detach shmid=%d ref=%d\n",
      p->pid,
      id,
      s->ref_count
    );

    // clear proc metadata
    memset(&p->shm[i], 0, sizeof(p->shm[i]));

    // không còn ai dùng
    if(s->ref_count == 0){

      for(int j = 0; j < s->num_pages; j++){
        kfree((void*)s->pa[j]);
      }

      memset(s, 0, sizeof(*s));

      printf("[SHM] free shared pages\n");
    }

    break;
  }

  release(&shmtable.lock);
}
void
shm_cleanup_proc(struct proc *p)
{
  for(int i = 0; i < MAX_ATTACH; i++){

    if(p->shm[i].used){
      uint64 va = p->shm[i].va;
      shm_detach(p, va);
    }
  }
}
