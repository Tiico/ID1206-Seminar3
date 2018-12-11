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
#define PERIOD 10000

static sigset_t block;
static ucontext_t main_cntx = {0};
static green_t main_green = {&main_cntx, NULL, NULL, NULL, NULL, FALSE};

static green_t *running = &main_green;

static void init() __attribute__((constructor));

void timer_handler(int);

struct readyList {
  struct green_t *first;
  struct green_t *last;
};

struct readyList readylist = {NULL};

void enqueue(green_t *thread){
  if (readylist.first == NULL) {
    readylist.first = thread;
    readylist.last = thread;
  }else{
    readylist.last->next = thread;
    readylist.last = thread;
  }
}

struct green_t* dequeue(){
  green_t *next = readylist.first;
  readylist.first = next->next;
  if (next == NULL) {
    next = &main_green;
  }
  return next;
}

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
  green_yield();
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

struct green_t* condDequeue(green_cond_t* cond){
  green_t *next = cond->first;
    if (next != NULL) {
      cond->first = next->next;
    }
  return next;
}

void green_cond_init(green_cond_t* cond){
  cond->first = NULL;
  cond->last = NULL;
}

void green_cond_wait(green_cond_t* cond){
  condEnqueue(cond, running);
  green_t* susp = running;
  running = dequeue(cond);
  sigprocmask(SIG_BLOCK, &block , NULL);
  swapcontext(susp->context, running->context);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
}

void green_cond_signal(green_cond_t* cond){

    green_t* signalled = condDequeue(cond);
    if (signalled != NULL) {
      enqueue(signalled);
    }
  }

void green_thread() {
  green_t *this = running;

  (*this->fun)(this->arg);

  //place waiting (joining) thread in ready queue
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
  sigprocmask(SIG_BLOCK, &block , NULL);
  setcontext(next->context);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
}

int green_yield(){
  green_t *susp = running;
  //add susp to ready queue
  enqueue(susp);
  //select the next thread for execution
  green_t *next = dequeue();
  if (next == NULL) {
    next = &main_green;
  }

  running = next;
  sigprocmask(SIG_BLOCK, &block , NULL);
  swapcontext(susp->context, next->context);
  sigprocmask(SIG_UNBLOCK, &block , NULL);
  return 0;
}

int green_join(green_t *thread){
  if (thread->zombie) {
    return 0;
  }
  green_t *susp = running;
  //add to waiting threads
  thread->join = susp;

  //select the next thread for execution
  green_t *next = dequeue();

  running = next;
  sigprocmask(SIG_BLOCK, &block , NULL);
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
  enqueue(new);
  return 0;
}
