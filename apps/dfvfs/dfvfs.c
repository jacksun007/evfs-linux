#include <sys/sendfile.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>
#include <stdio.h>
#include <ftw.h>

int process_reg_file(const char * path, const struct stat * stat)
{
    int in, out, res;
    unsigned len = strlen(path)+10;
    char * buf = malloc(len*sizeof(char));
    
    strcpy(buf, path);
    strncat(dirname(buf), "/temp.dat", len);
    
    if ((in = open(path, O_RDONLY)) < 0) {
        free(buf);
        return in;
    }
    
    if ((out = creat(buf, stat->st_mode)) < 0) {
        free(buf);
        close(in);
        return out;
    }
    
    res = sendfile(out, in, NULL, stat->st_size);
    close(in);
    close(out);
    
    if (res < 0) {
        free(buf);
        return res;
    }
    
    res = rename(buf, path);
    free(buf);
    return res;
}

int ftw_callback(const char * path, const struct stat * stat, int flag)
{
    if (flag == FTW_F) {
        if (stat->st_mode | S_IFREG) {
            return process_reg_file(path, stat);
        }
    }
    
    return 0;
}


int main(int argc, const char * argv[])
{
    if (argc == 2) {
        return ftw(argv[1], ftw_callback, 512);
    }

    fprintf(stderr, "usage: %s PATH\n", argv[0]);
    return EXIT_FAILURE;
}
