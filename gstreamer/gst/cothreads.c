/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * cothreads.c: Cothreading routines
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "gst_private.h"

#include "cothreads.h"
#include "gstarch.h"
#include "gstlog.h"
#include "gstutils.h"


#define STACK_SIZE 0x200000

#define COTHREAD_MAGIC_NUMBER 0xabcdef

#define COTHREAD_MAXTHREADS 16
#define COTHREAD_STACKSIZE (STACK_SIZE/COTHREAD_MAXTHREADS)

static void 	cothread_destroy 	(cothread_state *thread);

struct _cothread_context
{
  cothread_state *threads[COTHREAD_MAXTHREADS];
  int nthreads;
  int current;
  GHashTable *data;
};


static pthread_key_t _cothread_key = -1;

/* Disabling this define allows you to shut off a few checks in
 * cothread_switch.  This likely will speed things up fractionally */
/* #define COTHREAD_PARANOID */

/**
 * cothread_context_init:
 *
 * Create and initialize a new cothread context 
 *
 * Returns: the new cothread context
 */
cothread_context *
cothread_context_init (void)
{
  cothread_context *ctx = (cothread_context *) g_malloc (sizeof (cothread_context));

  /* we consider the initiating process to be cothread 0 */
  ctx->nthreads = 1;
  ctx->current = 0;
  ctx->data = g_hash_table_new (g_str_hash, g_str_equal);

  GST_INFO (GST_CAT_COTHREADS, "initializing cothreads");

  if (_cothread_key == (pthread_key_t)-1) {
    if (pthread_key_create (&_cothread_key, NULL) != 0) {
      perror ("pthread_key_create");
      return NULL;
    }
  }
  pthread_setspecific (_cothread_key, ctx);

  memset (ctx->threads, 0, sizeof (ctx->threads));

  ctx->threads[0] = (cothread_state *) g_malloc0 (sizeof (cothread_state));
  ctx->threads[0]->ctx = ctx;
  ctx->threads[0]->threadnum = 0;
  ctx->threads[0]->func = NULL;
  ctx->threads[0]->argc = 0;
  ctx->threads[0]->argv = NULL;
  ctx->threads[0]->priv = NULL;
  ctx->threads[0]->flags = COTHREAD_STARTED;
  ctx->threads[0]->sp = (void *) CURRENT_STACK_FRAME;
  ctx->threads[0]->pc = 0;

  GST_INFO (GST_CAT_COTHREADS, "0th thread is %p at sp:%p", ctx->threads[0], ctx->threads[0]->sp);

  return ctx;
}

/**
 * cothread_context_free:
 * @ctx: the cothread context to free
 *
 * Free the cothread context.
 */
void
cothread_context_free (cothread_context *ctx)
{
  gint i;

  g_return_if_fail (ctx != NULL);

  GST_INFO (GST_CAT_COTHREADS, "free cothread context");

  for (i = 0; i < COTHREAD_MAXTHREADS; i++) {
    if (ctx->threads[i]) {
      cothread_destroy (ctx->threads[i]);
    }
  }
  g_hash_table_destroy (ctx->data);
  g_free (ctx);
}

/**
 * cothread_create:
 * @ctx: the cothread context
 *
 * Create a new cothread state in the given context
 *
 * Returns: the new cothread state or NULL on error
 */
cothread_state*
cothread_create (cothread_context *ctx)
{
  cothread_state *thread;
  void *sp;
  void *mmaped = 0;
  guchar *stack_end;
  gint slot = 0;

  g_return_val_if_fail (ctx != NULL, NULL);

  if (ctx->nthreads == COTHREAD_MAXTHREADS) {
    /* this is pretty fatal */
    g_warning ("cothread_create: attempt to create > COTHREAD_MAXTHREADS\n");
    return NULL;
  }
  /* find a free spot in the stack, note slot 0 has the main thread */
  for (slot = 1; slot < ctx->nthreads; slot++) {
    if (ctx->threads[slot] == NULL)
      break;
    else if (ctx->threads[slot]->flags & COTHREAD_DESTROYED &&
		    slot != ctx->current) {
      cothread_destroy (ctx->threads[slot]);
      break;
    }
  }

  GST_DEBUG(GST_CAT_COTHREADS, "Found free cothread slot %d", slot);

  sp = CURRENT_STACK_FRAME;
  /* FIXME this may not be 64bit clean
   *       could use casts to uintptr_t from inttypes.h
   *       if only all platforms had inttypes.h
   */
  stack_end = (guchar *) ((gulong) sp & ~(STACK_SIZE - 1));

  thread = (cothread_state *) (stack_end + ((slot - 1) * COTHREAD_STACKSIZE));
  GST_DEBUG (GST_CAT_COTHREADS, 
             "mmap   cothread slot stack from %p to %p (size 0x%lx)", 
	     thread, thread + COTHREAD_STACKSIZE, 
	     (long) COTHREAD_STACKSIZE);

  GST_DEBUG (GST_CAT_COTHREADS, "going into mmap");
  /* the mmap is used to reserve part of the stack
   * ie. we state explicitly that we are going to use it */
  mmaped = mmap ((void *) thread, COTHREAD_STACKSIZE,
	          PROT_READ | PROT_WRITE | PROT_EXEC, 
		  MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  GST_DEBUG (GST_CAT_COTHREADS, "coming out of mmap");
  if (mmaped == MAP_FAILED) {
    perror ("mmap'ing cothread stack space");
    return NULL;
  }
  if (mmaped != thread) {
    g_warning ("could not mmap requested memory");
    return NULL;
  }

  thread->magic_number = COTHREAD_MAGIC_NUMBER;
  GST_DEBUG (GST_CAT_COTHREADS, "create  cothread %d with magic number 0x%x",
             slot, thread->magic_number);
  thread->ctx = ctx;
  thread->threadnum = slot;
  thread->flags = 0;
  thread->priv = NULL;
  thread->sp = ((guchar *) thread + COTHREAD_STACKSIZE);
  thread->top_sp = thread->sp; /* for debugging purposes to detect stack overruns */

  GST_INFO (GST_CAT_COTHREADS, 
            "created cothread #%d in slot %d: %p at sp:%p", 
	    ctx->nthreads, slot, thread, thread->sp);

  ctx->threads[slot] = thread;
  ctx->nthreads++;

  return thread;
}

/**
 * cothread_free:
 * @thread: the cothread state
 *
 * Free the given cothread state
 */
void
cothread_free (cothread_state *thread)
{
  g_return_if_fail (thread != NULL);

  GST_INFO (GST_CAT_COTHREADS, "flag cothread %d for destruction", thread->threadnum);

  /* we simply flag the cothread for destruction here */
  thread->flags |= COTHREAD_DESTROYED;
}

static void
cothread_destroy (cothread_state *thread)
{
  cothread_context *ctx;
  gint threadnum;

  g_return_if_fail (thread != NULL);

  threadnum = thread->threadnum;
  ctx = thread->ctx;

  GST_INFO (GST_CAT_COTHREADS, "destroy cothread %d %p %d", threadnum, thread, ctx->current);

  /* we have to unlock here because we might be switched out with the lock held */
  cothread_unlock (thread);

  if (threadnum == 0) 
  {
    GST_INFO (GST_CAT_COTHREADS,
	      "trying to destroy cothread 0 with %d cothreads left", 
	     ctx->nthreads);
    if (ctx->nthreads > 1)
    {
      /* we're trying to destroy cothread 0 when there are still cothreads
       * active, so kill those first */
      int i;

      for (i = 1; i < COTHREAD_MAXTHREADS; ++i)
      {
	if (ctx->threads[i] != NULL)
	{
	  cothread_destroy (ctx->threads[i]);
	  GST_INFO (GST_CAT_COTHREADS,
	            "destroyed cothread %d, %d cothreads left\n", 
		    i, ctx->nthreads);
	}
      }
    }
    g_assert (ctx->nthreads == 1);
    g_free (thread);
  }
  else {
  /*  int res; 
   *  Replaced with version below until cothreads issues solved */
   int res = 0;
    
    /* doing cleanups of the cothread create */
    GST_DEBUG (GST_CAT_COTHREADS, "destroy cothread %d with magic number 0x%x",
             threadnum, thread->magic_number);
    g_assert (thread->magic_number == COTHREAD_MAGIC_NUMBER);

    g_assert (thread->priv == NULL);

    GST_DEBUG (GST_CAT_COTHREADS, 
               "munmap cothread slot stack from %p to %p (size 0x%lx)", 
  	       thread, thread + COTHREAD_STACKSIZE, 
	       (long) COTHREAD_STACKSIZE);
/*    res = munmap (thread, COTHREAD_STACKSIZE);
 *    Commented out waiting for resolution for cothread issue */
    if (res != 0)
    {
      switch (res)
      {
	case EINVAL:
	  g_warning ("munmap doesn't like start %p or length %d\n",
	             thread, COTHREAD_STACKSIZE);
	  break;
	default:
	  g_warning ("Thomas was too lazy to check for all errors, so I can't tell you what is wrong.\n");
	  break;
      }
    }
  }
  GST_DEBUG (GST_CAT_COTHREADS, "munmap done\n");

  ctx->threads[threadnum] = NULL;
  ctx->nthreads--;
}

/**
 * cothread_setfunc:
 * @thread: the cothread state
 * @func: the function to call
 * @argc: argument count for the cothread function
 * @argv: arguments for the cothread function
 *
 * Set the cothread function
 */
void
cothread_setfunc (cothread_state * thread, cothread_func func, int argc, char **argv)
{
  thread->func = func;
  thread->argc = argc;
  thread->argv = argv;
  thread->pc = (void *) func;
}

/**
 * cothread_stop:
 * @thread: the cothread to stop
 *
 * Stop the cothread and reset the stack and program counter.
 */
void
cothread_stop (cothread_state * thread)
{
  thread->flags &= ~COTHREAD_STARTED;
  thread->pc = 0;
  thread->sp = thread->top_sp;
}

/**
 * cothread_main:
 * @ctx: cothread context to find main thread of
 *
 * Get the main thread.
 *
 * Returns: the #cothread_state of the main (0th) thread
 */
cothread_state *
cothread_main (cothread_context * ctx)
{
  GST_DEBUG (GST_CAT_COTHREADS, "returning %p, the 0th cothread", ctx->threads[0]);
  return ctx->threads[0];
}

/**
 * cothread_current_main:
 *
 * Get the main thread in the current pthread.
 *
 * Returns: the #cothread_state of the main (0th) thread in the current pthread
 */
cothread_state *
cothread_current_main (void)
{
  cothread_context *ctx = pthread_getspecific (_cothread_key);

  return ctx->threads[0];
}

/**
 * cothread_current:
 *
 * Get the currenttly executing cothread
 *
 * Returns: the #cothread_state of the current cothread
 */
cothread_state *
cothread_current (void)
{
  cothread_context *ctx = pthread_getspecific (_cothread_key);

  return ctx->threads[ctx->current];
}

static void
cothread_stub (void)
{
  cothread_context *ctx = pthread_getspecific (_cothread_key);
  register cothread_state *thread = ctx->threads[ctx->current];

  GST_DEBUG_ENTER ("");

  thread->flags |= COTHREAD_STARTED;

  while (TRUE) {
    thread->func (thread->argc, thread->argv);
    /* we do this to avoid ever returning, we just switch to 0th thread */
    cothread_switch (cothread_main (ctx));
  }
  GST_DEBUG_LEAVE ("");
}

/**
 * cothread_getcurrent:
 *
 * Get the current cothread id
 *
 * Returns: the current cothread id
 */
int cothread_getcurrent (void) __attribute__ ((no_instrument_function));
int
cothread_getcurrent (void)
{
  cothread_context *ctx = pthread_getspecific (_cothread_key);

  if (!ctx)
    return -1;
  return ctx->current;
}

/**
 * cothread_set_private:
 * @thread: the cothread state
 * @data: the data
 *
 * set private data for the cothread.
 */
void
cothread_set_private (cothread_state *thread, gpointer data)
{
  thread->priv = data;
}

/**
 * cothread_context_set_data:
 * @thread: the cothread state
 * @key: a key for the data
 * @data: the data
 *
 * adds data to a cothread
 */
void
cothread_context_set_data (cothread_state *thread, gchar *key, gpointer data)
{
  cothread_context *ctx = pthread_getspecific (_cothread_key);

  g_hash_table_insert (ctx->data, key, data);
}

/**
 * cothread_get_private:
 * @thread: the cothread state
 *
 * get the private data from the cothread
 *
 * Returns: the private data of the cothread
 */
gpointer
cothread_get_private (cothread_state *thread)
{
  return thread->priv;
}

/**
 * cothread_context_get_data:
 * @thread: the cothread state
 * @key: a key for the data
 *
 * get data from the cothread
 *
 * Returns: the data associated with the key
 */
gpointer
cothread_context_get_data (cothread_state * thread, gchar * key)
{
  cothread_context *ctx = pthread_getspecific (_cothread_key);

  return g_hash_table_lookup (ctx->data, key);
}

/**
 * cothreads_stackquery:
 * @stack: Will be set to point to the allocated stack location
 * @stacksize: Will be set to the size of the allocated stack
 *
 *  Returns: #TRUE on success, #FALSE otherwise.
 */
gboolean
cothread_stackquery (void **stack, glong* stacksize)
{
  /* wingo says: use posix_memalign to allocate a 2M-aligned, 2M stack */

#ifdef HAVE_POSIX_MEMALIGN
  int retval = posix_memalign (stack, STACK_SIZE, STACK_SIZE);
  if (retval != 0)
  {
    g_warning ("Could not posix_memalign stack !\n");
    if (retval == EINVAL)
      g_warning ("The alignment parameter %d was not a power of two !\n",
	         STACK_SIZE);
    if (retval == ENOMEM)
      g_warning ("Insufficient memory to allocate the request of %d !\n",
	         STACK_SIZE);
    *stacksize = 0;
    return FALSE;
  }
  GST_DEBUG (GST_CAT_THREAD, "have  posix_memalign at %p of size %d",
           (void *) *stack, STACK_SIZE);
#else
  if ((*stack = valloc (STACK_SIZE)) != 0)
  {
    g_warning ("Could not valloc stack !\n");
    return FALSE;
  }
  GST_DEBUG (GST_CAT_THREAD, "have  valloc at %p of size %d",
           (void *) *stack, STACK_SIZE);
#endif

  GST_DEBUG (GST_CAT_COTHREADS, 
             "Got new cothread stack from %p to %p (size %ld)",
             *stack, *stack + STACK_SIZE - 1, (long) STACK_SIZE);
  *stacksize = STACK_SIZE;
  return TRUE;
}

/**
 * cothread_switch:
 * @thread: cothread state to switch to
 *
 * Switches to the given cothread state
 */
void
cothread_switch (cothread_state * thread)
{
  cothread_context *ctx;
  cothread_state *current;
  int enter;

#ifdef COTHREAD_PARANOID
  if (thread == NULL)
    goto nothread;
#endif
  ctx = thread->ctx;
#ifdef COTHREAD_PARANOID
  if (ctx == NULL)
    goto nocontext;
#endif

  current = ctx->threads[ctx->current];
#ifdef COTHREAD_PARANOID
  if (current == NULL)
    goto nocurrent;
#endif
  if (current == thread)
    goto selfswitch;


  /* find the number of the thread to switch to */
  GST_INFO (GST_CAT_COTHREAD_SWITCH, "switching from cothread #%d to cothread #%d",
	    ctx->current, thread->threadnum);
  ctx->current = thread->threadnum;

  /* save the current stack pointer, frame pointer, and pc */
#ifdef GST_ARCH_PRESETJMP
  GST_ARCH_PRESETJMP ();
#endif
  enter = setjmp (current->jmp);
  if (enter != 0) {
    GST_DEBUG (GST_CAT_COTHREADS, "enter thread #%d %d %p<->%p (%d) %p", current->threadnum, enter,
	       current->sp, current->top_sp, (char*)current->top_sp - (char*)current->sp, current->jmp);
    return;
  }
  GST_DEBUG (GST_CAT_COTHREADS, "exit thread #%d %d %p<->%p (%d) %p", current->threadnum, enter,
	       current->sp, current->top_sp, (char*)current->top_sp - (char*)current->sp, current->jmp);
  enter = 1;

  if (current->flags & COTHREAD_DESTROYED) {
    cothread_destroy (current);
  }

  GST_DEBUG (GST_CAT_COTHREADS, "set stack to %p", thread->sp);
  /* restore stack pointer and other stuff of new cothread */
  if (thread->flags & COTHREAD_STARTED) {
    GST_DEBUG (GST_CAT_COTHREADS, "in thread %p", thread->jmp);
    /* switch to it */
    longjmp (thread->jmp, 1);
  }
  else {
    GST_ARCH_SETUP_STACK ((char*)thread->sp);
    GST_ARCH_SET_SP (thread->sp);
    /* start it */
    GST_ARCH_CALL (cothread_stub);
    GST_DEBUG (GST_CAT_COTHREADS, "exit thread ");
    ctx->current = 0;
  }

  return;

#ifdef COTHREAD_PARANOID
nothread:
  g_warning ("cothread: can't switch to NULL cothread!\n");
  return;
nocontext:
  g_warning ("cothread: there's no context, help!\n");
  exit (2);
nocurrent:
  g_warning ("cothread: there's no current thread, help!\n");
  exit (2);
#endif /* COTHREAD_PARANOID */
selfswitch:
  g_warning ("cothread: trying to switch to same thread, legal but not necessary\n");
  return;
}

/**
 * cothread_lock:
 * @thread: cothread state to lock
 *
 * Locks the cothread state.
 */
void
cothread_lock (cothread_state * thread)
{
}

/**
 * cothread_trylock:
 * @thread: cothread state to try to lock
 *
 * Try to lock the cothread state
 *
 * Returns: TRUE if the cothread could be locked.
 */
gboolean
cothread_trylock (cothread_state * thread)
{
  return TRUE;
}

/**
 * cothread_unlock:
 * @thread: cothread state to unlock
 *
 * Unlock the cothread state.
 */
void
cothread_unlock (cothread_state * thread)
{
}
