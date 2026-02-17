#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>

// 线程函数声明
void threads_init(void);
static void create_thread(pthread_t *t, void* (*f)(void*), const char *name) ;
void *system_publish_thread(void *arg) ;
void *mqtt_thread(void *arg);
void *sensor_thread(void *arg);
void *network_publish_thread(void *arg);
void *button_thread(void *arg);

#endif
