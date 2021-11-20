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
    
    if(pid > 0) {
        close(fds1[0]);
        close(fds2[1]);
        int p = getpid();
        write(fds1[1], write_buffer, 1);
        read(fds2[0], read_buffer, 1);
        fprintf(1, "%d: received pong\n", p);
    } else {
        close(fds1[1]);
        close(fds2[0]);
        int p = getpid();
        read(fds1[0], read_buffer, 1);
        fprintf(1, "%d: received ping\n", p);
        write(fds2[1], write_buffer, 1);
    }
    
    exit(0);
}
    
