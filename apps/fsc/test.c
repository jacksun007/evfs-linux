#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "set.h"

int main()
{
    struct set * set = set_new();
    char *line = NULL;
    size_t len = 0;

    while (1) {
        int i, c;
        long v;               
        
        ssize_t nread;
        
        nread = getline(&line, &len, stdin);
        if (nread < 0) {
            break;
        }
        
        if (line[0] == 'Q' || line[0] == 'q')
            break;
            
        v = strtol(line, NULL, 10);
        if (set_add(set, v) < 0) {
            printf("could not add %ld\n", v);
            break;
        }
    
        printf("{ ");
        c = set_count(set);
        for (i = 0; i < c; i++) {
            v = set_item(set, i);
            printf("%ld, ", v);
        }
        printf("}\n");
    }

    free(line);
    return 0;
}

