/**
 * =============================================================================
 * 文件: simple_list.h
 * 功能: 通用单向链表，支持存储任意类型的数据（void*指针）
 * 作者: [你的名字]
 * 版本: 1.0
 * =============================================================================
 * 
 * 【使用方法】
 *   1. #include "simple_list.h"
 *   2. LIST_HEAD my_list; list_init(&my_list);
 *   3. 用 list_add_head() / list_add_tail() 添加数据
 *   4. 用 list_find() 查找数据（需提供比较函数）
 *   5. 用 list_delete() 删除数据（需提供比较函数）
 *   6. 用 list_destroy() 清空整个链表（需提供释放函数）
 * 
 * 【示例代码——存储整数】
 *   int *p = malloc(sizeof(int)); *p = 100;
 *   list_add_head(&my_list, p);              // 插入
 *   // 查找值为100的节点
 *   LIST_NODE *node = list_find(&my_list, [](void *d){ 
 *       return (*(int*)d == 100) ? 0 : 1; 
 *   });
 *   printf("%d\n", *(int*)node->data);       // 输出100
 *   // 清空链表（释放所有int）
 *   list_destroy(&my_list, [](void *d){ free(d); });
 * 
 * 【示例代码——存储自定义结构体】
 *   typedef struct { int id; char name[20]; } User;
 *   User *u = malloc(sizeof(User)); u->id = 1; strcpy(u->name, "Alice");
 *   list_add_tail(&my_list, u);
 *   // 查找 id=1 的用户
 *   LIST_NODE *node = list_find(&my_list, [](void *d){
 *       return (((User*)d)->id == 1) ? 0 : 1;
 *   });
 * 
 * 【依赖的头文件】
 *   - stdlib.h (malloc/free)
 * 
 * 【注意事项】
 *   - 链表只存储指针，不拷贝数据，调用者需保证数据指针的有效性
 *   - 删除节点时不会自动释放 data，需调用者自行管理
 *   - 使用 list_destroy() 时需传入释放函数，否则会导致内存泄漏
 * =============================================================================
 */

#ifndef __SIMPLE_LIST_H__
#define __SIMPLE_LIST_H__

/**
 * 链表节点结构体
 */
typedef struct list_node {
    void *data;                 /**< 指向任意类型的数据 */
    struct list_node *next;     /**< 指向下一个节点 */
} LIST_NODE;

/**
 * 链表头结构体
 */
typedef struct list_head {
    LIST_NODE *first;           /**< 指向第一个节点 */
    int count;                  /**< 节点数量 */
} LIST_HEAD;

/* ==================== 函数声明 ==================== */

/**
 * 初始化链表头
 * 
 * 使用前必须先调用此函数
 * 
 * @param head  指向 LIST_HEAD 结构体的指针
 * 
 * @示例
 *   LIST_HEAD my_list;
 *   list_init(&my_list);
 */
extern void list_init(LIST_HEAD *head);

/**
 * 在链表头部插入一个节点
 * 
 * @param head  链表头指针
 * @param data  要存储的数据指针（不能为NULL）
 * @return      0 = 成功
 *             -1 = 失败（malloc失败）
 * 
 * @示例
 *   int *p = malloc(sizeof(int)); *p = 10;
 *   list_add_head(&my_list, p);
 */
extern int list_add_head(LIST_HEAD *head, void *data);

/**
 * 在链表尾部追加一个节点
 * 
 * @param head  链表头指针
 * @param data  要存储的数据指针
 * @return      0 = 成功
 *             -1 = 失败（malloc失败）
 */
extern int list_add_tail(LIST_HEAD *head, void *data);

/**
 * 查找节点（需传入比较函数）
 * 
 * @param head        链表头指针
 * @param match_func  比较函数指针
 *                    参数: 节点中存储的数据指针
 *                    返回值: 0 表示匹配, 非0 表示不匹配
 * @return            找到的节点指针
 *                    NULL = 未找到
 * 
 * @示例
 *   // 查找 data == 100 的节点
 *   int target = 100;
 *   LIST_NODE *node = list_find(&my_list, [](void *d){
 *       return (*(int*)d == 100) ? 0 : 1;
 *   });
 */
extern LIST_NODE *list_find(LIST_HEAD *head, int (*match_func)(void *data));

/**
 * 删除节点（需传入比较函数）
 * 
 * 删除第一个匹配的节点，返回被删除的数据指针
 * 
 * @param head        链表头指针
 * @param match_func  比较函数（同 list_find）
 * @return            被删除节点的 data 指针
 *                    NULL = 未找到或链表为空
 * 
 * @note 调用者需要负责 free() 返回的 data 指针
 * 
 * @示例
 *   void *deleted = list_delete(&my_list, [](void *d){
 *       return (*(int*)d == 100) ? 0 : 1;
 *   });
 *   if (deleted) {
 *       printf("删除了: %d\n", *(int*)deleted);
 *       free(deleted);
 *   }
 */
extern void *list_delete(LIST_HEAD *head, int (*match_func)(void *data));

/**
 * 遍历链表，对每个节点执行回调函数
 * 
 * @param head      链表头指针
 * @param callback  回调函数，参数为每个节点的 data
 * 
 * @示例
 *   // 打印所有整数
 *   list_foreach(&my_list, [](void *d){
 *       printf("%d ", *(int*)d);
 *   });
 */
extern void list_foreach(LIST_HEAD *head, void (*callback)(void *data));

/**
 * 清空链表（只释放节点结构体，不释放 data）
 * 
 * 适用于 data 指向的是静态数据或由外部管理的情况
 * 
 * @param head  链表头指针
 * 
 * @note 调用后链表变为空，count = 0
 */
extern void list_clear(LIST_HEAD *head);

/**
 * 清空链表并释放所有 data
 * 
 * @param head        链表头指针
 * @param free_data   释放 data 的函数指针（可为NULL）
 *                    如果为NULL，则只释放节点结构体，不释放data
 * 
 * @示例
 *   // data 是 malloc 分配的 int*
 *   list_destroy(&my_list, [](void *d){ free(d); });
 * 
 *   // data 是静态数据，不需要释放
 *   list_destroy(&my_list, NULL);
 */
extern void list_destroy(LIST_HEAD *head, void (*free_data)(void *data));

/**
 * 获取链表节点数量
 * 
 * @param head  链表头指针
 * @return      节点数量
 */
static inline int list_count(LIST_HEAD *head)
{
    return head->count;
}

/**
 * 判断链表是否为空
 * 
 * @param head  链表头指针
 * @return      1 = 空, 0 = 非空
 */
static inline int list_is_empty(LIST_HEAD *head)
{
    return (head->first == NULL);
}

extern void list_delete_node(LIST_HEAD *head, LIST_NODE *node, void (*free_data)(void *data));

#endif /* __SIMPLE_LIST_H__ */