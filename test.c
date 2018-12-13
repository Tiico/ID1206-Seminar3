#include <stdio.h>
#include "green.h"

int numThreads = 4;
int counter = 0;
int flag = 0;
green_cond_t cond;
green_mutex_t mutex;

void *testTimer(void *arg){
	int id = *(int*)arg;
	int loop = 10;
	while(loop > 0){
		if(flag == id){
			printf("thread %d: %d\n", id, loop);
			loop--;
			flag = (id + 1) % 4;
			green_cond_signal(&cond);
		}else{
			green_cond_signal(&cond);
			green_cond_wait(&cond, &mutex);
		}
	}
}
/**
The program falls short without the signal before the wait.
With the signal on the condition before the wait the program works
as intended.
**/

void *testStd(void *arg){
	int i = *(int*)arg;
	int loop = 40000;
	while(loop > 0){
		printf("thread %d: %d\n", i, loop);
		loop--;
		green_yield();
	}
}

void *testSharedResource(void* arg) {
  int id = *(int*)arg;
  for(int i = 0; i < 100000; i++) {
    green_mutex_lock(&mutex);
    int timeWaste = 0;
    while(timeWaste < 1000){
    	timeWaste++;
    }
    counter++;
    green_mutex_unlock(&mutex);
  }
}

/**
Producer consumer for testing the new green_cond_wait() function
**/
int buffer = 0;
int productions;
green_cond_t full, empty;
void produce() {
  for(int i = 0; i < productions; i++) {
    green_mutex_lock(&mutex);
    while(buffer == 1){ // wait for consumer before producing more
      green_cond_wait(&empty, &mutex);
  	}
    buffer++;
    printf("Produced! Remaining: %d!\n", buffer);
    green_cond_signal(&full);
    green_mutex_unlock(&mutex);
  }
}

void consume() {
  for(int i = 0; i < productions/(numThreads-1); i++) {
    green_mutex_lock(&mutex);
    while(buffer == 0){ // wait for producer before consuming
      green_cond_wait(&full, &mutex);
  	}
    buffer--;
    printf("Consumed! Remaining: %d!\n", buffer);
    green_cond_signal(&empty);
    green_mutex_unlock(&mutex);
  }
}
void* testConsumerProducer(void* arg) {
  int id = *(int*)arg;
  if(id == 0) { // producer
    produce();
  } else { // consumer
    consume();
  }

}

int main(){
	productions = 100 * (numThreads-1); // Must be multiple of (numThreads-1)
  	green_cond_init(&cond);
  	green_cond_init(&full);
  	green_cond_init(&empty);
	green_mutex_init(&mutex);

	green_t g0;
	green_t g1;
	green_t g2;
	green_t g3;

	int a0 = 0;
	int a1 = 1;
	int a2 = 2;
	int a3 = 3;

	green_create(&g0, testTimer, &a0);
	green_create(&g1, testTimer, &a1);
	green_create(&g2, testTimer, &a2);
	green_create(&g3, testTimer, &a3);

	green_join(&g0);
	printf("\n");
	printf("Thread nr %d done\n", a0);
	green_join(&g1);
	printf("Thread nr %d done\n", a1);
	green_join(&g2);
	printf("Thread nr %d done\n", a2);
	green_join(&g3);
	printf("Thread nr %d done\n\n", a3);
	//printf("count is %d\n", counter);
	printf("done\n");
	return 0;
}
