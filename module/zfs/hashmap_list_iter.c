#include <lethe/hashmap_list_iter.h>

struct HashMapListIter hashmaplistiter_new(struct HashMapList *list) {
    struct HashMapListIter self = {
        .curr = list->head->next,
        .end = list->tail,
    };
    return self;
}

bool hashmaplistiter_next(
    struct HashMapListIter *self,
    HASHMAP_KEY_TYPE *key,
    HASHMAP_VAL_TYPE **val
) {
    if (self->curr == self->end) {
        return false;
    }

    *key = self->curr->key;
    *val = &self->curr->val;
    self->curr = self->curr->next;

    return true;
}

void hashmaplistiter_drop(struct HashMapListIter *self) {
    self->curr = NULL;
    self->end = NULL;
}
