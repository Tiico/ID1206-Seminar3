#include <stdio.h>
#include "green.h"

int flag = 0;
green_cond_t cond;
green_mutex_t mutex;
int counter = 0;
int numThreads = 7;

void *test(void *arg){
  int id = *(int*)arg;
  int loop = 1000;
  while (loop > 0) {
    if (flag == id) {
      printf("thread %d: %d\n",id, loop);
      for(int i = 0; i < 100000; i++) {
        if(i < 1)
          i++;
      }
      loop--;
      flag = (id + 1) % numThreads;
      green_cond_signal(&cond);
    }else{
      green_cond_signal(&cond);
      green_cond_wait(&cond);
    }
  }
}
int globalcount = 0;

void* testLock(void* arg) {
  int id = *(int*)arg;
  for(int i = 0; i < 10000; i++) {
    green_mutex_lock(&mutex);
      globalcount++;
    green_mutex_unlock(&mutex);
  }
  printReady();
  printf("globalcount: %d\n", globalcount);
}

void* testCV(void* arg) {
  int id = *(int*)arg;
  int loop = 4;
  while(loop > 0) {
    if(flag == id) {
      printf("thread %d: %d\n", id, loop);
      loop--;
      flag = (id + 1) % numThreads;
      green_cond_signal(&cond);
    } else {
      //printf("Thread waiting: %d, flag: %d\n", id, flag);
      green_cond_signal(&cond);
      green_cond_wait(&cond);
    }

  }
}

int main(){
  green_t g0, g1, g2;
  int a0 = 0;
  int a1 = 1;
  int a2 = 2;
  green_cond_init(&cond);
  green_mutex_init(&mutex);

  green_t threads[numThreads];
  int args[numThreads];

  for(int i = 0; i < numThreads; i++){
      args[i] = i;
    }

  for(int i = 0; i < numThreads; i++) {
    green_create(&threads[i], testLock, &args[i]);
    //printf("Thread %d created!\n", i);
  }

  for(int i = 0; i < numThreads; i++) {
    green_join(&threads[i]);
    //printf("Joined thread %d\n", i);
  }

  printf("done\n");
  return 0;
}
