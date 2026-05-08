//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_STL_H
#define DNSRELAY_STL_H
typedef void* T; //解耦具体类型

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

void LinkedList_addFirst(LinkedList* list,T data);
void LinkedList_addLast(LinkedList* list,T data);
void LinkedList_addLast(LinkedList* list,T data);

//-------------优先队列--------------------
typedef int (*Comparator)(T a,T b); // 比较函数，返回“a-b”的值，以此判断大小关系
typedef struct  {
    Comparator comparator;
    T* data;
    int size;
}PriorityQueue;
PriorityQueue* PriorityQueue_create(Comparator comparator);

void PriorityQueue_add(PriorityQueue* queue,T data);
//取最小的元素
T PriorityQueue_poll(PriorityQueue* queue);
T PriorityQueue_peek(PriorityQueue* queue);

//-------------哈希表-------------

#endif //DNSRELAY_STL_H