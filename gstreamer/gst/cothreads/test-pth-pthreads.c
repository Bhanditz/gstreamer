#include "pth_p.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include "linuxthreads-internals.h"

#ifndef CURRENT_STACK_FRAME
#define CURRENT_STACK_FRAME  ({ char __csf; &__csf; })
#endif /* CURRENT_STACK_FRAME */

pth_mctx_t main_context;
int threadnum = 0;

void cothread (void *unused)
{
  printf ("1.1: current stack frame: %p\n", CURRENT_STACK_FRAME);
  printf ("1.1: sleeping 2s in thread %d...\n", threadnum);
  sleep (2);
  printf ("1.1: current stack frame: %p\n", CURRENT_STACK_FRAME);
  printf ("1.1: returning to cothread 0\n");
  pth_mctx_restore (&main_context);
}

void pthread (void* unused) 
{
  pth_mctx_t ctx;
  char *skaddr, *stackspace;
  
  printf ("1: saving the main context\n");
  printf ("1: current stack frame: %p\n", CURRENT_STACK_FRAME);
  pth_mctx_save (&main_context);
  
  printf ("1: %d\n", pthread_self());
  
  stackspace = malloc(2 * 1024 * 1024);
  (pthread_descr) stackspace[2 * 1024 * 1024 - sizeof (pthread_descr) -1] = 
    (thread_handle (pthread_self()))->h_descr;
  
  while (1) {
    skaddr = stackspace;
    
    printf ("1: current stack frame: %p\n", CURRENT_STACK_FRAME);
    printf ("1: spawning a new cothread\n");
    pth_mctx_set (&ctx, cothread, skaddr, skaddr + 64 * 1024);
    printf ("1: new thread's stack frame will be in the heap at %p\n", skaddr);
    
    printf ("1: current stack frame: %p\n", CURRENT_STACK_FRAME);
    printf ("1: switching to cothread %d...\n", ++threadnum);
    
    printf ("1: current stack frame: %p\n", CURRENT_STACK_FRAME);
    pth_mctx_switch (&main_context, &ctx);
  
    printf ("1: current stack frame: %p\n", CURRENT_STACK_FRAME);
    printf ("1: back now, looping\n");
  }
}


int main (int argc, char *argv[])
{
  pthread_t tid;

  printf ("0: current stack frame: %p\n", CURRENT_STACK_FRAME);
  printf ("0: creating the pthread\n");
  pthread_create (&tid, NULL, pthread, NULL);
  printf ("0: %d\n", pthread_self());
//  pthread(NULL);
//  printf ("joining the pthread\n");
  pthread_join (tid, NULL);

  printf ("0: current stack frame: %p\n", CURRENT_STACK_FRAME);
  printf ("0: take five...\n");
  sleep(5);

  printf ("0 current stack frame: %p\n", CURRENT_STACK_FRAME);
  printf ("exiting\n");
  
  exit (0);
}

