/* GStreamer
 *
 * Common code for GStreamer unittests
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#ifndef __GST_CHECK_H__
#define __GST_CHECK_H__

#include <signal.h>
#include <string.h>
#include <stdlib.h>

#include <check.h>

#include <gst/gst.h>

/* logging function for tests
 * a test uses g_message() to log a debug line
 * a gst unit test can be run with GST_TEST_DEBUG env var set to see the
 * messages
 */
extern gboolean _gst_check_threads_running;
extern gboolean _gst_check_raised_critical;
extern gboolean _gst_check_expecting_log;

void gst_check_init (int *argc, char **argv[]);

/***
 * thread test macros and variables
 */
extern GList *thread_list;
extern GMutex *mutex;
extern GCond *start_cond;	/* used to notify main thread of thread startups */
extern GCond *sync_cond;	/* used to synchronize all threads and main thread */

#define MAIN_START_THREADS(count, function, data)		\
MAIN_INIT();							\
MAIN_START_THREAD_FUNCTIONS(count, function, data);		\
MAIN_SYNCHRONIZE();

#define MAIN_INIT()			\
G_STMT_START {				\
  _gst_check_threads_running = TRUE;	\
					\
  mutex = g_mutex_new ();		\
  start_cond = g_cond_new ();		\
  sync_cond = g_cond_new ();		\
} G_STMT_END;

#define MAIN_START_THREAD_FUNCTIONS(count, function, data)	\
G_STMT_START {							\
  int i;							\
  for (i = 0; i < count; ++i) {					\
    MAIN_START_THREAD_FUNCTION (i, function, data);		\
  }								\
} G_STMT_END;

#define MAIN_START_THREAD_FUNCTION(i, function, data)		\
G_STMT_START {							\
    GThread *thread = NULL;					\
    g_message ("MAIN: creating thread %d\n", i);		\
    g_mutex_lock (mutex);					\
    thread = g_thread_create ((GThreadFunc) function, data,	\
	TRUE, NULL);						\
    /* wait for thread to signal us that it's ready */		\
    g_message ("MAIN: waiting for thread %d\n", i);		\
    g_cond_wait (start_cond, mutex);				\
    g_mutex_unlock (mutex);					\
								\
    thread_list = g_list_append (thread_list, thread);		\
} G_STMT_END;


#define MAIN_SYNCHRONIZE()		\
G_STMT_START {				\
  g_message ("MAIN: synchronizing\n");	\
  g_cond_broadcast (sync_cond);		\
  g_message ("MAIN: synchronized\n");	\
} G_STMT_END;

#define MAIN_STOP_THREADS()					\
G_STMT_START {							\
  _gst_check_threads_running = FALSE;				\
								\
  /* join all threads */					\
  g_message ("MAIN: joining\n");				\
  g_list_foreach (thread_list, (GFunc) g_thread_join, NULL);	\
  g_message ("MAIN: joined\n");					\
} G_STMT_END;

#define THREAD_START()						\
THREAD_STARTED();						\
THREAD_SYNCHRONIZE();

#define THREAD_STARTED()					\
G_STMT_START {							\
  /* signal main thread that we started */			\
  g_message ("THREAD %p: started\n", g_thread_self ());		\
  g_mutex_lock (mutex);						\
  g_cond_signal (start_cond);					\
} G_STMT_END;

#define THREAD_SYNCHRONIZE()					\
G_STMT_START {							\
  /* synchronize everyone */					\
  g_message ("THREAD %p: syncing\n", g_thread_self ());		\
  g_cond_wait (sync_cond, mutex);				\
  g_message ("THREAD %p: synced\n", g_thread_self ());		\
  g_mutex_unlock (mutex);					\
} G_STMT_END;

#define THREAD_SWITCH()						\
G_STMT_START {							\
  /* a minimal sleep is a context switch */			\
  g_usleep (1);							\
} G_STMT_END;

#define THREAD_TEST_RUNNING()	(_gst_check_threads_running == TRUE)

#define ASSERT_CRITICAL(code)					\
G_STMT_START {							\
  _gst_check_expecting_log = TRUE;				\
  _gst_check_raised_critical = FALSE;				\
  code;								\
  _fail_unless (_gst_check_raised_critical, __FILE__, __LINE__, \
                "Expected g_critical, got nothing: '"#code"'"); \
  _gst_check_expecting_log = FALSE;				\
} G_STMT_END


#endif /* __GST_CHECK_H__ */

