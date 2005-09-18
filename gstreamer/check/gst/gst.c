/* GStreamer
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gst.c: Unit test for gst.c
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

#include <gst/check/gstcheck.h>

GST_START_TEST (test_init)
{
  /* don't segfault with NULL, NULL */
  gst_init (NULL, NULL);
  /* allow calling twice. well, actually, thrice. */
  gst_init (NULL, NULL);
}

GST_END_TEST;

GST_START_TEST (test_deinit)
{
  gst_init (NULL, NULL);

  gst_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_deinit_sysclock)
{
  GstClock *clock;

  gst_init (NULL, NULL);

  clock = gst_system_clock_obtain ();
  gst_object_unref (clock);

  gst_deinit ();
}

GST_END_TEST;


Suite *
gst_suite (void)
{
  Suite *s = suite_create ("Gst");
  TCase *tc_chain = tcase_create ("gst tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_init);
  tcase_add_test (tc_chain, test_deinit);
  tcase_add_test (tc_chain, test_deinit_sysclock);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
