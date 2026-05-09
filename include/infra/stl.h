//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_STL_H
#define DNSRELAY_STL_H
#include <stdint.h>
typedef void* T; //解耦具体类型
typedef int (*Comparator)(T a,T b); // 比较函数，返回“a-b”的值，以此判断大小关系
typedef int (*Extractor)(T a); // 提取函数，提取元素的key,用来唯一标识元素
typedef uint32_t (*HashFunction)(const char* key);
//----------------向量--------------
typedef struct Vector {
    T* elements;
    int capacity;
}Vector;
Vector* vector_create(int init_capacity);

/**
 * 追加元素，如果超过容量，要扩容
 * @param vector
 * @param element
 */
void vector_add(Vector* vector,T element);

/**
 * 在指定位置插入元素，如果超过容量,要扩容。
 *
 * @param vector
 * @param element
 * @param index
 */
void vector_insert(Vector* vector,T element,int index);
void vector_remove(Vector *vector,int index);
void vector_free(Vector* vector);
//--------------链表-----------------
typedef struct LinkNode{
    T data;
    struct LinkNode* next;
}LinkedNode;
typedef struct LinkedList {
//todo待实现，具体持有待实现
    int size;
}LinkedList;
LinkedList* LinkedList_create();
void linkedlist_free(LinkedList* list);
void LinkedList_addFirst(LinkedList* list,T data);
void LinkedList_addLast(LinkedList* list,T data);

/**
 * 删除元素
 * @param data 元素
 * @param comparator 元素比较函数
 */
void LinkedList_remove(T data,Comparator comparator);
//-------------优先队列--------------------



typedef struct HeapNode {
    T data;
    char is_deleted;
} HeapElement;

typedef struct  {
    Comparator comparator;
    HeapElement * elements;
    int size; //队列中实际被占据的槽位数量，因为懒删除机制的存在所以不能反映实际大小
}PriorityQueue;
PriorityQueue* priority_queue_create(Comparator comparator);

/**
 * 添加元素，比较逻辑由队列的Comparator决定
 * @param queue
 * @param data
 */
void priority_queue_add(PriorityQueue* queue,T data);
//取最小的元素
T priority_queue_pop(PriorityQueue* queue);
T priority_queue_peek(PriorityQueue* queue);
//从队列中删除元素，懒删除即可（标记元素为删除,当pop/peek到被删除元素时再pop），
void priority_queue_lazy_remove(PriorityQueue* queue,T target,Extractor key_extractor);
//-------------哈希表-------------
typedef struct {
    Vector* buckets; // 向量的每个元素都是一个链表。可以用linklist,也可以用LinkNode
    float load_factor ; //装填因子，默认0.75
    HashFunction hash_function;
}HashMap;

#endif //DNSRELAY_STL_H