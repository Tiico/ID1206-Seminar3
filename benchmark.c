#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include "green.h"


int flag = 0;
green_cond_t cond;
green_mutex_t mutex;
pthread_cond_t emptyP, fullP;
pthread_mutex_t mutexP;
int numThreads = 8;

int counter = 0;
int buffer;
int productions;
green_cond_t full, empty;


void produce() {
  for(int i = 0; i < productions/(numThreads/2); i++) {
    green_mutex_lock(&mutex);
    while(buffer == 1)
      green_cond_wait(&empty, &mutex);
    buffer = 1;
    green_cond_signal(&full);
    green_mutex_unlock(&mutex);
  }
}

void consume() {
  for(int i = 0; i < productions/(numThreads/2); i++) {
    green_mutex_lock(&mutex);
    while(buffer == 0) // wait for producer before consuming
      green_cond_wait(&full, &mutex);
    buffer = 0;
    green_cond_signal(&empty);
    green_mutex_unlock(&mutex);
  }
}

void produceP() { // heahae
  for(int i = 0; i < productions/(numThreads/2); i++) {
    pthread_mutex_lock(&mutexP);
    while(buffer == 1) // wait for consumer before producing more
      pthread_cond_wait(&emptyP, &mutexP);
    buffer = 1;
    pthread_cond_signal(&fullP);
    pthread_mutex_unlock(&mutexP);
  }
}

void consumeP() {
  for(int i = 0; i < productions/(numThreads/2); i++) {
    pthread_mutex_lock(&mutexP);
    while(buffer == 0)
      pthread_cond_wait(&fullP, &mutexP);
    buffer = 0;
    pthread_cond_signal(&emptyP);
    pthread_mutex_unlock(&mutexP);
  }
}

void* GtestCP(void* arg) {
  int id = *(int*)arg;
  if(id % 2 == 0) {
    produce();
  } else {
    consume();
  }
}

void* Ptest(void* arg) {
  int id = *(int*)arg;
  if(id % 2 == 0) {
    produceP();
  } else {
    consumeP();
  }
}

void testGreen(int* args) {
  green_t threads[numThreads];

  for(int i = 0; i < numThreads; i++)
    green_create(&threads[i], GtestCP, &args[i]);

  for(int i = 0; i < numThreads; i++)
    green_join(&threads[i]);
}

void testPthread(int* args) {
  pthread_t threads[numThreads];

  for(int i = 0; i < numThreads; i++)
    pthread_create(&threads[i], NULL, Ptest, &args[i]);

  for(int i = 0; i < numThreads; i++)
    pthread_join(threads[i], NULL);
}

int main() {
  clock_t c_start, c_stop;
  double timeGreen = 0, timePthread = 0;
  green_cond_init(&cond);
  green_cond_init(&full);
  green_cond_init(&empty);
  green_mutex_init(&mutex);

  pthread_cond_init(&fullP, NULL);
  pthread_cond_init(&emptyP, NULL);
  pthread_mutex_init(&mutexP, NULL);
  printf("#threads: %d\n", numThreads);
  printf("#Benchmark, Consume/Produce\n#\n#\n");
  printf("#productions\tGreen(ms)\tPthread(ms)\n");
  int numRuns = 50;
  for(int run = 1; run <= numRuns; run++) {
    buffer = 0;
    productions = 100 * 2 * run;

    int args[numThreads];
    for(int i = 0; i < numThreads; i++)
      args[i] = i;

    c_start = clock();
    testGreen(args);
    c_stop = clock();
    timeGreen = ((double)(c_stop - c_start)) / ((double)CLOCKS_PER_SEC/1000);
    c_start = clock();
    testPthread(args);
    c_stop = clock();
    timePthread = ((double)(c_stop - c_start)) / ((double)CLOCKS_PER_SEC/1000);
    printf("%d\t%f\t%f\n", productions, timeGreen, timePthread);
  }
    printf("done\n");

  return 0;
}
