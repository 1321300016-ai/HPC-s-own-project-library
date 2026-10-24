#ifndef __IP_FILTER_H__
#define __IP_FILTER_H__

#include "simple_list.h"

typedef struct ip_data
{
    unsigned char ip[4];
}IP_DATA;

extern LIST_HEAD g_ip_filter_head;

extern void ip_filter_init();
extern void ip_filter_save();
extern int ip_filter_add(unsigned char *ip);
extern int ip_filter_del(unsigned char *ip);
extern int ip_filter_check(unsigned char *ip);
extern void ip_filter_show();
extern void ip_filter_destroy();


#endif // !__IP_FILTER_H__
