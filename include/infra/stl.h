//
// Created by yian on 2026/5/8.
//

#ifndef DNSRELAY_STL_H
#define DNSRELAY_STL_H
#include <stdint.h>

//删除和增加操作都是增删指针（一个无符号整型），容器在执行crud方法时，只是在对这个整型进行操作。
typedef void* T; //值的类型
typedef void* K; //key的类型
typedef int (*Comparator)(T a,T b); // 比较函数，返回“a-b”的值，以此判断元素大小等于关系
typedef int (*HashFunction)( K key); //计算元素的哈希值

//预定义的计算函数，可供上层选用,这里是直接将uint16拷贝了
static  int hash_uint16_t(K key) {
    return *(uint16_t*)key;
}
// 字符串哈希函数
static int hash_str(K key) {
    return 0;
}
//----------------向量--------------
typedef struct Vector {
    T* elements; //元素列表
    int size; //当前元素个数
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




T vector_get(Vector* vector,int index);



/**
 * 返回向量中元素数量
 * @param vector
 * @return
 */
int vector_size(Vector* vector);





//--------------链表-----------------
typedef struct LinkNode{
    T data;
    struct LinkNode* prev;
    struct LinkNode* next;
}LinkedNode;
typedef struct LinkedList {
//todo待实现，具体持有待实现
    LinkedNode* head;
    LinkedNode* tail;
    int size;
}LinkedList;
LinkedList* linked_list_create();
void linked_list_free(LinkedList* list);
void linked_list_addFirst(LinkedList* list,T data);
void linked_list_addLast(LinkedList* list,T data);
void linked_list_clear(LinkedList*list);
int linked_list_is_empty(LinkedList* list);
/**
 * 删除元素
 * @param list 链表
 * @param data 元素
 * @param comparator 元素比较函数
 */
void linked_list_remove(LinkedList* list,T data,Comparator comparator);


//-------------优先队列--------------------


/**用于实现懒删除，当删除中间元素时，应当搜索并设置标记，直到元素到达堆顶再做实际删除。
比较逻辑：
两个未删除元素比较：调用比较器比较
已删除元素 < 未删除元素
当两个元素都被删除时，顺序任意
*/
typedef struct HeapNode {
    T data;
    char is_deleted;
} HeapElement;

/**
 * 最小值优先的队列，自扩容
 * 允许删除元素，删除元素时，会将所有相同元素都删除
 *
 */
typedef struct  {
    Comparator comparator;
    HeapElement * elements;
    int size; // 当前元素数量（包括逻辑删除的元素）
    int capacity; //当前容量

}PriorityQueue;

PriorityQueue* priority_queue_create(Comparator comparator);

/**
 * 添加元素，允许重复
 * @param queue
 * @param data
 */
void priority_queue_add(PriorityQueue* queue,T data);
/**
 *取出最小的元素，如果队列为空，返回NULL，
*/
T priority_queue_pop(PriorityQueue* queue);//  如果堆顶元素被标记为删除，就继续pop直到得到有效元素/NULL后返回

/**
 * 查看最小的元素
 * @param queue
 * @return
 */
T priority_queue_peek(PriorityQueue* queue);

/**
 * 删除所有值为data的元素
 * @param queue
 * @param data
 */
void priority_remove(PriorityQueue* queue,T data);



//-------------哈希表-------------
typedef struct {
    Vector* buckets; // 向量的每个元素都是一个链表,链表的元素类型为数据类型T。可以用linklist,也可以用LinkNode
    float load_factor ; //装填因子，默认0.75
    HashFunction hash_function; // 哈希函数计算的哈希值不要直接拿来当索引，不安全。
    Comparator comparator;
}HashMap;

HashMap * hash_map_create(HashFunction hash_function,Comparator comparator);

/**
  添加键值对，应当满足幂等性。
 * @param map
 * @param key
 * @param data
 */
void hash_map_put(HashMap* map,K key,T data);
int hash_map_get(HashMap* map,K key,T* result);
void hash_map_remove(HashMap* map,K key);
#endif //DNSRELAY_STL_H
