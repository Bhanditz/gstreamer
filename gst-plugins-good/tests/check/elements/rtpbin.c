/* GStreamer
 *
 * unit test for gstrtpbin
 *
 * Copyright (C) <2009> Wim Taymans <wim.taymans@gmail.com>
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

GST_START_TEST (test_cleanup_send)
{
  GstElement *rtpbin;
  GstPad *rtp_sink, *rtp_src;
  GObject *session;

  rtpbin = gst_element_factory_make ("gstrtpbin", "rtpbin");

  rtp_sink = gst_element_get_request_pad (rtpbin, "send_rtp_sink_0");
  fail_unless (rtp_sink != NULL);
  ASSERT_OBJECT_REFCOUNT (rtp_sink, "rtp_sink", 2);

  rtp_src = gst_element_get_static_pad (rtpbin, "send_rtp_src_0");
  fail_unless (rtp_src != NULL);
  ASSERT_OBJECT_REFCOUNT (rtp_src, "rtp_src", 2);

  /* we should be able to get an internal session 0 now */
  g_signal_emit_by_name (rtpbin, "get-internal-session", 0, &session);
  fail_unless (session != NULL);
  g_object_unref (session);

  gst_element_release_request_pad (rtpbin, rtp_sink);
  ASSERT_OBJECT_REFCOUNT (rtp_sink, "rtp_sink", 1);
  ASSERT_OBJECT_REFCOUNT (rtp_src, "rtp_src", 1);

  /* the session should be gone now */
  g_signal_emit_by_name (rtpbin, "get-internal-session", 0, &session);
  fail_unless (session == NULL);

  /* the other pad should be gone too now */
  fail_unless (gst_element_get_static_pad (rtpbin, "send_rtp_src_0") == NULL);

  /* unref the request pad and the static pad */
  gst_object_unref (rtp_sink);
  gst_object_unref (rtp_src);

  gst_object_unref (rtpbin);
}

GST_END_TEST;


Suite *
gstrtpbin_suite (void)
{
  Suite *s = suite_create ("gstrtpbin");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_cleanup_send);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gstrtpbin_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
