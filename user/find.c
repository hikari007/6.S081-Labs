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
    while(read(fd, &de, sizeof(de)) == sizeof(de)) {
        if(de.inum == 0)
            continue;
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if(stat(buf, &st) < 0) {
            printf("find: cannot stat %s\n", buf);
            continue;
        }

        if(st.type == T_FILE && strcmp(fmtname(buf), filename) == 0) {
            printf("%s\n", buf);
        }
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