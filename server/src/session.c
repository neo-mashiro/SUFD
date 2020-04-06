/*
** session.c -- session manager for replication consistency synchronization
*/

#include "define.h"

//void* tracker_thread(void* omitted) {
//    while (1) {
//        char* operation = tracker.os.pop();
//        run operation;
//        pthread_mutex_lock(&monitor.m_mtx);
//        time_t now = time(0);  // create a timer pointer
//        printf("monitor: %s\n", ctime(&now));  // local calendar time
//        pthread_mutex_unlock(&monitor.m_mtx);
//    }
//    pthread_exit(NULL);
//}