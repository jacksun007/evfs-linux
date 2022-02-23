#include <sys/sendfile.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <libgen.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <ftw.h>

int nr_total = 0;

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
    
    if (res == 0) {
        nr_total++;
    }
    
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

int process_file_list(const char * filename)
{
    FILE * fp = fopen(filename, "rt");
    struct stat statbuf;
    char * line = NULL;
    size_t len = 0;
    int res = 0;

    if (!fp) {
        fprintf(stderr, "error: could not open '%s'\n", filename);
        return -errno;
    }
    
    while (getline(&line, &len, fp) != -1) {
        size_t slen = 0;
        char * cp = line;
        
        while (!isspace(*cp) && slen < len) {
            slen++;
            cp++;
        }
       
        // empty line
        if (slen == 0) {
            continue;
        }
        
        *cp = 0;    // found first space character, trim it
        if ((res = stat(line, &statbuf)) < 0) {
            printf("error: could not stat '%s'\n", line);
            goto done;
        }
        
        if (!(statbuf.st_mode | S_IFREG)) {
            printf("error: '%s' is not a regular file!\n", line);
            goto done;
        }
        
        if ((res = process_reg_file(line, &statbuf)) < 0) {
            printf("error: could not process '%s'\n", line);
            goto done;
        }
    }

done:    
    if (line) {
        free(line);
    }
    
    fclose(fp);
    return res;
}

int usage(const char * prog) {
    fprintf(stderr, "usage: %s [-f] PATH\n", prog);
    return EXIT_FAILURE;
}

int main(int argc, const char * argv[])
{
    int res = 0;

    if (argc <= 1 || argc >= 4) {
        return usage(argv[0]);
    }

    if (!strcmp(argv[1], "-f") && argc != 3) {
        return usage(argv[0]);
    }
    else if (argc == 2) {
        res = ftw(argv[1], ftw_callback, 512);
    }

    res = process_file_list(argv[2]);
    
    printf("%d regular files processed\n", nr_total);
    return res;
}

