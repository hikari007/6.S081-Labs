#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"


int main(int argc, char *argv[]) {
    if(argc < 2) {
        fprintf(2, "Usage: xargs COMMAND\n");
    }
    char *exec_argv[MAXARG];
    char buffer[512], *p = buffer;
    int i, n;
    for(i = 0; i < argc - 1; ++i)
        exec_argv[i] = argv[i + 1];
    
    do {
        n = read(0, p, sizeof(char));
        if((n == 0 && p != buffer)|| *p == '\n') {
            *p = '\0';
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
    } while(n != 0);

    exit(0);
}
    