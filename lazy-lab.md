# Lazy-Lab

## 缺页异常

Xv6对于异常的处理非常简单: 
- 对于来自用户空间的异常，内核将对应进程杀死
- 对于来自内核空间的异常，内核只是`panic`

当CPU无法将某个虚拟地址翻译成物理地址时，那么CPU会产生一个**缺页异常(page-fault exception)**

RISC-V有三种缺页类型:

|        缺页类型         |          造成原因           |
| :---------------------: | :-------------------------: |
|    load page faults     | `load`指令无法翻译虚拟地址  |
|    store page faults    | `store`指令无法翻译虚拟地址 |
| instruction page faults |   某个指令的地址无法翻译    |

`scause`寄存器标明缺页异常的类型，`stval`寄存器包含了无法翻译的地址

现实中的操作系统对于缺页异常有着许多特性，使得操作系统变得更加效率

### copy-on-write(COW)

当子进程`fork`自父进程时，内核会调用`uvmcopy`为子进程分配物理内存然后将父进程的内存拷贝进去

一个更有效率的做法是COW，即写时拷贝，父子进程共享同一物理内存，它的核心思想为:

- 最开始父子进程共享所有物理页，但是这些页被映射为只读的
- 当父进程或者子进程执行一条`store`指令时，触发缺页异常
- 对异常做相应处理，内核造成异常的虚拟地址对应物理页拷贝一份，两份以读写权限分别映射到父子进程的地址空间
- 处理完缺页异常后，从造成缺页异常的地方继续执行，此时由于异常的虚拟地址是可写的，可以继续执行而不会导致缺页

### lazy allocation 

懒分配分为两个部分，它的核心思想在于内核只有在应用程序真正需要时才会分配物理内存

- 应用程序调用`sbrk`来增长地址空间，但是并不分配物理内存，只是将对应`PTE`置为无效
- 读写这些新增地址会导致缺页异常，内核此时才会真正分配物理内存，并建立页表映射

### paging from disk

RAM是有限的，如果一个应用程序需要的内存超过目前可用内存，那么内核必须将某些页换出到磁盘中，并且将相关页表条目置为无效。之后当应用程序读写这些页时，就会导致缺页异常。内核发现导致异常的页面在磁盘上，会分配一个物理页，将磁盘内容换入该物理页，并且更新相关页表条目为有效

## _Eliminate allocation from sbrk() (<font color=00ff00><u>easy.</u></font>)_

目前系统调用`sbrk(n)`，会增长进程的地址空间并且分配物理内存，该实验第一步是要修改`sys_sbrk`，让其只增长地址空间，但不分配物理内存，也就是不能调用`growproc`

```c
int
growproc(int n)
{
    uint sz;
    struct proc *p = myproc();

    sz = p->sz;
    if(n > 0){
        if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
            return -1;
        }
    } else if(n < 0){
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}
```

`growproc`中调用`uvmalloc`来增长地址空间，分配物理内存并建立页表映射;用`uvmdealloc`来减少地址空间，如果`p->sz`和`p->sz + n`不在同一页，`uvmdealloc`还会调用调用`uvmunmap`来收回物理内存以及取消映射

```c
uint64
sys_sbrk(void)
{
    int addr;
    int n;
	
    if(argint(0, &n) < 0)
    	return -1;
    struct proc *p = myproc();
  	addr = p->sz;
    
	// 修改地址空间大小
    p->sz += n;
    // 如果是减少地址空间，还要调用uvmdealloc来回收物理内存和取消页表映射
    if (n < 0)
        uvmdealloc(p->pagetable, addr, addr + n);
    
  	// if(growproc(n) < 0)
    //	return -1;
  	return addr;
}
```

启动xv6，输入`echo hi`，结果如下:

```sh
$ echo hi
usertrap(): unexpected scause 0x000000000000000f pid=4
            sepc=0x00000000000012ac stval=0x0000000000004008
panic: uvmunmap: not mapped
```

`usertrap`没有对缺页异常做特殊处理

## _Lazy allocation (<font color=0000ff><u>moderate.</u></font>)_

接下来对于缺页异常做相关处理(`scause`为13或15)，分配一个物理页，并将其映射到造成异常的虚拟地址

参考`kernel/vm.c`中的`uvmalloc`函数，在`kernel/trap.c`中实现`lazyalloc`函数来分配一个物理页并建立映射

```c
int lazyalloc(uint64 va) {
    struct proc *p = myproc();
	
    // 分配物理页，分配失败返回-1
    char *mem = kalloc();
    if (mem == 0) 
        return -1;

    // 为这个物理页建立映射关系
    memset(mem, 0, PGSIZE);
    if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0) {
        kfree(mem);
        return -1;
    }

    return 0;
}
```

在`usertrap`中对缺页中断做特殊处理

```c
void
usertrap(void)
{
    ...
  	else if(r_scause() == 13 || r_scause() == 15) {
		if (lazyalloc(r_stval()) < 0)
        	p->killed = 1;
    }
    ...
}
```

进程回收时，由于`uvmunmap`会取消整个地址空间映射，而有些地址并没有建立映射，会造成`panic`，这与`lazy allocation`矛盾，所以如果发现页表不存在或者无效，跳过该页即可

```c
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
    uint64 a;
    pte_t *pte;

    if((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
        // 跳过页表不存在或无效的页
        if((pte = walk(pagetable, a, 0)) == 0)
            continue;
        // panic("uvmunmap: walk");
        if((*pte & PTE_V) == 0)
            continue;
        // panic("uvmunmap: not mapped");
        if(PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        if(do_free){
            uint64 pa = PTE2PA(*pte);
            kfree((void*)pa);
        }
        *pte = 0;
    }
}
```

编译后再次执行`echo hi`，会发现之前`usertrap`中的错误打印已经消失，但是还是会`panic`

```sh
$ echo hi
panic: freewalk: leaf
```

经过调试得到`freewalk`前的页表如下:

```
page table 0x0000000087f75000
..0: pte 0x0000000021fdc801 pa 0x0000000087f72000
.. ..0: pte 0x0000000021fd9401 pa 0x0000000087f65000
.. .. ..20: pte 0x0000000021fd641f pa 0x0000000087f59000
..255: pte 0x0000000021fdd001 pa 0x0000000087f74000
.. ..511: pte 0x0000000021fdcc01 pa 0x0000000087f73000
```

导致`panic`的就是`.. .. ..20`这一条，对应虚拟地址为`0x14000`

经过长时间调试，发现问题在于`lazyalloc`中的`mappages`，原因是传入`va`没有下取整到对应页表起始地址，导致分配了两个物理页(可能有一个虚拟页地址大于等于`PGROUNDUP(p->sz)`但是被建立了映射)，最终没有被`uvmunmap`取消掉映射

```sh
# lazyalloc中执行了两次mappages，传入va参数如下
# 可以看到第二次导致了多映射了一个从虚拟地址0x14000开始的页面
va:0x0000000000004008
va:0x0000000000013f48
```

```c
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
    uint64 a, last;
    pte_t *pte;
    
    // 如果va不是PGSIZE整数倍，本来是要分配一个物理页，却导致分配了两个
    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);

    for(;;){
        if((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        if(*pte & PTE_V)
            panic("remap");
        *pte = PA2PTE(pa) | perm | PTE_V;
        if(a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}
```

修改`lazyalloc`

```c
int lazyalloc(uint64 va) {
      struct proc *p = myproc();

      if (va >= p->sz)
        return -1;

      if (va < p->trapframe->sp)
        return -1;

      char *mem = kalloc();
      if (mem == 0) 
        return -1;

      memset(mem, 0, PGSIZE);
      // 传入PGROUNDDOWN(va)而不是va
      if (mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0) {
        kfree(mem);
        return -1;
      }

      return 0;
}
```

编译再次执行`echo hi`，问题已解决

```sh
$ echo hi
hi
```

## _Lazytests and Usertests (<font color=0000ff><u>moderate.</u></font>)_

这个实验是主要是处理一些细节问题

### 处理sbrk()传递负参的问题

这个之前已经解决了，`n`为负时调用`uvmdealloc`来回收物理内存并取消映射

### 如果缺页异常是由一个高于sbrk()分配的地址导致的，将进程杀掉

只需要判断`stval`和`p->sz`的关系即可，在`lazyalloc`中添加判断

```c
int lazyalloc(uint64 va) {
    struct proc *p = myproc();
    
    // 判断导致缺页的地址是否高于p->sz
    if (va >= p->sz)
        return -1;
	
    // 分配物理页，分配失败返回-1
    char *mem = kalloc();
    if (mem == 0) 
        return -1;

    // 为这个物理页建立映射关系
    memset(mem, 0, PGSIZE);
    if (mappages(p->pagetable, va, PGSIZE, (uint64)mem, PTE_W|PTE_X|PTE_R|PTE_U) != 0) {
        kfree(mem);
        return -1;
    }

    return 0;
}
```

### 对fork时父子进程的内存拷贝做正确处理

`fork`中是调用了`uvmcopy`函数来拷贝父进程内存的

```c
int
fork(void) 
{
	...
    // 拷贝父进程内存
    if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
        freeproc(np);
        release(&np->lock);
        return -1;
    }
    ...
}
```

`fork`中的`uvmcopy`会拷贝父进程的整个地址空间，问题在于现在执行的是`lazy allocation`策略，地址空间是增加了，但并没有分配物理内存和建立映射，所以会造成`panic`，需要对`uvmcopy`做相应修改

```c
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
    pte_t *pte;
    uint64 pa, i;
    uint flags;
    char *mem;

    for(i = 0; i < sz; i += PGSIZE){
        // 如果pte不存在或者无效，跳过当前页而不是panic
        if((pte = walk(old, i, 0)) == 0) continue;
        //    panic("uvmcopy: pte should exist");
        if((*pte & PTE_V) == 0) continue;
        //    panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);
        if((mem = kalloc()) == 0)
            goto err;
        memmove(mem, (char*)pa, PGSIZE);
        if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
            kfree(mem);
            goto err;
        }
    }
    return 0;

    err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}
```

### 处理传递给read/write等系统调用一个合法地址，但是对应的物理内存尚未分配的问题

`sys_read`最终会调用`copyout`将内核若干字节拷贝到指定的虚拟地址，`sys_write`最终会调用`copyin`将给定虚拟地址的若干字节拷贝到内核中；而`copyin`和`copyout`最终都会调用`walkaddr`来完成虚拟地址到物理地址的翻译

问题和之前一样，这个虚拟地址是合法的，但是没有分配物理内存及建立映射，需要调用`lazyalloc`

在`kernel/defs.h`中添加`lazyalloc`声明

```c
int lazyalloc(uint64);
```

修改`walkaddr`，如果页表未分配或者无效，先调用`lazyalloc`来分配物理内存建立映射，再次调用`walk`来取得pte

```c
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
    pte_t *pte;
	uint64 pa;

    if(va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    // if(pte == 0)
    //   return 0;
    // if((*pte & PTE_V) == 0)
    //   return 0;

    // 处理页表未分配或者无效问题
    if (pte == 0 || (*pte & PTE_V) == 0) {
        if (lazyalloc(va) < 0)
            return 0;
        pte = walk(pagetable, va, 0);
    }

    if((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    return pa;
}
```

### 处理缺页异常时调用kalloc分配物理页失败，应当杀掉进程

这个之前在`lazyalloc`中已经解决了

```c
int lazyalloc(uint64 va) {
	...
	
    // 分配物理页，分配失败返回-1
    char *mem = kalloc();
    if (mem == 0) 
        return -1;
	...

    return 0;
}
```

### 处理访问用户栈外问题

用户栈是一页大小，从高地址向低地址增长，访问栈地址应当不低于栈顶`sp`，在`lazyalloc`添加判断

```c
int lazyalloc(uint64 va) {
    struct proc *p = myproc();

    ...
    
    // 访问栈时地址不能低于sp
	if (va < p->trapframe->sp)
    	return -1;

    ...
        
    return 0;
}
```

### 测试结果

执行`lazytests`，样例全部通过

```sh
$ lazytests
lazytests starting
running test lazy alloc
test lazy alloc: OK
running test lazy unmap
test lazy unmap: OK
running test out of memory
test out of memory: OK
ALL TESTS PASSED
```

执行`usertests`，样例全部通过

```sh
$ usertests
usertests starting
test execout: OK
test copyin: OK
test copyout: OK
test copyinstr1: OK
test copyinstr2: OK
test copyinstr3: OK
test rwsbrk: OK
test truncate1: OK
...
test forktest: OK
test bigdir: OK
ALL TESTS PASSED
```

`make grade`，结果如下

```
== Test running lazytests ==
$ make qemu-gdb
(15.4s)
== Test   lazy: map ==
  lazy: map: OK
== Test   lazy: unmap ==
  lazy: unmap: OK
== Test usertests ==
$ make qemu-gdb
Timeout! (300.2s)
== Test   usertests: pgbug ==
  usertests: pgbug: OK
== Test   usertests: sbrkbugs ==
  usertests: sbrkbugs: OK
== Test   usertests: argptest ==
  usertests: argptest: OK
== Test   usertests: sbrkmuch ==
  usertests: sbrkmuch: OK
== Test   usertests: sbrkfail ==
  usertests: sbrkfail: OK
== Test   usertests: sbrkarg ==
  usertests: sbrkarg: OK
== Test   usertests: stacktest ==
  usertests: stacktest: OK
== Test   usertests: execout ==
  usertests: execout: OK
== Test   usertests: copyin ==
  usertests: copyin: OK
== Test   usertests: copyout ==
  usertests: copyout: OK
== Test   usertests: copyinstr1 ==
  usertests: copyinstr1: OK
== Test   usertests: copyinstr2 ==
  usertests: copyinstr2: OK
== Test   usertests: copyinstr3 ==
  usertests: copyinstr3: OK
== Test   usertests: rwsbrk ==
  usertests: rwsbrk: OK
== Test   usertests: truncate1 ==
  usertests: truncate1: OK
== Test   usertests: truncate2 ==
  usertests: truncate2: OK
== Test   usertests: truncate3 ==
  usertests: truncate3: OK
== Test   usertests: reparent2 ==
  usertests: reparent2: OK
== Test   usertests: badarg ==
  usertests: badarg: OK
== Test   usertests: reparent ==
  usertests: reparent: OK
== Test   usertests: twochildren ==
  usertests: twochildren: OK
== Test   usertests: forkfork ==
  usertests: forkfork: OK
== Test   usertests: forkforkfork ==
  usertests: forkforkfork: OK
== Test   usertests: createdelete ==
  usertests: createdelete: OK
== Test   usertests: linkunlink ==
  usertests: linkunlink: OK
== Test   usertests: linktest ==
  usertests: linktest: OK
== Test   usertests: unlinkread ==
  usertests: unlinkread: OK
== Test   usertests: concreate ==
  usertests: concreate: OK
== Test   usertests: subdir ==
  usertests: subdir: OK
== Test   usertests: fourfiles ==
  usertests: fourfiles: OK
== Test   usertests: sharedfd ==
  usertests: sharedfd: OK
== Test   usertests: exectest ==
  usertests: exectest: OK
== Test   usertests: bigargtest ==
  usertests: bigargtest: OK
== Test   usertests: bigwrite ==
  usertests: bigwrite: OK
== Test   usertests: bsstest ==
  usertests: bsstest: OK
== Test   usertests: sbrkbasic ==
  usertests: sbrkbasic: OK
== Test   usertests: kernmem ==
  usertests: kernmem: OK
== Test   usertests: validatetest ==
  usertests: validatetest: OK
== Test   usertests: opentest ==
  usertests: opentest: OK
== Test   usertests: writetest ==
  usertests: writetest: OK
== Test   usertests: writebig ==
  usertests: writebig: OK
== Test   usertests: createtest ==
  usertests: createtest: OK
== Test   usertests: openiput ==
  usertests: openiput: OK
== Test   usertests: exitiput ==
  usertests: exitiput: OK
== Test   usertests: iput ==
  usertests: iput: OK
== Test   usertests: mem ==
  usertests: mem: OK
== Test   usertests: pipe1 ==
  usertests: pipe1: OK
== Test   usertests: preempt ==
  usertests: preempt: OK
== Test   usertests: exitwait ==
  usertests: exitwait: OK
== Test   usertests: rmdot ==
  usertests: rmdot: OK
== Test   usertests: fourteen ==
  usertests: fourteen: OK
== Test   usertests: bigfile ==
  usertests: bigfile: OK
== Test   usertests: dirfile ==
  usertests: dirfile: OK
== Test   usertests: iref ==
  usertests: iref: OK
== Test   usertests: forktest ==
  usertests: forktest: OK
== Test time ==
time: OK
Score: 119/119
```