#include <lethe/hashmap_list.h>

#ifdef __KERNEL__
    #include <linux/printk.h>
    #include <linux/vmalloc.h>
#else
    #include <inttypes.h>
    #include <stdio.h>
    #include <stdlib.h>
#endif

struct HashMapList hashmaplist_new(void) {
#ifdef __KERNEL__
    struct HashMapList self = {
        .head = vmalloc(sizeof(struct HashMapNode)),
        .tail = vmalloc(sizeof(struct HashMapNode)),
        .len = 0,
    };
#else
    struct HashMapList self = {
        .head = malloc(sizeof(struct HashMapNode)),
        .tail = malloc(sizeof(struct HashMapNode)),
        .len = 0,
    };
#endif

    *self.head = hashmapnode_new(0, btreemap_new());
    *self.tail = hashmapnode_new(0, btreemap_new());

    self.head->next = self.tail;
    self.tail->prev = self.head;

    return self;
}

bool hashmaplist_is_empty(struct HashMapList *self) {
    return self->head->next == self->tail;
}

void hashmaplist_drop(struct HashMapList *self) {
    struct HashMapNode *curr = self->head;
    while (curr) {
        struct HashMapNode *next = curr->next;
        hashmapnode_drop(curr);
#ifdef __KERNEL__
        vfree(curr);
#else
        free(curr);
#endif
        curr = next;
    }
}

struct HashMapNode *hashmaplist_find(struct HashMapList *self, HASHMAP_KEY_TYPE key) {
    for (struct HashMapNode *n = self->head->next; n != self->tail; n = n->next) {
        if (n->key == key) {
            return n;
        }
    }
    return NULL;
}

struct HashMapNode *
hashmaplist_insert(struct HashMapList *self, HASHMAP_KEY_TYPE key, HASHMAP_VAL_TYPE val) {
    struct HashMapNode *n = hashmaplist_find(self, key);
    if (n) {
        return n;
    }

#ifdef __KERNEL__
    n = vmalloc(sizeof(*n));
#else
    n = malloc(sizeof(*n));
#endif
    *n = hashmapnode_new(key, val);

    n->next = self->head->next;
    n->prev = self->head;

    self->head->next->prev = n;
    self->head->next = n;

    return NULL;
}

struct HashMapNode *hashmaplist_remove(struct HashMapList *self, HASHMAP_KEY_TYPE key) {
    struct HashMapNode *n = hashmaplist_find(self, key);
    if (!n) {
        return NULL;
    }

    n->next->prev = n->prev;
    n->prev->next = n->next;

    return n;
}

void hashmaplist_print(struct HashMapList *self) {
#ifdef __KERNEL__
    for (struct HashMapNode *n = self->head->next; n != self->tail; n = n->next) {
        if (n == self->head->next) {
            pr_info("{ %llu }", n->key);
        } else {
            pr_cont("{ %llu }", n->key);
        }
        if (n->next != self->tail) {
            pr_cont(" -> ");
        }
    }
#else
    for (struct HashMapNode *n = self->head->next; n != self->tail; n = n->next) {
        printf("{ %" PRIu64 " }", n->key);
        if (n->next != self->tail) {
            printf(" -> ");
        }
    }
#endif
}
