/* GLIB - Library of useful routines for C programming
 * Copyright (C) 1995-1997  Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * GAsyncQueue: thread pool implementation.
 * Copyright (C) 2000 Sebastian Wilhelmi; University of Karlsruhe
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * MT safe
 */

#include "glib.h"

typedef struct _GRealThreadPool GRealThreadPool;

struct _GRealThreadPool
{
  GThreadPool pool;
  GAsyncQueue* queue;
  gint max_threads;
  gint num_threads;
  gboolean running;
  gboolean immediate;
  gboolean waiting;
};

/* The following is just an address to mark the stop order for a
 * thread, it could be any address (as long, as it isn't a valid
 * GThreadPool address) */
static const gpointer stop_this_thread_marker = (gpointer) &g_thread_pool_new;

/* Here all unused threads are waiting, depending on their priority */
static GAsyncQueue *unused_thread_queue[G_THREAD_PRIORITY_URGENT + 1][2];
static gint unused_threads = 0;
static gint max_unused_threads = 0;
G_LOCK_DEFINE_STATIC (unused_threads);

static GMutex *inform_mutex = NULL;
static GCond *inform_cond = NULL;

static void g_thread_pool_free_internal (GRealThreadPool* pool);
static void g_thread_pool_thread_proxy (gpointer data);
static void g_thread_pool_start_thread (GRealThreadPool* pool, GError **error);
static void g_thread_pool_wakeup_and_stop_all (GRealThreadPool* pool);

#define g_thread_should_run(pool, len) \
  ((pool)->running || (!(pool)->immediate && (len) > 0))

static void 
g_thread_pool_thread_proxy (gpointer data)
{
  GRealThreadPool *pool = data;

  g_async_queue_lock (pool->queue);
  while (TRUE)
    {
      gpointer task; 
      gboolean goto_global_pool = 
	!pool->pool.exclusive && pool->pool.stack_size == 0;
      gint len = g_async_queue_length_unlocked (pool->queue);
      
      if (g_thread_should_run (pool, len))
	{
	  task = g_async_queue_pop_unlocked (pool->queue);

	  if (pool->num_threads > pool->max_threads && pool->max_threads != -1)
	    /* We are in fact a superfluous threads, so we go to the
	     * global pool and just hand the data further to the next one
	     * waiting in the queue */
	    {
	      g_async_queue_push_unlocked (pool->queue, task);
	      goto_global_pool = TRUE;
	    }
	  else if (pool->running || !pool->immediate)
	    {
	      g_async_queue_unlock (pool->queue);
	      pool->pool.thread_func (task, pool->pool.user_data);
	      g_async_queue_lock (pool->queue);
	    }

	  len = g_async_queue_length_unlocked (pool->queue);
	}

      if (!g_thread_should_run (pool, len))
	{
	  g_cond_broadcast (inform_cond);
	  goto_global_pool = TRUE;
	}
      else if (len >= 0)
	/* At this pool there is no thread waiting */
	goto_global_pool = FALSE; 
      
      if (goto_global_pool)
	{
	  GAsyncQueue *unused_queue = 
	    unused_thread_queue[pool->pool.priority][pool->pool.bound ? 1 : 0];
	  pool->num_threads--; 

	  if (!pool->running && !pool->waiting)
	    {
	      if (pool->num_threads == 0)
		{
		  g_async_queue_unlock (pool->queue);
		  g_thread_pool_free_internal (pool);
		}		
	      else if (len == - pool->num_threads)
		{
		  g_thread_pool_wakeup_and_stop_all (pool);
		  g_async_queue_unlock (pool->queue);
		}
	    }
	  else
	    g_async_queue_unlock (pool->queue);
	  
	  g_async_queue_lock (unused_queue);

	  G_LOCK (unused_threads);
	  if ((unused_threads >= max_unused_threads && 
	       max_unused_threads != -1) || pool->pool.stack_size != 0)
	    {
	      G_UNLOCK (unused_threads);
	      g_async_queue_unlock (unused_queue);
	      /* Stop this thread */
	      return;      
	    }
	  unused_threads++;
	  G_UNLOCK (unused_threads);

	  pool = g_async_queue_pop_unlocked (unused_queue);

	  G_LOCK (unused_threads);
	  unused_threads--;
	  G_UNLOCK (unused_threads);

	  g_async_queue_unlock (unused_queue);
	  
	  if (pool == stop_this_thread_marker)
	    /* Stop this thread */
	    return;
	  
	  g_async_queue_lock (pool->queue);

	  /* pool->num_threads++ is not done here, but in
           * g_thread_pool_start_thread to make the new started thread
           * known to the pool, before itself can do it. */
	}
    }
}

static void
g_thread_pool_start_thread (GRealThreadPool  *pool, 
			    GError          **error)
{
  gboolean success = FALSE;
  GThreadPriority priority = pool->pool.priority;
  guint bound = pool->pool.bound ? 1 : 0;
  GAsyncQueue *queue = unused_thread_queue[priority][bound];
  
  if (pool->num_threads >= pool->max_threads && pool->max_threads != -1)
    /* Enough threads are already running */
    return;

  g_async_queue_lock (queue);

  if (g_async_queue_length_unlocked (queue) < 0)
    {
      /* First we try a thread with the right priority */
      g_async_queue_push_unlocked (queue, pool);
      success = TRUE;
    }

  g_async_queue_unlock (queue);

  /* We will not search for threads with other priorities, because changing
   * priority is quite unportable */
  
  if (!success)
    {
      GError *local_error = NULL;
      /* No thread was found, we have to start a new one */
      g_thread_create (g_thread_pool_thread_proxy, pool, 
		       pool->pool.stack_size, FALSE, 
		       bound, priority, &local_error);
      
      if (local_error)
	{
	  g_propagate_error (error, local_error);
	  return;
	}
    }

  /* See comment in g_thread_pool_thread_proxy as to why this is done
   * here and not there */
  pool->num_threads++;
}

/**
 * g_thread_pool_new: 
 * @thread_func: a function to execute in the threads of the new thread pool
 * @max_threads: the maximal number of threads to execute concurrently in 
 *   the new thread pool, -1 means no limit
 * @stack_size: the stack size for the threads of the new thread pool,
 *   0 means using the standard
 * @bound: should the threads of the new thread pool be bound?
 * @priority: a priority for the threads of the new thread pool
 * @exclusive: should this thread pool be exclusive?
 * @user_data: user data that is handed over to @thread_func every time it 
 *   is called
 * @error: return location for error
 *
 * This function creates a new thread pool. All threads created within
 * this thread pool will have the priority @priority and the stack
 * size @stack_size and will be bound if and only if @bound is
 * true. 
 *
 * Whenever you call g_thread_pool_push(), either a new thread is
 * created or an unused one is reused. At most @max_threads threads
 * are running concurrently for this thread pool. @max_threads = -1
 * allows unlimited threads to be created for this thread pool. The
 * newly created or reused thread now executes the function
 * @thread_func with the two arguments. The first one is the parameter
 * to g_thread_pool_push() and the second one is @user_data.
 *
 * The parameter @exclusive determines, whether the thread pool owns
 * all threads exclusive or whether the threads are shared
 * globally. If @exclusive is @TRUE, @max_threads threads are started
 * immediately and they will run exclusively for this thread pool until
 * it is destroyed by g_thread_pool_free(). If @exclusive is @FALSE,
 * threads are created, when needed and shared between all
 * non-exclusive thread pools. This implies that @max_threads may not
 * be -1 for exclusive thread pools.
 *
 * Note, that only threads from a thread pool with a @stack_size of 0
 * (which means using the standard stack size) will be globally
 * reused. Threads from a thread pool with a non-zero stack size will
 * stay only in this thread pool until it is freed and can thus not be
 * controlled by the g_thread_pool_set_unused_threads() function.
 *
 * @error can be NULL to ignore errors, or non-NULL to report
 * errors. An error can only occur, when @exclusive is set to @TRUE and
 * not all @max_threads threads could be created.
 *
 * Return value: the new #GThreadPool
 **/
GThreadPool* 
g_thread_pool_new (GFunc            thread_func,
		   gint             max_threads,
		   gulong           stack_size,
		   gboolean         bound,
		   GThreadPriority  priority,
		   gboolean         exclusive,
		   gpointer         user_data,
		   GError         **error)
{
  GRealThreadPool *retval;
  G_LOCK_DEFINE_STATIC (init);

  g_return_val_if_fail (thread_func, NULL);
  g_return_val_if_fail (!exclusive || max_threads != -1, NULL);
  g_return_val_if_fail (max_threads >= -1, NULL);
  g_return_val_if_fail (g_thread_supported (), NULL);

  retval = g_new (GRealThreadPool, 1);

  retval->pool.thread_func = thread_func;
  retval->pool.stack_size = stack_size;
  retval->pool.bound = bound;
  retval->pool.priority = priority;
  retval->pool.exclusive = exclusive;
  retval->pool.user_data = user_data;
  retval->queue = g_async_queue_new ();
  retval->max_threads = max_threads;
  retval->num_threads = 0;
  retval->running = TRUE;

  G_LOCK (init);
  
  if (!inform_mutex)
    {
      inform_mutex = g_mutex_new ();
      inform_cond = g_cond_new ();
      for (priority = G_THREAD_PRIORITY_LOW; 
	   priority < G_THREAD_PRIORITY_URGENT + 1; priority++)
	{
	  unused_thread_queue[priority][0] = g_async_queue_new ();
	  unused_thread_queue[priority][1] = g_async_queue_new ();
	}
    }

  G_UNLOCK (init);

  if (retval->pool.exclusive)
    {
      g_async_queue_lock (retval->queue);
  
      while (retval->num_threads < retval->max_threads)
	{
	  GError *local_error = NULL;
	  g_thread_pool_start_thread (retval, &local_error);
	  if (local_error)
	    {
	      g_propagate_error (error, local_error);
	      break;
	    }
	}

      g_async_queue_unlock (retval->queue);
    }

  return (GThreadPool*) retval;
}

/**
 * g_thread_pool_push:
 * @pool: a #GThreadPool
 * @data: a new task for @pool
 * @error: return location for error
 * 
 * Inserts @data into the list of tasks to be executed by @pool. When
 * the number of currently running threads is lower than the maximal
 * allowed number of threads, a new thread is started (or reused) with
 * the properties given to g_thread_pool_new (). Otherwise @data stays
 * in the queue until a thread in this pool finishes its previous task
 * and processes @data. 
 *
 * @error can be NULL to ignore errors, or non-NULL to report
 * errors. An error can only occur, when a new thread couldn't be
 * created. In that case @data is simply appended to the queue of work
 * to do.  
 **/
void 
g_thread_pool_push (GThreadPool     *pool,
		    gpointer         data,
		    GError         **error)
{
  GRealThreadPool *real = (GRealThreadPool*) pool;

  g_return_if_fail (real);

  g_async_queue_lock (real->queue);
  
  if (!real->running)
    {
      g_async_queue_unlock (real->queue);
      g_return_if_fail (real->running);
    }

  if (g_async_queue_length_unlocked (real->queue) >= 0)
    /* No thread is waiting in the queue */
    g_thread_pool_start_thread (real, error);

  g_async_queue_push_unlocked (real->queue, data);
  g_async_queue_unlock (real->queue);
}

/**
 * g_thread_pool_set_max_threads:
 * @pool: a #GThreadPool
 * @max_threads: a new maximal number of threads for @pool
 * @error: return location for error
 * 
 * Sets the maximal allowed number of threads for @pool. A value of -1
 * means, that the maximal number of threads is unlimited.
 *
 * Setting @max_threads to 0 means stopping all work for @pool. It is
 * effectively frozen until @max_threads is set to a non-zero value
 * again.
 * 
 * A thread is never terminated while calling @thread_func, as
 * supplied by g_thread_pool_new (). Instead the maximal number of
 * threads only has effect for the allocation of new threads in
 * g_thread_pool_push (). A new thread is allocated, whenever the
 * number of currently running threads in @pool is smaller than the
 * maximal number.
 *
 * @error can be NULL to ignore errors, or non-NULL to report
 * errors. An error can only occur, when a new thread couldn't be
 * created. 
 **/
void
g_thread_pool_set_max_threads (GThreadPool     *pool,
			       gint             max_threads,
			       GError         **error)
{
  GRealThreadPool *real = (GRealThreadPool*) pool;
  gint to_start;

  g_return_if_fail (real);
  g_return_if_fail (real->running);
  g_return_if_fail (!real->pool.exclusive || max_threads != -1);
  g_return_if_fail (max_threads >= -1);

  g_async_queue_lock (real->queue);

  real->max_threads = max_threads;
  
  if (pool->exclusive)
    to_start = real->max_threads - real->num_threads;
  else
    to_start = g_async_queue_length_unlocked (real->queue);
  
  for ( ; to_start > 0; to_start--)
    {
      GError *local_error = NULL;
      g_thread_pool_start_thread (real, &local_error);
      if (local_error)
	{
	  g_propagate_error (error, local_error);
	  break;
	}
    }
   
  g_async_queue_unlock (real->queue);
}

/**
 * g_thread_pool_get_max_threads:
 * @pool: a #GThreadPool
 *
 * Returns the maximal number of threads for @pool.
 *
 * Return value: the maximal number of threads
 **/
gint
g_thread_pool_get_max_threads (GThreadPool     *pool)
{
  GRealThreadPool *real = (GRealThreadPool*) pool;
  gint retval;

  g_return_val_if_fail (real, 0);
  g_return_val_if_fail (real->running, 0);

  g_async_queue_lock (real->queue);

  retval = real->max_threads;
    
  g_async_queue_unlock (real->queue);

  return retval;
}

/**
 * g_thread_pool_get_num_threads:
 * @pool: a #GThreadPool
 *
 * Returns the number of threads currently running in @pool.
 *
 * Return value: the number of threads currently running
 **/
guint
g_thread_pool_get_num_threads (GThreadPool     *pool)
{
  GRealThreadPool *real = (GRealThreadPool*) pool;
  guint retval;

  g_return_val_if_fail (real, 0);
  g_return_val_if_fail (real->running, 0);

  g_async_queue_lock (real->queue);

  retval = real->num_threads;
    
  g_async_queue_unlock (real->queue);

  return retval;
}

/**
 * g_thread_pool_unprocessed:
 * @pool: a #GThreadPool
 *
 * Returns the number of tasks still unprocessed in @pool.
 *
 * Return value: the number of unprocessed tasks
 **/
guint
g_thread_pool_unprocessed (GThreadPool     *pool)
{
  GRealThreadPool *real = (GRealThreadPool*) pool;
  gint unprocessed;

  g_return_val_if_fail (real, 0);
  g_return_val_if_fail (real->running, 0);

  unprocessed = g_async_queue_length (real->queue);

  return MAX (unprocessed, 0);
}

/**
 * g_thread_pool_free:
 * @pool: a #GThreadPool
 * @immediate: should @pool shut down immediately?
 * @wait: should the function wait for all tasks to be finished?
 *
 * Frees all resources allocated for @pool.
 *
 * If @immediate is #TRUE, no new task is processed for
 * @pool. Otherwise @pool is not freed before the last task is
 * processed. Note however, that no thread of this pool is
 * interrupted, while processing a task. Instead at least all still
 * running threads can finish their tasks before the @pool is freed.
 *
 * If @wait is #TRUE, the functions does not return before all tasks
 * to be processed (dependent on @immediate, whether all or only the
 * currently running) are ready. Otherwise the function returns immediately.
 *
 * After calling this function @pool must not be used anymore. 
 **/
void
g_thread_pool_free (GThreadPool     *pool,
		    gboolean         immediate,
		    gboolean         wait)
{
  GRealThreadPool *real = (GRealThreadPool*) pool;

  g_return_if_fail (real);
  g_return_if_fail (real->running);
  /* It there's no thread allowed here, there is not much sense in
   * not stopping this pool immediately, when it's not empty */
  g_return_if_fail (immediate || real->max_threads != 0 || 
		    g_async_queue_length (real->queue) == 0);

  g_async_queue_lock (real->queue);

  real->running = FALSE;
  real->immediate = immediate;
  real->waiting = wait;

  if (wait)
    {
      g_mutex_lock (inform_mutex);
      while (g_async_queue_length_unlocked (real->queue) != -real->num_threads)
	{
	  g_async_queue_unlock (real->queue); 
	  g_cond_wait (inform_cond, inform_mutex); 
	  g_async_queue_lock (real->queue); 
	}
      g_mutex_unlock (inform_mutex); 
    }

  if (g_async_queue_length_unlocked (real->queue) == -real->num_threads)
    {
      /* No thread is currently doing something (and nothing is left
       * to process in the queue) */
      if (real->num_threads == 0) /* No threads left, we clean up */
	{
	  g_async_queue_unlock (real->queue);
	  g_thread_pool_free_internal (real);
	  return;
	}

      g_thread_pool_wakeup_and_stop_all (real);
    }
  
  real->waiting = FALSE; /* The last thread should cleanup the pool */
  g_async_queue_unlock (real->queue);
}

static void
g_thread_pool_free_internal (GRealThreadPool* pool)
{
  g_return_if_fail (pool);
  g_return_if_fail (!pool->running);
  g_return_if_fail (pool->num_threads == 0);

  g_async_queue_unref (pool->queue);

  g_free (pool);
}

static void
g_thread_pool_wakeup_and_stop_all (GRealThreadPool* pool)
{
  guint i;
  
  g_return_if_fail (pool);
  g_return_if_fail (!pool->running);
  g_return_if_fail (pool->num_threads != 0);
  g_return_if_fail (g_async_queue_length_unlocked (pool->queue) == 
		    -pool->num_threads);

  pool->immediate = TRUE; 
  for (i = 0; i < pool->num_threads; i++)
    g_async_queue_push_unlocked (pool->queue, GUINT_TO_POINTER (1));
}

/**
 * g_thread_pool_set_max_unused_threads:
 * @max_threads: maximal number of unused threads
 *
 * Sets the maximal number of unused threads to @max_threads. If
 * @max_threads is -1, no limit is imposed on the number of unused
 * threads.
 **/
void
g_thread_pool_set_max_unused_threads (gint max_threads)
{
  g_return_if_fail (max_threads >= -1);  

  G_LOCK (unused_threads);
  
  max_unused_threads = max_threads;

  if (max_unused_threads < unused_threads && max_unused_threads != -1)
    {
      guint close_down_num = unused_threads - max_unused_threads;

      while (close_down_num > 0)
	{
	  GThreadPriority priority;
	  guint bound;

	  guint old_close_down_num = close_down_num;
	  for (priority = G_THREAD_PRIORITY_LOW; 
	       priority < G_THREAD_PRIORITY_URGENT + 1 && close_down_num > 0; 
	       priority++)
	    {
	      for (bound = 0; bound < 2; bound++)
		{
		  GAsyncQueue *queue = unused_thread_queue[priority][bound];
		  g_async_queue_lock (queue);
		  
		  if (g_async_queue_length_unlocked (queue) < 0)
		    {
		      g_async_queue_push_unlocked (queue, 
						   stop_this_thread_marker);
		      close_down_num--;
		    }
		  
		  g_async_queue_unlock (queue);
		}
	    }

	  /* Just to make sure, there are no counting problems */
	  g_assert (old_close_down_num != close_down_num);
	}
    }
    
  G_UNLOCK (unused_threads);
}

/**
 * g_thread_pool_get_max_unused_threads:
 * 
 * Returns the maximal allowed number of unused threads.
 *
 * Return value: the maximal number of unused threads
 **/
gint
g_thread_pool_get_max_unused_threads (void)
{
  gint retval;
  
  G_LOCK (unused_threads);
  retval = max_unused_threads;
  G_UNLOCK (unused_threads);

  return retval;
}

/**
 * g_thread_pool_get_num_unused_threads:
 * 
 * Returns the number of currently unused threads.
 *
 * Return value: the number of currently unused threads
 **/
guint g_thread_pool_get_num_unused_threads (void)
{
  guint retval;
  
  G_LOCK (unused_threads);
  retval = unused_threads;
  G_UNLOCK (unused_threads);

  return retval;
}

/**
 * g_thread_pool_stop_unused_threads:
 * 
 * Stops all currently unused threads. This does not change the
 * maximal number of unused threads. This function can be used to
 * regularly stop all unused threads e.g. from g_timeout_add().
 **/
void g_thread_pool_stop_unused_threads (void)
{ 
  guint oldval = g_thread_pool_get_max_unused_threads ();
  g_thread_pool_set_max_unused_threads (0);
  g_thread_pool_set_max_unused_threads (oldval);
}
