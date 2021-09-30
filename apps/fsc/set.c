/*
 * set.c
 *
 * set structure implementation
 *
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

struct set {
    int capacity;
    int count;
    long * buffer;
};

static int binary_search(long * buf, int l, int r, long v)
{
    if (r >= l) {
        int m = l + (r - l) / 2;
  
        if (buf[m] == v)
            return m;

        if (buf[m] > v)
            return binary_search(buf, l, m - 1, v);
  
        return binary_search(buf, m + 1, r, v);
    }
  
    // return the last search location
    return -l - 1;
}

// return 0 if not inserted, 1 if yes
int set_add(struct set * set, long val)
{
    int j, i = binary_search(set->buffer, 0, set->count - 1, val); 
    
    // element found
    if (i >= 0) {
        return 0;
    }
    
    j = -(i + 1);
    printf("adding %ld to %d\n", val, j);
    
    if (set->capacity == set->count) {
        set->capacity *= 2;
        set->buffer = realloc(set->buffer, set->capacity * sizeof(long));
    }
    
    for ( i = set->count; i > j; i--) {
        set->buffer[i] = set->buffer[i-1];
    }
    
    set->count += 1;
    set->buffer[j] = val;
    
    return 1;
}

void set_free(struct set * set)
{
    free(set->buffer);
    free(set);
}

struct set * set_new(void)
{
    struct set * s = malloc(sizeof(struct set));
    if (s == NULL)
        return s;
        
    s->capacity = 4;
    s->count = 0;
    
    s->buffer = malloc(sizeof(long) * s->capacity);
    if (s->buffer == NULL) {
        free(s);
        return NULL;
    }
    
    return s;   
}

int set_count(struct set * set) 
{
    return set->count;
}

long set_item(struct set * set, int idx)
{
    assert(idx >= 0 && idx < set->count);
    return set->buffer[idx];
}

