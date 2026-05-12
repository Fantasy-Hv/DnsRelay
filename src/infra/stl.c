//
// Created by yian on 2026/5/8.
//
# include  "infra/stl.h"

#include <stddef.h>

/**
 *
 * @param init_capacity
 * @return
 */
Vector* vector_create(int init_capacity) {

}
/**
 * 追加元素，如果超过容量，要扩容
 * @param vector
 * @param element
 */
void vector_add(Vector* vector,T element) {}
/**
 * 在指定位置插入元素，如果超过容量,要扩容。
 *
 * @param vector
 * @param element
 * @param index
 */
void vector_insert(Vector* vector,T element,int index) {

}
void vector_remove(Vector *vector,int index) {

}

T vector_get(const Vector* vector,int index) {

}

/**
 * 返回向量中元素数量
 * @param vector
 * @return
 */
int vector_size(const Vector* vector) {

}

void vector_free(Vector* vector) {

}

//---------------------------------------
LinkedList* linked_list_create() {
    return NULL;
}

void linked_list_addFirst(LinkedList* list,T data) {

}

void linked_list_addLast(LinkedList* list,T data) {

}

/**
 * 删除元素
 * @param data 元素
 * @param comparator 元素比较函数
 */
void linked_list_remove(T data,Comparator comparator) {
}

int linked_list_is_empty(LinkedList* list) {
    return 0;
}

void linked_list_clear(LinkedList*list) {

}

void linked_list_free(LinkedList* list) {

}

//-------------------------------

PriorityQueue* priority_queue_create(Comparator comparator) {
    return NULL;
}
/**
 * 添加元素，比较逻辑由队列的Comparator决定，允许添加相同元素
 * @param queue
 * @param data
 */
void priority_queue_add(PriorityQueue* queue,T data) {

}
//取最小的元素，如果队列为空，返回NULL
T priority_queue_pop(PriorityQueue* queue) {
    return NULL;
}
T priority_queue_peek(PriorityQueue* queue) {
    return NULL;
}
/**
 * 删除所有值为data的元素，懒删除，标记目标元素即可
 * @param queue
 * @param data
 */
void priority_remove(PriorityQueue* queue,T data) {

}
void priority_queue_free(PriorityQueue* queue) {}

//--------------------------------------
HashMap * hash_map_create(HashFunction hash_function,Comparator comparator) {
    return NULL;
}
//释放哈希表使用的所有内存
void hash_map_free(HashMap* map) {}


void hash_map_put(HashMap* map, K key,T data) {

}
int hash_map_get(const HashMap* map,K key,T* result) {
    return 0;
}
void hash_map_remove(HashMap* map,K key) {

}