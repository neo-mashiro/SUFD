#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/file.h>
#include <errno.h>

int enter_critical (int fd) {
  struct flock lock_info;
  lock_info.l_type = F_WRLCK;
  lock_info.l_whence = SEEK_SET;
  lock_info.l_start = lock_info.l_len = 0;
  int status;

  while ( (status = fcntl(fd,F_SETLKW,&lock_info)) == -1 && 
          errno == EINTR ) {
    // reconstruct lock_info:
    lock_info.l_type = F_WRLCK;
    lock_info.l_whence = SEEK_SET;
    lock_info.l_start = lock_info.l_len = 0;
  }
  if (status == -1)
    perror("enter_critical");
  return status;
}

int exit_critical (int fd) {
  struct flock lock_info;
  lock_info.l_type = F_UNLCK;
  lock_info.l_whence = SEEK_SET;
  lock_info.l_start = lock_info.l_len = 0;
  int status;

  while ( (status = fcntl(fd,F_SETLKW,&lock_info)) == -1 && 
          errno == EINTR ) {
    // reconstruct lock_info:
    lock_info.l_type = F_UNLCK;
    lock_info.l_whence = SEEK_SET;
    lock_info.l_start = lock_info.l_len = 0;
  }
  if (status == -1)
    perror("exit_critical");
  return status;
}

void test1(int lock1) {
  // we create three processes:
  if(fork() == 0) {
    if(fork() == 0) { // nephew = `process 1'
      printf("Process 1 enters critical 1 (%d)\n", enter_critical(lock1));
      sleep(3);
      printf("Process 1 exits critical 1 (%d)\n", exit_critical(lock1));
    }
    else {            // child = `process 2'
      printf("Process 2 enters critical 1 (%d)\n", enter_critical(lock1));
      sleep(3);
      printf("Process 2 exits critical 1 (%d)\n", exit_critical(lock1));
    }
  }
  else {              // parent = `process 3'
    printf("Process 3 enters critical 1 (%d)\n", enter_critical(lock1));
    sleep(3);
    printf("Process 3 exits critical 1 (%d)\n", exit_critical(lock1));
  }  
}

void test2 (int lock1, int lock2) {
  // we create two processes:
  if(fork() == 0) {  // child = `process 1'
    printf("Process 1 enters critical 1 (%d)\n", enter_critical(lock1));
    sleep(1);
    printf("Process 1 enters critical 2 (%d)\n", enter_critical(lock2));
    sleep(3);
    printf("Process 1 exits critical 2 (%d)\n", exit_critical(lock2));
    printf("Process 1 exits critical 1 (%d)\n", exit_critical(lock1));
  }
  else {             // parent = `process 2'
    printf("Process 2 enters critical 2 (%d)\n", enter_critical(lock2));
    sleep(1);
    printf("Process 2 enters critical 1 (%d)\n", enter_critical(lock1));
    sleep(3);
    printf("Process 2 exits critical 2 (%d)\n", exit_critical(lock2));
    printf("Process 2 exits critical 1 (%d)\n", exit_critical(lock1));

  }
}

int main (int argc, char** argv) {
  char lock1name[256], lock2name[256];
  snprintf(lock1name,255,"/tmp/lock-%d-1",getpid());
  snprintf(lock2name,255,"/tmp/lock-%d-2",getpid());
  int lock1 = open(lock1name,O_WRONLY|O_CREAT|O_APPEND,S_IRWXU|S_IRWXG|S_IRWXO); 
  int lock2 = open(lock2name,O_WRONLY|O_CREAT|O_APPEND,S_IRWXU|S_IRWXG|S_IRWXO);

  if (lock1 == -1 || lock2 == -1) {
    perror("Cannot create locks");
    return 1;
  }
  
  //test1(lock1);
  test2(lock1,lock2);
  
  // clean up
  close(lock1);
  close(lock2);
  unlink(lock1name);
  unlink(lock2name);
}
