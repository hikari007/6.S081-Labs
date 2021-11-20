# Util-Lab

[Lab地址](https://pdos.csail.mit.edu/6.828/2020/labs/util.html)

## _sleep(<font color="#00ff00">Easy</font>)_

没啥好说的，判断用法是否正确，直接调用系统调用`sleep`即可

```C
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if(argc != 2) {
        fprintf(2, "Usage: sleep NUMBER\n");
        exit(1);
    }
    
    int n = atoi(argv[1]);
    sleep(n);

    exit(0);
}
```

## _pingpong(<font color="00ff00">Easy</font>)_

父子进程利用管道传输数据，注意匿名管道是**单向传输**，所以要创建两个管道

```C
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    if(argc != 1) {
        fprintf(2, "Usage: pingpong\n");
        exit(1);
    }

    int fds1[2];
    int fds2[2];
    char read_buffer[64];
    char write_buffer[64];

    if(pipe(fds1) < 0 || pipe(fds2) < 0) {
        fprintf(2, "pipe failed\n");
        exit(1);
    }

    int pid = fork();
    if(pid < 0) {
        fprintf(2, "fork() failed\n");
        exit(1);
    }
    
    // 父进程
    if(pid > 0) {
        // 关闭管道1读端，管道2写端
        close(fds1[0]);
        close(fds2[1]);
        int p = getpid();
        // 向管道1写一个字节
        write(fds1[1], write_buffer, 1);
        // 从管道2读一个字节
        read(fds2[0], read_buffer, 1);
        fprintf(1, "%d: received pong\n", p);
    } 
    // 子进程
    else {
        // 关闭管道1写端，管道2读端
        close(fds1[1]);
        close(fds2[0]);
        int p = getpid();
        // 从管道1读一个字节
        read(fds1[0], read_buffer, 1);
        // 向管道2写一个字节
        fprintf(1, "%d: received ping\n", p);
        write(fds2[1], write_buffer, 1);
    }
    
    exit(0);
}
```

## _primes(<font color="0000ff">Moderate</font>)/(<font color=ff0000>Hard</font>)_

对于每一个子进程，它有`in`和`out`两个管道，从`in`管道接收父进程结果，经过欧拉筛，将筛选结果通过`out`管道传输给子进程

要点在于如何递归创建子进程，要使用递归函数

另外要注意关闭不需要的描述符，对于管道来说，如果写端引用为0，读端读取完管道中数据后，再次`read`会返回0。而如果管道中已经没有数据，且写段引用不为0，那么`read`将导致进程阻塞

```C
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

// 对于每个子进程，in为从父进程接收数据的管道
void childprocess(int in[]) {
    // 关闭in写端
    close(in[1]);
    int base, num;
    // 接收第一个数字(质数)，并打印
    if(read(in[0], &base, sizeof(int)) == 0) {
        close(in[0]);
        exit(0);
    }
    fprintf(1, "prime %d\n", base);

    // 创建另一个管道，用于和子进程通信
    int out[2];
    pipe(out);

    // 当前进程
    if(fork() > 0) {
        // 不需要out读端
        close(out[0]);
        // 欧拉筛，将筛选结果通过out发送给子进程
        while(read(in[0], &num, sizeof(int))) {
            if(num % base)
                write(out[1], &num, sizeof(int));
        }
        // 传输完毕，关闭in读端，out写段
        close(in[0]);
        close(out[1]);
        wait(0);
    }
    // 子进程
    else {
        // 不需要in读端
        close(in[0]);
        // 对于子进程来说，out就是它的in
        childprocess(out);
    }
    exit(0);
}

int main(int argc, char *argv[]) {
    if(argc != 1) {
        fprintf(2, "Usage: primes\n");
        exit(1);
    }

    int p[2];
    pipe(p);

    if(fork() > 0) {
        close(p[0]);
        int i;
        for(i = 2; i <= 35; ++i)
            write(p[1], &i, sizeof(int));
        close(p[1]);
        wait(0);
    }
    else 
        childprocess(p);
    
    exit(0);
}
```

## _find(<font color="0000ff">Moderate</font>)_

仿照`ls`读取目录，忽略`.`和`..`目录，对其他子目录进行递归，找到文件打印即可

```C
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

static char* fmtname(char *path) {
    char *p;

    for(p = path + strlen(path); p >= path && *p != '/'; --p);
    ++p;

    return p;
}

void find(char *path, char *filename) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;
    
    if((fd = open(path, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", path);
        return;
    }

    if(fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", path);
        close(fd);
        return;
    }

    if(st.type != T_DIR) {
        fprintf(2, "find: %s is not a directory\n", path);
        close(fd);
        return;
    }

    if(strlen(path) + 1 + DIRSIZ + 1 > sizeof buf) {
        printf("find: path too long\n");
        return;
    }

    strcpy(buf, path);
    p = buf + strlen(buf);
    *p++ = '/';
    // 遍历当前路径文件内容
    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if(de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        // 是文件且文件名匹配，直接打印输出
        if(st.type == T_FILE && strcmp(fmtname(buf), filename) == 0) {
            printf("%s\n", buf);
        }
        // 是目录且不是.和..，递归查询
        if(st.type == T_DIR && strcmp(de.name, ".") && strcmp(de.name, "..")) {
            find(buf, filename);
        }
    }


}

int main(int argc, char *argv[]) {
    if(argc < 3) {
        fprintf(2, "Usage: find PATH FILE\n");
        exit(1);
    }    
    find(argv[1], argv[2]);
    exit(0);
}
```

## _xargs(<font color="0000ff">Moderate</font>)_

`xargs COMMAND ARGS`作用为对对于`stdin`每一行输入`INPUT`执行`xargs COMMAND ARGS INPUT`

```C
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"


int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(2, "Usage: xargs COMMAND\n");
    }
    // 作为exec的第二参数
    char *exec_argv[MAXARG];
    char buffer[512], *p = buffer;
    int i, n;
    for(i = 0; i < argc - 1; ++i)
        exec_argv[i] = argv[i + 1];
    
    do {
        n = read(0, p, sizeof(char));
        // 读取到文件尾或者换行表明该行结束
        if((n == 0 && p != buffer)|| *p == '\n') {
            // 添加字符串结束符
            *p = '\0';
            // exec_argv添加当前行作为参数，再后面添加0表明字符串数组结束
            exec_argv[i] = buffer;
            exec_argv[i + 1] = 0;
            if(fork() == 0) {
                exec(exec_argv[0], exec_argv);
                exit(1);
            }
            else {
                wait(0);
                p = buffer;
            }
        }
        else 
            ++p;
    } while(n != 0); // 读取stdin，直到遇到文件尾

    exit(0);
}
```