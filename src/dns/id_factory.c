//
// Created by yian on 2026/5/10.
//
#include "dns/id.h"
#include <string.h>

static uint16_t ids[65536];
static int top;
static int initialized;
static int st[65536];

static void id_pool_init() {
    top = 65535;
    for (int i = 0; i < 65536; i++) {
        ids[i] = (uint16_t) i;
    }
    memset(st, 0, sizeof(st));
    initialized = 1;
}

int id_alloc(uint16_t* id) {
    if (id == NULL) {
        return 0;
    }

    if (!initialized) {
        id_pool_init();
    }

    if (top < 0) {
        return 0;
    }

    *id = ids[top];
    st[*id] = 1;
    top--;
    return 1;
}

void id_free(uint16_t id) {
    if (st[id] && top < 65535) {
        st[id] = 0;
        ids[++top] = id;
    }
}
