/**
 * =============================================================================
 * 文件: simple_list.c
 * 功能: 实现 simple_list.h 中声明的所有函数
 * =============================================================================
 * 
 * 【调用关系】
 *   业务代码
 *        ↓
 *   list_add_head() / list_add_tail()
 *        ↓
 *   malloc(LIST_NODE)  → 节点插入链表
 *   
 *   业务代码
 *        ↓
 *   list_find() / list_delete()
 *        ↓
 *   遍历链表 → 调用 match_func() 比较
 *   
 *   业务代码
 *        ↓
 *   list_destroy()
 *        ↓
 *   遍历链表 → 调用 free_data() 释放 → free(LIST_NODE)
 * 
 * 【时间复杂度】
 *   - 插入头部: O(1)
 *   - 插入尾部: O(n) (需要遍历到尾)
 *   - 查找/删除: O(n)
 * =============================================================================
 */

#include <stdlib.h>
#include "simple_list.h"

void list_init(LIST_HEAD *head)
{
    head->first = NULL;
    head->count = 0;
}

int list_add_head(LIST_HEAD *head, void *data)
{
    LIST_NODE *node;
    
    if (data == NULL) {
        return -1;
    }
    
    node = (LIST_NODE *)malloc(sizeof(LIST_NODE));
    if (node == NULL) {
        return -1;
    }
    
    node->data = data;
    node->next = head->first;
    head->first = node;
    head->count++;
    return 0;
}

int list_add_tail(LIST_HEAD *head, void *data)
{
    LIST_NODE *node;
    LIST_NODE *p;
    
    if (data == NULL) {
        return -1;
    }
    
    node = (LIST_NODE *)malloc(sizeof(LIST_NODE));
    if (node == NULL) {
        return -1;
    }
    
    node->data = data;
    node->next = NULL;
    
    if (head->first == NULL) {
        /* 链表为空，直接作为第一个节点 */
        head->first = node;
    } else {
        /* 遍历到链表尾部 */
        p = head->first;
        while (p->next != NULL) {
            p = p->next;
        }
        p->next = node;
    }
    head->count++;
    return 0;
}

LIST_NODE *list_find(LIST_HEAD *head, int (*match_func)(void *data))
{
    LIST_NODE *p = head->first;
    
    while (p != NULL) {
        if (match_func(p->data) == 0) {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

void *list_delete(LIST_HEAD *head, int (*match_func)(void *data))
{
    LIST_NODE *prev = NULL;
    LIST_NODE *p = head->first;
    void *data;
    
    while (p != NULL) {
        if (match_func(p->data) == 0) {
            data = p->data;
            if (prev == NULL) {
                /* 删除的是第一个节点 */
                head->first = p->next;
            } else {
                prev->next = p->next;
            }
            free(p);
            head->count--;
            return data;
        }
        prev = p;
        p = p->next;
    }
    return NULL;
}

void list_foreach(LIST_HEAD *head, void (*callback)(void *data))
{
    LIST_NODE *p = head->first;
    while (p != NULL) {
        callback(p->data);
        p = p->next;
    }
}

void list_clear(LIST_HEAD *head)
{
    LIST_NODE *p = head->first;
    LIST_NODE *tmp;
    
    while (p != NULL) {
        tmp = p;
        p = p->next;
        free(tmp);
    }
    head->first = NULL;
    head->count = 0;
}

void list_destroy(LIST_HEAD *head, void (*free_data)(void *data))
{
    LIST_NODE *p = head->first;
    LIST_NODE *tmp;
    
    while (p != NULL) {
        tmp = p;
        p = p->next;
        if (free_data != NULL) {
            free_data(tmp->data);
        }
        free(tmp);
    }
    head->first = NULL;
    head->count = 0;
}// simple_list.c 中添加
void list_delete_node(LIST_HEAD *head, LIST_NODE *node, void (*free_data)(void *data))
{
    LIST_NODE *p;
    LIST_NODE *prev = NULL;
    
    if (head == NULL || node == NULL) return;
    
    p = head->first;
    while (p != NULL) {
        if (p == node) {
            if (prev == NULL) {
                head->first = p->next;
            } else {
                prev->next = p->next;
            }
            if (free_data != NULL) {
                free_data(p->data);
            }
            free(p);
            head->count--;
            return;
        }
        prev = p;
        p = p->next;
    }
}