//
// Created by yian on 2026/5/8.
//
# include  "infra/stl.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/**
 * Vector 实现建议：
 * 1. 维护三个状态：elements / size / capacity
 * 2. 保持不变量：0 <= size <= capacity
 * 3. 有效元素区间永远是 [0, size)
 *
 * 常用库函数提示：
 * - malloc/calloc: 初始分配结构体和底层数组
 * - realloc: 扩容底层数组
 * - memmove: 插入、删除时搬移元素，允许源和目标重叠
 * - free: 释放数组和结构体本身
 */
static int vector_grow(Vector *vector, int min_capacity) {
    if (vector == NULL) {
        return 0;
    }

    if (min_capacity <= vector->capacity) {
        return 1;
    }
    
    int temp_capacity = vector->capacity;
    if (vector->capacity <= 0) {
        temp_capacity = 1;
    }

    while (min_capacity > temp_capacity) {
        temp_capacity <<= 1;
    }

    T * temp_elements = realloc(vector->elements,sizeof(T) * temp_capacity);
    if (temp_elements == NULL) {
        return 0;
    }

    vector->elements = temp_elements;
    vector->capacity = temp_capacity;
    return 1;
}


Vector* vector_create(int init_capacity) {
    if (init_capacity <= 0) {
        init_capacity = 1;
    }

    Vector* vector = malloc(sizeof(Vector));
    if (vector == NULL) {
        return NULL;
    }

    vector->elements = malloc(sizeof(T) * init_capacity);
    if (vector->elements == NULL) {
        free(vector);
        return NULL;
    }

    vector->capacity = init_capacity;
    vector->size = 0;
    return vector;
}

/**
 * 追加元素，如果超过容量，要扩容
 * @param vector
 * @param element
 */
void vector_add(Vector* vector,T element) {

    if (vector == NULL) {
        return;
    }

    if (!vector_grow(vector, vector->size + 1)) {
        return;
    }

    vector->elements[vector->size] = element;
    vector->size++;
}
/**
 * 在指定位置插入元素，如果超过容量,要扩容。
 *
 * @param vector
 * @param element
 * @param index
 */
void vector_insert(Vector* vector,T element,int index) {
    if (vector == NULL || index < 0 || index > vector->size) {
        return;
    }

    if(! vector_grow(vector,vector->size + 1)) {
        return;
    }

    for(int i = vector->size - 1;i >= index;i --) {
        vector->elements[i + 1] = vector->elements[i];
    }

    vector->elements[index] = element;
    vector->size ++;
}

void vector_remove(Vector *vector,int index) {

    if (vector == NULL || index >= vector->size || index < 0) {
        return;
    }

    memmove(vector->elements + index,vector->elements + index + 1,sizeof(T) * (vector->size - index - 1) );

    vector->size --;
}

T vector_get(Vector* vector,int index) {


    if (vector == NULL || index >= vector->size || index < 0) {
        return NULL;
    }

    return vector->elements[index];
}

/**
 * 返回向量中元素数量
 * @param vector
 * @return
 */
int vector_size(Vector* vector) {

    if (vector == NULL ) {
        return 0;
    }

    return vector->size;
}

void vector_free(Vector* vector) {

    if (vector == NULL) {
        return;
    }
    free(vector->elements);
    free(vector);
}

//---------------------------------------
static LinkedNode *linked_node_create(T data) {
    LinkedNode *node = malloc(sizeof(LinkedNode));
    if (node == NULL) {
        return NULL;
    }
    node->data = data;
    node->prev = NULL;
    node->next = NULL;
    return node;
}

LinkedList* linked_list_create() {
    LinkedList *list = malloc(sizeof(LinkedList));
    if (list == NULL) {
        return NULL;
    }
    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
    return list;
}

void linked_list_addFirst(LinkedList* list,T data) {
    if (list == NULL) {
        return;
    }

    LinkedNode *node = linked_node_create(data);
    if (node == NULL) {
        return;
    }

    node->next = list->head;
    if (list->head != NULL) {
        list->head->prev = node;
    } else {
        list->tail = node;
    }

    list->head = node;
    list->size++;
}

void linked_list_addLast(LinkedList* list,T data) {
    if (list == NULL) {
        return;
    }

    LinkedNode *node = linked_node_create(data);
    if (node == NULL) {
        return;
    }

    node->prev = list->tail;
    if (list->tail != NULL) {
        list->tail->next = node;
    } else {
        list->head = node;
    }

    list->tail = node;
    list->size++;
}

/**
 * 删除元素
 * @param data 元素
 * @param comparator 元素比较函数
 */
void linked_list_remove(LinkedList* list,T data,Comparator comparator) {
    if (list == NULL || comparator == NULL) {
        return;
    }

    LinkedNode *node = list->head;
    while (node != NULL) {
        LinkedNode *next = node->next;
        if (comparator(node->data, data) == 0) {
            if (node->prev != NULL) {
                node->prev->next = node->next;
            } else {
                list->head = node->next;
            }

            if (node->next != NULL) {
                node->next->prev = node->prev;
            } else {
                list->tail = node->prev;
            }

            free(node);
            list->size--;
        }
        node = next;
    }
}

int linked_list_is_empty(LinkedList* list) {
    return list == NULL || list->size == 0;
}
void linked_list_foreach(LinkedList * list,void (*consumer)(T value)) {
    if (list==NULL||consumer==NULL)return;

    for (LinkedNode *node = list->head;node!=NULL;node = node->next)
        consumer(node->data);

}
void linked_list_clear(LinkedList*list) {
    if (list == NULL) {
        return;
    }

    LinkedNode *node = list->head;
    while (node != NULL) {
        LinkedNode *next = node->next;
        free(node);
        node = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->size = 0;
}

void linked_list_free(LinkedList* list) {
    if (list == NULL) {
        return;
    }
    linked_list_clear(list);
    free(list);
}

//-------------------------------
// typedef struct HeapNode {
//     T data;
//     char is_deleted;
// } HeapElement;

// /**
//  * 最小值优先的队列，自扩容
//  * 允许删除元素，删除元素时，会将所有相同元素都删除
//  *
//  */
// typedef struct  {
//     Comparator comparator;
//     HeapElement * elements;
//     int size; // 当前元素数量（包括逻辑删除的元素）
//     int capacity; //当前容量

// }PriorityQueue;
static int heap_element_compare(const PriorityQueue *queue,
                                const HeapElement *a,
                                const HeapElement *b) {
    if (a == NULL || b == NULL) {
        return 0;
    }
    if (a->is_deleted && b->is_deleted) {
        return 0;
    }
    if (a->is_deleted) {
        return -1;
    }
    if (b->is_deleted) {
        return 1;
    }
    if (queue == NULL || queue->comparator == NULL) {
        return 0;
    }
    return queue->comparator(a->data, b->data);
}

static void down(PriorityQueue* queue,int u){
    if (queue == NULL || u <= 0 || u > queue->size) {
        return;
    }

    while (1) {
        int t = u;
        int left = u * 2;
        int right = u * 2 + 1;

        if (left <= queue->size &&
            heap_element_compare(queue, &queue->elements[t], &queue->elements[left]) > 0) {
            t = left;
        }

        if (right <= queue->size &&
            heap_element_compare(queue, &queue->elements[t], &queue->elements[right]) > 0) {
            t = right;
        }
        if (t == u) {
            return;
        }
        HeapElement temp = queue->elements[t];
        queue->elements[t] = queue->elements[u];
        queue->elements[u] = temp;

        u = t;
    }
}

static void up(PriorityQueue* queue,int u){
    if (queue == NULL || u <= 0 || u > queue->size) {
        return;
    }

    while (1) {
        int t = u / 2;
        if (t && heap_element_compare(queue, &queue->elements[u], &queue->elements[t]) < 0) {
            HeapElement temp = queue->elements[t];
            queue->elements[t] = queue->elements[u];
            queue->elements[u] = temp;
            u = t;
        } else {
            return;
        }
    }
}

static int priority_queue_grow(PriorityQueue* queue,int min_capacity) {
    if (queue == NULL) {
        return 0;
    }

    if (min_capacity <= queue->capacity) {
        return 1;
    }

    int temp_capacity = queue->capacity;
    if (temp_capacity <= 0) {
        temp_capacity = 1;
    }

    while (min_capacity > temp_capacity) {
        temp_capacity <<= 1;
    }

    HeapElement* temp = realloc(queue->elements, sizeof(HeapElement) * (size_t)(temp_capacity + 1));
    if (temp == NULL) {
        return 0;
    }
    queue->elements = temp;
    queue->capacity = temp_capacity;
    return 1;
}

PriorityQueue* priority_queue_create(Comparator comparator) {
    if (comparator == NULL) {
        return NULL;
    }
    PriorityQueue* queue = malloc(sizeof(PriorityQueue));
    if (queue == NULL) {
        return NULL;
    }

    const int init_capacity = 16;
    queue->elements = malloc(sizeof(HeapElement) * (init_capacity + 1));
    if (queue->elements == NULL) {
        free(queue);
        return NULL;
    }
    queue->comparator = comparator;
    queue->capacity = init_capacity;
    queue->size = 0;
    return queue;
}


/**
 * 添加元素，比较逻辑由队列的Comparator决定，允许添加相同元素
 * @param queue
 * @param data
 */
void priority_queue_add(PriorityQueue* queue,T data) {
    if (queue == NULL) {
        return;
    }

    if (!priority_queue_grow(queue, queue->size + 1)) {
        return;
    }

    queue->size ++;
    queue->elements[queue->size].data = data;
    queue->elements[queue->size].is_deleted = 0;
    up(queue,queue->size);
}

static void lazy_delete(PriorityQueue* queue) {
    if (queue == NULL) {
        return;
    }
    while (queue->size > 0 && queue->elements[1].is_deleted == 1) {
        queue->elements[1] = queue->elements[queue->size];
        queue->size--;
        down(queue, 1);
    }
}

//取最小的元素，如果队列为空，返回NULL
T priority_queue_pop(PriorityQueue* queue) {
    if (queue == NULL || queue->size == 0) {
        return NULL;
    }
    lazy_delete(queue);

    if (queue->size == 0) {
        return NULL;
    }
    HeapElement temp_element = queue->elements[1];
    queue->elements[1] = queue->elements[queue->size--];
    down(queue, 1);
    return temp_element.data;
}
T priority_queue_peek(PriorityQueue* queue) {
    if (queue == NULL || queue->size == 0) {
        return NULL;
    }
    lazy_delete(queue);

    if (queue->size == 0) {
        return NULL;
    }
    return queue->elements[1].data;
}
/**
 * 删除所有值为data的元素，懒删除，标记目标元素即可
 * @param queue
 * @param data
 */
void priority_remove(PriorityQueue* queue,T data) {
    if (queue == NULL || queue->comparator == NULL) {
        return;
    }
    for (int i = 1; i <= queue->size; i++) {
        if (queue->comparator(queue->elements[i].data, data) == 0) {
            queue->elements[i].is_deleted = 1;
        }
    }
}
void priority_queue_free(PriorityQueue* queue) {
    if (queue == NULL) {
        return;
    }
    free(queue->elements);
    free(queue);
}

//--------------------------------------
// HashMap 采用“桶数组 + 链表”的链地址法处理冲突。
typedef struct {
    K key;
    T data;
} HashMapEntry;

#define HASH_MAP_INIT_BUCKETS 16
#define HASH_MAP_DEFAULT_LOAD_FACTOR 0.75f

static HashMapEntry *hash_map_entry_create(K key, T data) {
    HashMapEntry *entry = malloc(sizeof(HashMapEntry));
    if (entry == NULL) {
        return NULL;
    }
    entry->key = key;
    entry->data = data;
    return entry;
}

static LinkedList *hash_map_bucket_at(HashMap *map, int index) {
    return vector_get(map->buckets, index);
}

static int hash_map_bucket_index(HashMap *map, K key, int bucket_count) {
    // 哈希函数只负责产生整数；真正的桶下标还要对桶数量取模。
    int hash = map->hash_function(key);
    if (hash == INT32_MIN) {
        hash = 0;
    } else if (hash < 0) {
        hash = -hash;
    }
    return hash % bucket_count;
}

static LinkedNode *hash_map_find_node(HashMap *map, LinkedList *bucket, K key) {
    if (map == NULL || bucket == NULL) {
        return NULL;
    }

    // 同一个桶里可能挂多个 entry，靠 comparator 在链表中做精确匹配。
    LinkedNode *node = bucket->head;
    while (node != NULL) {
        HashMapEntry *entry = node->data;
        if (entry != NULL && map->comparator(entry->key, key) == 0) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}

static int hash_map_resize(HashMap *map, int new_bucket_count) {
    if (map == NULL || new_bucket_count <= 0) {
        return 0;
    }

    // 扩容不是单纯把 buckets 变大，而是要把旧元素全部重新分桶。
    Vector *new_buckets = vector_create(new_bucket_count);
    if (new_buckets == NULL) {
        return 0;
    }

    for (int i = 0; i < new_bucket_count; i++) {
        LinkedList *bucket = linked_list_create();
        if (bucket == NULL) {
            for (int j = 0; j < vector_size(new_buckets); j++) {
                linked_list_free(vector_get(new_buckets, j));
            }
            vector_free(new_buckets);
            return 0;
        }
        vector_add(new_buckets, bucket);
    }

    const int old_bucket_count = vector_size(map->buckets);
    for (int i = 0; i < old_bucket_count; i++) {
        LinkedList *bucket = vector_get(map->buckets, i);
        LinkedNode *node = bucket->head;
        while (node != NULL) {
            HashMapEntry *entry = node->data;
            int new_index = hash_map_bucket_index(map, entry->key, new_bucket_count);
            linked_list_addLast(vector_get(new_buckets, new_index), entry);
            node = node->next;
        }
        // entry 已经迁移到新桶；这里只释放旧桶本身。
        linked_list_free(bucket);
    }

    vector_free(map->buckets);
    map->buckets = new_buckets;
    return 1;
}

HashMap * hash_map_create(HashFunction hash_function,Comparator comparator) {
    if (hash_function == NULL || comparator == NULL) {
        return NULL;
    }

    HashMap *map = malloc(sizeof(HashMap));
    if (map == NULL) {
        return NULL;
    }

    map->buckets = vector_create(HASH_MAP_INIT_BUCKETS);
    if (map->buckets == NULL) {
        free(map);
        return NULL;
    }

    // 先把每个桶初始化成空链表，后续 put/get/remove 都按桶操作。
    for (int i = 0; i < HASH_MAP_INIT_BUCKETS; i++) {
        LinkedList *bucket = linked_list_create();
        if (bucket == NULL) {
            for (int j = 0; j < vector_size(map->buckets); j++) {
                linked_list_free(vector_get(map->buckets, j));
            }
            vector_free(map->buckets);
            free(map);
            return NULL;
        }
        vector_add(map->buckets, bucket);
    }

    map->load_factor = HASH_MAP_DEFAULT_LOAD_FACTOR;
    map->hash_function = hash_function;
    map->comparator = comparator;
    map->size = 0;
    return map;
}

//释放哈希表使用的所有内存
void hash_map_free(HashMap* map) {
    if (map == NULL) {
        return;
    }

    const int bucket_count = vector_size(map->buckets);
    for (int i = 0; i < bucket_count; i++) {
        LinkedList *bucket = vector_get(map->buckets, i);
        LinkedNode *node = bucket->head;
        while (node != NULL) {
            HashMapEntry *entry = node->data;
            free(entry);
            node = node->next;
        }
        // bucket 节点由 linked_list_free 回收；这里只负责 entry 本体。
        linked_list_free(bucket);
    }
    vector_free(map->buckets);
    free(map);
}


int hash_map_put(HashMap* map,K key,T data) {
    if (map == NULL || key == NULL) {
        return 1;
    }

    // 先定位桶，再在桶里查是“更新旧值”还是“追加新 entry”。
    int bucket_count = vector_size(map->buckets);
    int bucket_index = hash_map_bucket_index(map, key, bucket_count);
    LinkedList *bucket = hash_map_bucket_at(map, bucket_index);
    LinkedNode *node = hash_map_find_node(map, bucket, key);
    if (node != NULL) {
        HashMapEntry *entry = node->data;
        entry->data = data;
        return 1;
    }

    HashMapEntry *entry = hash_map_entry_create(key, data);
    if (entry == NULL) {
        return 1;
    }
    linked_list_addLast(bucket, entry);
    map->size++;

    // 装填因子过高时扩容，避免桶内链表过长。
    const float threshold = (float) bucket_count * map->load_factor;
    if ((float) map->size > threshold) {
        hash_map_resize(map, bucket_count * 2);
    }
    return 0;
}

int hash_map_get(HashMap* map,K key,T* result) {
    if (map == NULL || key == NULL || result == NULL) {
        return 1;
    }

    // get 和 put 一样，先根据 key 找桶，再在桶内做线性查找。
    int bucket_count = vector_size(map->buckets);
    int bucket_index = hash_map_bucket_index(map, key, bucket_count);
    LinkedList *bucket = hash_map_bucket_at(map, bucket_index);
    LinkedNode *node = hash_map_find_node(map, bucket, key);
    if (node == NULL) {
        return 1;
    }

    HashMapEntry *entry = node->data;
    *result = entry->data;
    return 0;
}

void hash_map_remove(HashMap* map,K key,KeyDestructor destructor) {
    if (map == NULL || key == NULL) {
        return;
    }

    int bucket_count = vector_size(map->buckets);
    int bucket_index = hash_map_bucket_index(map, key, bucket_count);
    LinkedList *bucket = hash_map_bucket_at(map, bucket_index);
    LinkedNode *node = hash_map_find_node(map, bucket, key);
    if (node == NULL) {
        return;
    }

    HashMapEntry *entry = node->data;
    // 直接在桶链表中摘节点，不需要全表搬移元素。
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        bucket->head = node->next;
    }
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        bucket->tail = node->prev;
    }
    if (destructor != NULL) {
        destructor(entry->key);
    }
    free(entry);
    free(node);
    bucket->size--;
    map->size--;
}
