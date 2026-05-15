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


void hash_map_put(HashMap* map,K key,T data) {

}
int hash_map_get(HashMap* map,K key,T* result) {
    return 0;
}
void hash_map_remove(HashMap* map,K key) {

}
