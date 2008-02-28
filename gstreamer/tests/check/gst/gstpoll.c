/* GStreamer
 *
 * unit test for GstPoll
 *
 * Copyright (C) <2007> Peter Kjellerstedt <pkj@axis.com>
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

#include <unistd.h>
#include <gst/check/gstcheck.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#define EINPROGRESS WSAEINPROGRESS
#else
#include <sys/socket.h>
#endif

static void
test_poll_wait (GstPollMode mode)
{
  GstPoll *set;
  GstPollFD rfd = { 0, };
  GstPollFD wfd = { 0, };
  gint socks[2];
  guchar c = 'A';

  set = gst_poll_new (mode, FALSE);
  fail_if (set == NULL, "Failed to create a GstPoll");

#ifdef G_OS_WIN32
  fail_if (_pipe (socks, 4096, _O_BINARY) < 0, "Could not create a pipe");
#else
  fail_if (socketpair (PF_UNIX, SOCK_STREAM, 0, socks) < 0,
      "Could not create a pipe");
#endif
  rfd.fd = socks[0];
  wfd.fd = socks[1];

  fail_unless (gst_poll_add_fd (set, &rfd), "Could not add read descriptor");
  fail_unless (gst_poll_fd_ctl_read (set, &rfd, TRUE),
      "Could not mark the descriptor as readable");

  fail_unless (write (wfd.fd, &c, 1) == 1, "write() failed");

  fail_unless (gst_poll_wait (set, GST_CLOCK_TIME_NONE) == 1,
      "One descriptor should be available");
  fail_unless (gst_poll_fd_can_read (set, &rfd),
      "Read descriptor should be readable");
  fail_if (gst_poll_fd_can_write (set, &rfd),
      "Read descriptor should not be writeable");

  fail_unless (gst_poll_add_fd (set, &wfd), "Could not add write descriptor");
  fail_unless (gst_poll_fd_ctl_write (set, &wfd, TRUE),
      "Could not mark the descriptor as writeable");

  fail_unless (gst_poll_wait (set, GST_CLOCK_TIME_NONE) == 2,
      "Two descriptors should be available");
  fail_unless (gst_poll_fd_can_read (set, &rfd),
      "Read descriptor should be readable");
  fail_if (gst_poll_fd_can_write (set, &rfd),
      "Read descriptor should not be writeable");
  fail_if (gst_poll_fd_can_read (set, &wfd),
      "Write descriptor should not be readable");
  fail_unless (gst_poll_fd_can_write (set, &wfd),
      "Write descriptor should be writeable");

  fail_unless (read (rfd.fd, &c, 1) == 1, "read() failed");

  fail_unless (gst_poll_wait (set, GST_CLOCK_TIME_NONE) == 1,
      "One descriptor should be available");
  fail_if (gst_poll_fd_can_read (set, &rfd),
      "Read descriptor should not be readable");
  fail_if (gst_poll_fd_can_write (set, &rfd),
      "Read descriptor should not be writeable");
  fail_if (gst_poll_fd_can_read (set, &wfd),
      "Write descriptor should not be readable");
  fail_unless (gst_poll_fd_can_write (set, &wfd),
      "Write descriptor should be writeable");

  gst_poll_free (set);
  close (socks[0]);
  close (socks[1]);
}

GST_START_TEST (test_poll_basic)
{
  GstPoll *set;
  GstPollFD fd = { 0, };

  fd.fd = 1;

  set = gst_poll_new (GST_POLL_MODE_AUTO, FALSE);
  fail_if (set == NULL, "Failed to create a GstPoll");
  fail_unless (gst_poll_get_mode (set) == GST_POLL_MODE_AUTO,
      "Mode should have been GST_POLL_MODE_AUTO");

  gst_poll_set_mode (set, GST_POLL_MODE_SELECT);
  fail_unless (gst_poll_get_mode (set) == GST_POLL_MODE_SELECT,
      "Mode should have been GST_POLL_MODE_SELECT");

  fail_unless (gst_poll_add_fd (set, &fd), "Could not add descriptor");
  fail_unless (gst_poll_fd_ctl_write (set, &fd, TRUE),
      "Could not mark the descriptor as writeable");
  fail_unless (gst_poll_fd_ctl_read (set, &fd, TRUE),
      "Could not mark the descriptor as readable");
  fail_if (gst_poll_fd_has_closed (set, &fd),
      "Descriptor should not be closed");
  fail_if (gst_poll_fd_has_error (set, &fd),
      "Descriptor should not have an error");
  fail_if (gst_poll_fd_can_write (set, &fd),
      "Descriptor should not be writeable");
  fail_if (gst_poll_fd_can_read (set, &fd),
      "Descriptor should not be readable");
  fail_unless (gst_poll_remove_fd (set, &fd), "Could not remove descriptor");

  fail_if (gst_poll_remove_fd (set, &fd),
      "Could remove already removed descriptor");

  fail_unless (gst_poll_wait (set, 50 * GST_MSECOND) == 0,
      "Waiting did not timeout");

  gst_poll_free (set);

  set = gst_poll_new (GST_POLL_MODE_AUTO, TRUE);
  fail_if (set == NULL, "Failed to create a GstPoll");
  gst_poll_set_flushing (set, TRUE);
  gst_poll_free (set);
}

GST_END_TEST;

static gpointer
delayed_stop (gpointer data)
{
  GstPoll *set = data;

  THREAD_START ();

  g_usleep (500000);

  gst_poll_set_flushing (set, TRUE);

  return NULL;
}

GST_START_TEST (test_poll_wait_stop)
{
  GstPoll *set;

  set = gst_poll_new (GST_POLL_MODE_AUTO, TRUE);
  fail_if (set == NULL, "Failed to create a GstPoll");

  MAIN_START_THREADS (1, delayed_stop, set);

  fail_unless (gst_poll_wait (set, GST_SECOND) != 0, "Waiting timed out");

  MAIN_STOP_THREADS ();

  gst_poll_free (set);
}

GST_END_TEST;

static gpointer
delayed_restart (gpointer data)
{
  GstPoll *set = data;
  GstPollFD fd = { 0, };

  fd.fd = 1;

  THREAD_START ();

  g_usleep (500000);

  gst_poll_add_fd (set, &fd);
  gst_poll_fd_ctl_write (set, &fd, TRUE);
  gst_poll_restart (set);

  return NULL;
}

GST_START_TEST (test_poll_wait_restart)
{
  GstPoll *set;
  GstPollFD fd = { 0, };

  fd.fd = 1;

  set = gst_poll_new (GST_POLL_MODE_AUTO, TRUE);
  fail_if (set == NULL, "Failed to create a GstPoll");

  MAIN_START_THREADS (1, delayed_restart, set);

  fail_unless (gst_poll_wait (set, GST_SECOND) > 0, "Waiting was interrupted");
  fail_unless (gst_poll_fd_can_write (set, &fd),
      "Write descriptor should be writeable");

  MAIN_STOP_THREADS ();

  gst_poll_free (set);
}

GST_END_TEST;

static gpointer
delayed_flush (gpointer data)
{
  GstPoll *set = data;

  THREAD_START ();

  g_usleep (500000);
  gst_poll_set_flushing (set, TRUE);

  return NULL;
}

GST_START_TEST (test_poll_wait_flush)
{
  GstPoll *set;

  set = gst_poll_new (GST_POLL_MODE_AUTO, TRUE);
  fail_if (set == NULL, "Failed to create a GstPoll");

  gst_poll_set_flushing (set, TRUE);
  fail_unless (gst_poll_wait (set, GST_SECOND) == -1 && errno == EBUSY,
      "Waiting was not flushed");
  fail_unless (gst_poll_wait (set, GST_SECOND) == -1 && errno == EBUSY,
      "Waiting was not flushed");

  gst_poll_set_flushing (set, FALSE);
  fail_unless (gst_poll_wait (set, GST_SECOND) == 0, "Waiting did not timeout");

  MAIN_START_THREADS (1, delayed_flush, set);

  fail_unless (gst_poll_wait (set, GST_SECOND) == -1 && errno == EBUSY,
      "Waiting was not flushed");
  fail_unless (gst_poll_wait (set, GST_SECOND) == -1 && errno == EBUSY,
      "Waiting was not flushed");

  gst_poll_set_flushing (set, FALSE);
  fail_unless (gst_poll_wait (set, GST_SECOND) == 0, "Waiting did not timeout");

  MAIN_STOP_THREADS ();

  gst_poll_free (set);
}

GST_END_TEST;

static gpointer
delayed_control (gpointer data)
{
  GstPoll *set = data;
  GstPollFD fd = { 0, };

  fd.fd = 1;

  THREAD_START ();

  g_usleep (500000);

  gst_poll_add_fd (set, &fd);
  gst_poll_fd_ctl_write (set, &fd, TRUE);
  gst_poll_restart (set);

  THREAD_SYNCHRONIZE ();

  g_usleep (500000);

  gst_poll_add_fd (set, &fd);
  gst_poll_fd_ctl_write (set, &fd, TRUE);
  gst_poll_restart (set);

  return NULL;
}

GST_START_TEST (test_poll_controllable)
{
  GstPoll *set;
  GstPollFD fd = { 0, };

  fd.fd = 1;

  set = gst_poll_new (GST_POLL_MODE_AUTO, FALSE);
  fail_if (set == NULL, "Failed to create a GstPoll");

  MAIN_START_THREADS (1, delayed_control, set);

  fail_unless (gst_poll_wait (set, GST_SECOND) == 0, "Waiting did not timeout");

  fail_unless (gst_poll_remove_fd (set, &fd), "Could not remove descriptor");
  fail_unless (gst_poll_set_controllable (set, TRUE),
      "Could not make the set controllable");

  MAIN_SYNCHRONIZE ();

  fail_unless (gst_poll_wait (set, GST_SECOND) > 0, "Waiting was interrupted");
  fail_unless (gst_poll_fd_can_write (set, &fd),
      "Write descriptor should be writeable");

  MAIN_STOP_THREADS ();

  gst_poll_free (set);
}

GST_END_TEST;

GST_START_TEST (test_poll_wait_auto)
{
  test_poll_wait (GST_POLL_MODE_AUTO);
}

GST_END_TEST;

GST_START_TEST (test_poll_wait_ppoll)
{
  test_poll_wait (GST_POLL_MODE_PPOLL);
}

GST_END_TEST;

GST_START_TEST (test_poll_wait_poll)
{
  test_poll_wait (GST_POLL_MODE_POLL);
}

GST_END_TEST;

GST_START_TEST (test_poll_wait_pselect)
{
  test_poll_wait (GST_POLL_MODE_PSELECT);
}

GST_END_TEST;

GST_START_TEST (test_poll_wait_select)
{
  test_poll_wait (GST_POLL_MODE_SELECT);
}

GST_END_TEST;

Suite *
gst_poll_suite (void)
{
  Suite *s = suite_create ("GstPoll");
  TCase *tc_chain = tcase_create ("general");

  /* turn off timeout */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_poll_basic);
  tcase_add_test (tc_chain, test_poll_wait_auto);
  tcase_add_test (tc_chain, test_poll_wait_ppoll);
  tcase_add_test (tc_chain, test_poll_wait_poll);
  tcase_add_test (tc_chain, test_poll_wait_pselect);
  tcase_add_test (tc_chain, test_poll_wait_select);
  tcase_add_test (tc_chain, test_poll_wait_stop);
  tcase_add_test (tc_chain, test_poll_wait_restart);
  tcase_add_test (tc_chain, test_poll_wait_flush);
  tcase_add_test (tc_chain, test_poll_controllable);

  return s;
}

GST_CHECK_MAIN (gst_poll);
