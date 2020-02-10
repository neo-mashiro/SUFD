#include <iostream>
using namespace std;

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/file.h>
#include <errno.h>
#include <pthread.h>

pthread_mutex_t lock1;
pthread_mutex_t lock2;

void* do_lock (int n) {
  pthread_mutex_lock(&lock1);
  cout << "Thread " << n << " enters critical.\n";
  sched_yield();
  sleep(3);
  pthread_mutex_unlock(&lock1);
  cout << "Thread " << n << " exits critical.\n";
  return NULL;
}

void test1() {
  // we create three threads:
  pthread_t tt;
  pthread_attr_t ta;
  pthread_attr_init(&ta);
  pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

  pthread_create(&tt, &ta, (void* (*) (void*))do_lock, (void*)1);
  pthread_create(&tt, &ta, (void* (*) (void*))do_lock, (void*)2);
  pthread_create(&tt, &ta, (void* (*) (void*))do_lock, (void*)3);
  sched_yield();
  sleep(60);
}

void* do_lock_21 (int n) {
  pthread_mutex_lock(&lock2);
  cout << "Thread " << n << " enters critical 1.\n";
  sched_yield();
  sleep(1);
  pthread_mutex_lock(&lock1);
  cout << "Thread " << n << " enters critical 2.\n";
  sched_yield();
  sleep(3);
  pthread_mutex_unlock(&lock2);
  cout << "Thread " << n << " exits critical 2.\n";
  pthread_mutex_unlock(&lock1);
  cout << "Thread " << n << " exits critical 1.\n";
  return NULL;
}

void* do_lock_12 (int n) {
  pthread_mutex_lock(&lock1);
  cout << "Thread " << n << " enters critical 1.\n";
  sched_yield();
  sleep(1);
  pthread_mutex_lock(&lock2);
  cout << "Thread " << n << " enters critical 2.\n";
  sched_yield();
  sleep(3);
  pthread_mutex_unlock(&lock2);
  cout << "Thread " << n << " exits critical 2.\n";
  pthread_mutex_unlock(&lock1);
  cout << "Thread " << n << " exits critical 1.\n";
  return NULL;
}

int test2 () {
  // we create two threades:
  // we create three threads:
  pthread_t tt;
  pthread_attr_t ta;
  pthread_attr_init(&ta);
  pthread_attr_setdetachstate(&ta,PTHREAD_CREATE_DETACHED);

  pthread_create(&tt, &ta, (void* (*) (void*))do_lock_12, (void*)1);
  pthread_create(&tt, &ta, (void* (*) (void*))do_lock_21, (void*)2);
  sched_yield();
  sleep(60);
}

int main (int argc, char** argv) {
  pthread_mutex_init(&lock1,NULL);
  pthread_mutex_init(&lock2,NULL);

  if (argc > 1 && argv[1][0] == 'b')
    test2();
  else
    test1();
}
