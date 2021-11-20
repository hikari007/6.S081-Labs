#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void childprocess(int in[]) {
    close(in[1]);
    int base, num;
    if(read(in[0], &base, sizeof(int)) == 0) {
        close(in[0]);
        exit(0);
    }
    fprintf(1, "prime %d\n", base);

    int out[2];
    pipe(out);

    if(fork() > 0) {
        close(out[0]);
        while(read(in[0], &num, sizeof(int))) {
            if(num % base)
                write(out[1], &num, sizeof(int));
        }
        close(in[0]);
        close(out[1]);
        wait(0);
    }
    else {
        close(in[0]);
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