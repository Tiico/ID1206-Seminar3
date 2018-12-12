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

struct readyList {
  struct green_t *first;
  struct green_t *last;
};

struct readyList readylist = {NULL};

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

void enqueue(green_t *thread){
  if (readylist.first == NULL) {
    readylist.first = thread;
    readylist.last = thread;
  }else{
    readylist.last->next = thread;
    readylist.last = thread;
  }
}

void condEnqueue(green_cond_t* cond, green_t* thread){
  if (cond->first == NULL) {
    cond->first = thread;
    cond->last = thread;
  }else{
    cond->last->next = thread;
    cond->last = thread;
  }
}

struct green_t* dequeue(){
  if (readylist.first == NULL) {
    printf("No ready threads left, quitting...\n");
    return &main_green;
  }
  green_t *first = readylist.first;

  if (readylist.first->next == NULL) {
    readylist.first = NULL;
    readylist.last = NULL;
    return first;
  }
  readylist.first = readylist.first->next;
  first->next = NULL;
  return first;
}

struct green_t* condDequeue(green_cond_t* cond){
  green_t *current = cond->first;
    if (current != NULL) {
      cond->first = current->next;
      current->next = NULL;
    }

  return current;
}

void green_cond_init(green_cond_t* cond){
  cond->first = NULL;
  cond->last = NULL;
}

void green_cond_wait(green_cond_t* cond){
  sigprocmask(SIG_BLOCK, &block , NULL);
  condEnqueue(cond, running);
  green_t* susp = running;
  running = dequeue(cond);

  printf("%p , %p\n",susp , running );
  swapcontext(susp->context, running->context);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
}

void green_cond_signal(green_cond_t* cond){
  sigprocmask(SIG_BLOCK, &block , NULL);
  green_t* signalled = condDequeue(cond);
  if (signalled != NULL) {
    enqueue(signalled);
  }
  sigprocmask(SIG_UNBLOCK, &block , NULL);
}

void green_thread() {
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  green_t *this = running;

  (*this->fun)(this->arg);

  //place waiting (joining) thread in ready queue
  sigprocmask(SIG_BLOCK, &block , NULL);
  if (this->join != NULL) {
    enqueue(this->join);
  }
  //free allocated memory structure
  free(this->context->uc_stack.ss_sp);
  free(this->context);
  //we're a zombie
  this->zombie = 1;
  //find the next thread to run
  green_t *next = dequeue();

  running = next;
  setcontext(next->context);
}

int green_yield(){
  sigprocmask(SIG_BLOCK, &block , NULL);
  green_t *susp = running;
  //add susp to ready queue
  enqueue(susp);
  //select the next thread for execution
  green_t *next = dequeue();
  if (next == NULL) {
    next = &main_green;
  }

  running = next;
  swapcontext(susp->context, next->context);
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
  thread->join = susp;

  //select the next thread for execution
  green_t *next = dequeue();

  running = next;
  swapcontext(susp->context, next->context);
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
  enqueue(new);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

void mutexEnqueue(green_mutex_t *mutex, green_t *thread){
  if (mutex->susp == NULL) {
    mutex->susp = thread;
  }else{
    green_t *current = mutex->susp;
    while (current->next != NULL) {
        current = current->next;
    }
    current->next = thread;

  }
}

int green_mutex_init(green_mutex_t *mutex){
  mutex->taken = 0;
  mutex->susp = NULL;
}

int green_mutex_lock(green_mutex_t *mutex){
  sigprocmask(SIG_BLOCK, &block , NULL);

  green_t *susp = running;
  while (mutex->taken) {
    mutexEnqueue(mutex, susp);

    green_t* next = dequeue();
    running = next;
    swapcontext(susp->context, next->context);
  }
  mutex->taken = 1;
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

int green_mutex_unlock(green_mutex_t *mutex){
  sigprocmask(SIG_BLOCK, &block , NULL);
  //move suspended threads to ready queue
  green_t *susp_threads = mutex->susp;
  if (susp_threads != NULL) {
    enqueue(susp_threads);
    mutex->susp = mutex->susp->next;
    // susp_threads = susp_threads->next;
    // mutex->susp = NULL;
  }

  // green_t *susp_threads = mutex->susp;
  // while (susp_threads != NULL) {
  //   //printf("Adding to ready queue\n");
  //   enqueue(susp_threads);
  //   susp_threads = susp_threads->next;
  //   printf("next susp_threads: %p\n", susp_threads);
  //   susp_threads->next = NULL;  //Seq fault since susp_threads is null and thus doesn't have a next pointer
  // }
  mutex->taken = 0;

  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

void printReady(){
  printf("\n");
  printf("============ Ready list ============\n");
  green_t *first = readylist.first;
  while (first != NULL) {
    printf(" %p\n", first);
    first = first->next;
  }
  printf("====================================\n");
  printf("\n");
}
