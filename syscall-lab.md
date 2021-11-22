# Syscall-Lab

[Lab地址](https://pdos.csail.mit.edu/6.828/2020/labs/syscall.html)

## 相关知识

### 系统调用流程

比如用户想要通过调用`fork()`向内核申请创建一个子进程，调用过程如下: \
**fork() -> ecall SYS_fork -> syscall -> sys_fork()**

`fork()`是提供给用户程序的接口，执行`fork()`对应汇编下`ecall SYS_fork`，其中`SYS_fork`为系统调用号，定义在`kernel/syscall.h`中，然后处理器由用户模式转变为内核模式，控制转移给`syscall`，`syscall`从`a7`寄存器中取出系统调用号，通过索引找到`fork()`的内核实现`sys_fork()`并执行。

### 系统调用传递参数

比如`sleep(int n)`，用户传输参数`n`来指定睡眠时间。用户参数是通过寄存器传递给内核的，`sys_sleep()`通过调用`argint(0, &n)`将寄存器`a0`的值赋给`n`。

用户也可能传递指针参数，此时内核不能直接读取指针对应用户地址，原因有二：1.用户程序很可能是恶意的，可能传递给内核一个无效指针，或者地址不在用户程序自己的地址空间甚至对应内核地址空间。2.内核页表映射关系不同于用户页表映射，当然不能直接读取用户地址。

将用户栈特定地址n字节拷贝到内核栈来实现指针的传递，该操作需要当前进程`p`的页表`p->pagetable`以及用户地址`srcva`，内核检查`srcva`是否合法，然后使用`p->pagetable`将`srcva`翻译成物理地址`pa0`，然后将`pa0`开始的`n`字节拷贝到内核(内核页表虚拟地址和物理地址一一映射)。

当然内核也可能需要将结果存储到用户地址处，原理和上面类似。

### 系统调用返回参数

例如`sys_fork()`会返回一个值给用户程序，将结果存储到当前进程`p`的`p->trapframe->a0`即可。

### 本次Lab相关文件

|       File       |        Description         |
| :--------------: | :------------------------: |
|   user/user.h    |        系统调用接口        |
|   user/usys.pl   | 生成usys.S, 系统调用入口点 |
|  kernel/defs.h   |         模块间接口         |
| kernel/sysfile.c |      文件相关系统调用      |
|  kernel/proc.c   |         进程及调度         |
| kernel/sysproc.c |    进程相关系统调用实现    |
| kernel/syscall.h |      系统调用号宏定义      |
| kernel/syscall.c | 派遣系统调用到对应内核实现 |
| kernel/kalloc.c  |        物理内存分配        |

## _System call tracing<font color="0000ff">(Moderate)</font>_

目的：添加系统调用`trace(int mask)`，`mask`第`i`为`1`表明当前进程及其子进程要跟踪系统调用号为`i`的系统调用并打印输出

例如:
```sh
# 32对应第5位为1，SYS_read=5，表明要跟踪read调用
$ trace 32 grep hello README
# 打印输入形似：进程号 syscall 系统调用名 -> 系统调用返回值
3: syscall read -> 1023
3: syscall read -> 966
3: syscall read -> 70
3: syscall read -> 0
```

1. `Makefile`添加`$U_trace`到`UPROGS`
2. `user/user.h`声明系统调用接口`int trace(int)`，`usys.pl`下添加`entry("trace")`，编译后`usys.S`下多出:

    ```
    trace:
    li a7, SYS_trace    // 系统调用号记录在a7寄存器中
    ecall               // ecall->syscall
    ret
    ```

3. `kernel/syscall.h`添加宏定义`#define SYS_trace 22`
4. `kernel/proc.h`中用结构体`proc`记录了每个进程的信息，在结构体中添加成员`int tracemask`记录当前进程跟踪系统调用的掩码
5. 子进程需要拷贝父进程`mask`，在`kernel/proc.c`添加`np->tracemask = p->tracemask`
6. `kernel/sysproc.c`下实现`sys_trace`
   
    ```C
    uint64
    sys_trace(void)
    {
        int mask;
        if(argint(0, &mask) < 0) // 只有一个参数mask，从a0寄存器读取
            return -1;
        myproc()->tracemask = mask; // myproc返回当前进程PCB结构体指针，将mask保存在PCB中
        return 0;
    }
    ```

7. `kernel/syscall.c`文件中，加上`sys_trace`函数声明，在`syscalls`数组中添加`sys_trace`，另外由于打印需要知道系统调用名，创建一个数组`syscall_names`用来存储系统调用号到名称的映射，修改`syscall`打印跟踪信息
   
    ```C
    extern uint64 sys_chdir(void);
    extern uint64 sys_close(void);
    extern uint64 sys_dup(void);
    extern uint64 sys_exec(void);
    extern uint64 sys_exit(void);
    extern uint64 sys_fork(void);
    extern uint64 sys_fstat(void);
    extern uint64 sys_getpid(void);
    extern uint64 sys_kill(void);
    extern uint64 sys_link(void);
    extern uint64 sys_mkdir(void);
    extern uint64 sys_mknod(void);
    extern uint64 sys_open(void);
    extern uint64 sys_pipe(void);
    extern uint64 sys_read(void);
    extern uint64 sys_sbrk(void);
    extern uint64 sys_sleep(void);
    extern uint64 sys_unlink(void);
    extern uint64 sys_wait(void);
    extern uint64 sys_write(void);
    extern uint64 sys_uptime(void);
    extern uint64 sys_trace(void);

    // syscalls是函数指针数组，下标为系统调用号，元素记录对应调用号的函数入口
    static uint64 (*syscalls[])(void) = {
        [SYS_fork]    sys_fork, // [Index] Element这种写法可以初始化指定下标元素，不要求下标顺序
        [SYS_exit]    sys_exit,
        [SYS_wait]    sys_wait,
        [SYS_pipe]    sys_pipe,
        [SYS_read]    sys_read,
        [SYS_kill]    sys_kill,
        [SYS_exec]    sys_exec,
        [SYS_fstat]   sys_fstat,
        [SYS_chdir]   sys_chdir,
        [SYS_dup]     sys_dup,
        [SYS_getpid]  sys_getpid,
        [SYS_sbrk]    sys_sbrk,
        [SYS_sleep]   sys_sleep,
        [SYS_uptime]  sys_uptime,
        [SYS_open]    sys_open,
        [SYS_write]   sys_write,
        [SYS_mknod]   sys_mknod,
        [SYS_unlink]  sys_unlink,
        [SYS_link]    sys_link,
        [SYS_mkdir]   sys_mkdir,
        [SYS_close]   sys_close,
        [SYS_trace]   sys_trace,
    };

    static char* syscall_names[] = {
        [SYS_fork]    "fork",
        [SYS_exit]    "exit",
        [SYS_wait]    "wait",
        [SYS_pipe]    "pipe",
        [SYS_read]    "read",
        [SYS_kill]    "kill",
        [SYS_exec]    "exec",
        [SYS_fstat]   "fstat",
        [SYS_chdir]   "chdir",
        [SYS_dup]     "dup",
        [SYS_getpid]  "getpid",
        [SYS_sbrk]    "sbrk",
        [SYS_sleep]   "sleep",
        [SYS_uptime]  "uptime",
        [SYS_open]    "open",
        [SYS_write]   "write",
        [SYS_mknod]   "mknod",
        [SYS_unlink]  "unlink",
        [SYS_link]    "link",
        [SYS_mkdir]   "mkdir",
        [SYS_close]   "close",
        [SYS_trace]   "trace",
    };

    void
    syscall(void)
    {
        int num;
        struct proc *p = myproc(); // 当前进程PCB

        num = p->trapframe->a7; // 系统调用号记录在a7寄存器中
        if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
            p->trapframe->a0 = syscalls[num](); // 调用真正的实现函数，将结果保存在a0寄存器中
            if((p->tracemask) & (1 << num)) // 判断当前系统调用号对应mask位是否为1
                printf("%d: syscall %s -> %d\n", p->pid, syscall_names[num], p->trapframe->a0);
        } else {
            printf("%d %s: unknown sys call %d\n",
                    p->pid, p->name, num);
            p->trapframe->a0 = -1;
        }
    }
    ```



## _Sysinfo<font color="0000ff">(Moderate)</font>_

目的：添加系统调用`sysinfo(struct sysinfo *)`，`sysinfo`结构体记录当前操作系统的剩余内存以及进程数，该系统调用将结果保存在用户传入的指针中

1. `Makefile`添加`$U_sysinfotest`到`UPROGS`
2. `user/user.h`声明系统调用接口`int sysinfo(struct sysinfo*)`，注意需要前置声明`struct sysinfo`，`usys.pl`下添加`entry("sysinfo")`
3. `kernel/syscall.h`添加宏定义`#define SYS_sysinfo 23`，注意这里的名称是与用户接口相关的，见`user/usys.pl`：
    ```pl
    sub entry {
        my $name = shift;
        print ".global $name\n";
        print "${name}:\n";
        print " li a7, SYS_${name}\n";  # name为系统调用接口名，SYS_${name}为系统调用号
        print " ecall\n";
        print " ret\n";
    }
    ```
4. `kernel/proc.c`下实现`countprocess`用来计算当前用户进程数
    ```C
    // 猜测xv6进程数上限为NPROC，用proc数组来记录每个进程信息
    struct proc proc[NPROC];

    // free a proc structure and the data hanging from it,
    // including user pages.
    // p->lock must be held.
    static void
    freeproc(struct proc *p)
    {
        if(p->trapframe)
            kfree((void*)p->trapframe);
        p->trapframe = 0;
        if(p->pagetable)
            proc_freepagetable(p->pagetable, p->sz);
        p->pagetable = 0;
        p->sz = 0;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->chan = 0;
        p->killed = 0;
        p->xstate = 0;
        p->state = UNUSED;  // 进程资源释放时，进程状态为UNUSED，根据实验指导书，只需要计数不是UNUSED状态进程即可
    }


    // Print a process listing to console.  For debugging.
    // Runs when user types ^P on console.
    // No lock to avoid wedging a stuck machine further.
    void
    procdump(void)
    {
        static char *states[] = {
        [UNUSED]    "unused",
        [SLEEPING]  "sleep ",
        [RUNNABLE]  "runble",
        [RUNNING]   "run   ",
        [ZOMBIE]    "zombie"
        };
        struct proc *p;
        char *state;

        printf("\n");
        // 参照这里，来遍历进程数组
        for(p = proc; p < &proc[NPROC]; p++){
            if(p->state == UNUSED)
            continue;
            if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
            else
            state = "???";
            printf("%d %s %s", p->pid, state, p->name);
            printf("\n");
        }
    }

    int countprocess(void) {
        int pcount = 0;
        struct proc *p;
        for(p = proc; p < &proc[NPROC]; p++) {
            // 遍历proc数组，不是UNUSED状态，计数加1
            if(p->state == UNUSED)
                continue;
            ++pcount;
        }
        return pcount;
    }
    ```

5. `kernel/kalloc.c`下实现`countfree`来计算当前空闲内存
   
    ```C
    struct run {
        struct run *next;
    };

    struct {
        struct spinlock lock;
        struct run *freelist;
    } kmem;

    // Free the page of physical memory pointed at by v,
    // which normally should have been returned by a
    // call to kalloc().  (The exception is when
    // initializing the allocator; see kinit above.)
    void
    kfree(void *pa)
    {
        struct run *r;

        // 要求pa是页大小整数倍，pa是页基址，end~PHYSTOP是内核可用物理地址
        if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
            panic("kfree");

        // Fill with junk to catch dangling refs.
        memset(pa, 1, PGSIZE);

        r = (struct run*)pa;

        acquire(&kmem.lock);
        // 经典链表操作，头部添加元素然后链表头指向添加元素，kmem.freelist为空闲内存链表，每个元素对应一个空闲页
        r->next = kmem.freelist;
        kmem.freelist = r;
        release(&kmem.lock);
    }

    // Allocate one 4096-byte page of physical memory.
    // Returns a pointer that the kernel can use.
    // Returns 0 if the memory cannot be allocated.
    void *
    kalloc(void)
    {
        struct run *r;

        acquire(&kmem.lock);
        // 删除头部元素，链表指向下一个元素，对应分配一页
        r = kmem.freelist;
        if(r)
            kmem.freelist = r->next;
        release(&kmem.lock);

        if(r)
            memset((char*)r, 5, PGSIZE); // fill with junk
        return (void*)r;
    }

    // 参照上方，基本思想为遍历kmem.freelist，每个元素对应一个空闲页
    int countfree(void) {
        int mfree = 0;
        struct run *p = kmem.freelist;
        while(p) {
            mfree += PGSIZE;
            p = p->next;
        }
        return mfree;
    }
    ```
6. `kernel/defs.h`中添加刚才两个函数的声明
7. `kernel/sysproc.c`下实现`sys_sysinfo`, 参照`kernel/file.c`下的`filestat`和`kernel/sysfile.c`的`sys_fstat`将内核栈数据拷贝到用户栈
    ```C
    // Get metadata about file f.
    // addr is a user virtual address, pointing to a struct stat.
    int
    filestat(struct file *f, uint64 addr)
    {
        struct proc *p = myproc();
        struct stat st;
        
        if(f->type == FD_INODE || f->type == FD_DEVICE){
            ilock(f->ip);
            stati(f->ip, &st);
            iunlock(f->ip);
            // copyout(当前进程页表，用户虚拟地址，内核栈指针，拷贝数据大小)
            if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
                return -1;
            return 0;
        }
        return -1;
    }

    uint64
    sys_fstat(void)
    {
        struct file *f;
        uint64 st; // user pointer to struct stat

        // 系统调用fstat接受两个参数，第二个参数为stat结构体指针对应寄存器a1, 用argaddr(1, &st)提取
        if(argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
            return -1;
        return filestat(f, st);
    }

    uint64
    sys_sysinfo(void)
    {
        struct proc *p = myproc(); // 当前进程PCB指针为p
        struct sysinfo sinfo;   // 要拷贝到用户栈的sysinfo信息
        uint64 addr;            // sysinfo系统调用传入的用户虚拟地址 

        // 只有一个参数，去a0寄存器取出地址传给addr
        if(argaddr(0, &addr) < 0)
            return -1;
        
        // 调用刚才写的两个函数，填充sinfo
        sinfo.freemem = countfree();
        sinfo.nproc = countprocess();

        // 用copyout将sinfo拷贝到addr即用户传入的指针处
        if(copyout(p->pagetable, addr, (char*)&sinfo, sizeof(sinfo)) < 0)
            return -1;
        return 0;
    }
    ```
8. `kernel/syscall.c`文件中，加上`sys_sysinfo`函数声明，在`syscalls`数组中添加`sys_sysinfo`，在`syscall_names`数组中添加系统调用名`sysinfo`