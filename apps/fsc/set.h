/*
 * set.h
 *
 * set structure declaration
 *
 */
 
struct set;

int set_add(struct set * set, long val);
void set_free(struct set * set);
struct set * set_new(void);
int set_count(struct set * set);
long set_item(struct set * set, int idx);

