#include <stdlib.h>
#include <ucontext.h>
#include <stdio.h>
#include <assert.h>
#include <signal.h>
#include <sys/time.h>
#include "green.h"

#define FALSE 0
#define TRUE 1
#define STACK_SIZE 4096
#define PERIOD 100

static sigset_t block;
static ucontext_t main_cntx = {0};
static green_t main_green = {&main_cntx, NULL, NULL, NULL, NULL, FALSE};
static green_t *running = &main_green;

static void init() __attribute__((constructor));

void timer_handler(int sig);

void init(){
  getcontext(&main_cntx);
	sigemptyset(&block);
	sigaddset(&block,SIGVTALRM);

	struct sigaction act = {0};
	struct timeval interval;
	struct itimerval period;

	act.sa_handler = timer_handler;
	assert(sigaction(SIGVTALRM,&act,NULL)==0);

	interval.tv_sec=0;
	interval.tv_usec = PERIOD;
	period.it_interval=interval;
	period.it_value = interval;
	setitimer(ITIMER_VIRTUAL,&period,NULL);
}

void timer_handler(int sig){
  //printf("interrupt!\n");
  green_yield();
}

void runNext(){
    dequeue(&running);
}

void enqueue(green_t **queue, green_t *thread){
  green_t *currentQ = *queue;
  if (currentQ == NULL) {
    *queue = thread;
  }else{
    while(currentQ->next != NULL){
      currentQ = currentQ->next;
    }
    currentQ->next = thread;
  }
}

struct green_t* dequeue(green_t **queue){
  green_t *dequeued = *queue;
  if (dequeued != NULL) {
    *queue = dequeued->next;
    dequeued->next = NULL;
  }
  return dequeued;
}

void green_cond_init(green_cond_t* cond){
  cond->suspThreads = NULL;
}


int green_cond_wait(green_cond_t* cond, green_mutex_t* mutex){
  sigprocmask(SIG_BLOCK, &block , NULL);
  green_t *susp = running;
  enqueue(&(cond->suspThreads), susp);
  //printf("reee\n" );

  if(mutex != NULL) {
    mutex->taken = 0;
    enqueue(&running, mutex->susp);
    mutex->susp = NULL;
  }
  runNext();
  swapcontext(susp->context, running->context);

  if (mutex != NULL) {
    while (mutex->taken) { //true: suspending, false: takes lock
      green_t* susp = running;
      enqueue(&(mutex->susp), susp);
      runNext();
      swapcontext(susp->context, running->context);
    }
    mutex->taken = 1;
  }
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

void green_cond_signal(green_cond_t* cond){
  sigprocmask(SIG_BLOCK, &block , NULL);
  green_t* signalled = dequeue(&cond->suspThreads);
  enqueue(&running, signalled);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
}

void green_thread() {
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  green_t *this = running;

  (*this->fun)(this->arg);

  //place waiting (joining) thread in ready queue
  sigprocmask(SIG_BLOCK, &block , NULL);
  if (this->join != NULL) {
    enqueue(&running, this->join);
  }
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  //free allocated memory structure
  free(this->context->uc_stack.ss_sp);
  free(this->context);
  //we're a zombie
  this->zombie = 1;
  //find the next thread to run
  sigprocmask(SIG_BLOCK, &block , NULL);
  runNext();
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  setcontext(running->context);
}

int green_yield(){
  sigprocmask(SIG_BLOCK, &block , NULL);
  green_t *susp = running;
  //add susp to ready queue
  enqueue(&running, susp);
  //select the next thread for execution
  runNext();

  swapcontext(susp->context, running->context);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

int green_join(green_t *thread){
  if (thread->zombie) {
    return 0;
  }
  green_t *susp = running;
  sigprocmask(SIG_BLOCK, &block , NULL);
  //add to waiting threads
  if (thread->join == NULL) {
    thread->join = susp;
  }else{
    green_t *waitingThread = thread->join;
      while (waitingThread->next != NULL) {
        waitingThread = waitingThread->next;
      }
      waitingThread->next = susp;
  }

  //select the next thread for execution
  runNext();

  swapcontext(susp->context, running->context);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  //SUSP RESUME
  return 0;
}

int green_create(green_t *new, void *(*fun)(void*), void *arg){
  ucontext_t *cntx = (ucontext_t *)malloc(sizeof(ucontext_t));
  getcontext(cntx);

  void *stack = malloc(STACK_SIZE);

  cntx->uc_stack.ss_sp = stack;
  cntx->uc_stack.ss_size = STACK_SIZE;

  makecontext(cntx, green_thread, 0);
  new->context = cntx;
  new->fun = fun;
  new->arg = arg;
  new->next = NULL;
  new->join = NULL;
  new->zombie = FALSE;

  //TODO: add new to ready queue
  sigprocmask(SIG_BLOCK, &block , NULL);
  enqueue(&running, new);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

int green_mutex_init(green_mutex_t *mutex){
  mutex->taken = 0;
  mutex->susp = NULL;
}

int green_mutex_lock(green_mutex_t *mutex){
  sigprocmask(SIG_BLOCK, &block , NULL);
  green_t *susp = running;
  while (mutex->taken) {
    enqueue(&(mutex->susp), susp);
    runNext();
    swapcontext(susp->context, running->context);
  }
  mutex->taken = 1;
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

int green_mutex_unlock(green_mutex_t *mutex){
  sigprocmask(SIG_BLOCK, &block , NULL);
  //move suspended threads to ready queue
  enqueue(&running, mutex->susp);
  mutex->susp = NULL;
  mutex->taken = 0;

  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

void printReady(){
  printf("\n");
  printf("============ Ready list ============\n");
  int i = 0;
  green_t *first = running;
  while (first != NULL) {
    printf("[%d]: %p\n",i++, first);
    first = running->next;
  }
  printf("====================================\n");
  printf("\n");
}

// void printMutex(green_mutex_t* mutex){
//   printf("\n");
//   printf("============ Mutex list ============\n");
//   int i = 0;
//
//   green_t *first = mutex->susp;
//   while (first != NULL) {
//     printf("[%d]: %p\n",i++, first);
//     first = first->next;
//   }
//   printf("====================================\n");
//   printf("\n");
// }
//
// void printCond(green_cond_t* cond){
//   printf("\n");
//   printf("============ Cond List  ============\n");
//   int i = 0;
//   green_t *first = cond->first;
//   while (first != NULL) {
//     printf("[%d]: %p\n",i++, first);
//     first = first->next;
//   }
//   printf("====================================\n");
//   printf("\n");
// }
