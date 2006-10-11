/*
 * icydemux.c - Test icydemux element
 * Copyright (C) 2006 Michael Smith <msmith@fluendo.com>
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/check/gstcheck.h>

/* Chunk of data: 8 bytes, followed by a metadata-length byte of 2, followed by
 * some metadata (32 bytes), then some more data.
 */
#define TEST_METADATA \
    "Test metadata"
#define ICY_METADATA \
    "StreamTitle='" TEST_METADATA "';\0\0\0\0"

#define ICY_DATA \
    "aaaaaaaa" \
    "\x02" \
    ICY_METADATA \
    "bbbbbbbb"

#define ICYCAPS "application/x-icy, metadata-interval = (int)8"

#define SRC_CAPS "application/x-icy, metadata-interval = (int)[0, MAX]"
#define SINK_CAPS  "ANY"

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS)
    );

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS)
    );

static GstElement *icydemux;
static GstBus *bus;
GstPad *srcpad, *sinkpad;

static GstStaticCaps typefind_caps =
GST_STATIC_CAPS ("application/octet-stream");

static gboolean fake_typefind_caps;     /* FALSE */

static void
typefind_succeed (GstTypeFind * tf, gpointer private)
{
  if (fake_typefind_caps) {
    gst_type_find_suggest (tf, GST_TYPE_FIND_MAXIMUM,
        gst_static_caps_get (&typefind_caps));
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gst_type_find_register (plugin, "success", GST_RANK_PRIMARY, typefind_succeed,
      NULL, gst_static_caps_get (&typefind_caps), NULL, NULL);

  return TRUE;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "gst-test",
    "test plugin for icydemux",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

static void
icydemux_found_pad (GstElement * src, GstPad * pad, gpointer data)
{
  /* Turns out that this asserts a refcount which is wrong for this
   * case (adding the pad from a pad-added callback), so just do the same
   * thing inline... */
  /* sinkpad = gst_check_setup_sink_pad (icydemux, &sinktemplate, NULL); */
  sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  fail_if (sinkpad == NULL, "Couldn't create sinkpad");
  srcpad = gst_element_get_pad (icydemux, "src");
  fail_if (srcpad == NULL, "Failed to get srcpad from icydemux");
  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Failed to link pads");
  gst_object_unref (srcpad);
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_pad_set_active (sinkpad, TRUE);
}

static GstElement *
create_icydemux (void)
{
  icydemux = gst_check_setup_element ("icydemux");
  srcpad = gst_check_setup_src_pad (icydemux, &srctemplate, NULL);

  g_signal_connect (icydemux, "pad-added", G_CALLBACK (icydemux_found_pad),
      NULL);

  bus = gst_bus_new ();
  gst_element_set_bus (icydemux, bus);

  fail_unless (gst_element_set_state (icydemux, GST_STATE_PLAYING) !=
      GST_STATE_CHANGE_FAILURE, "could not set to playing");

  return icydemux;
}

static void
cleanup_icydemux (void)
{
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);
  bus = NULL;

  gst_check_teardown_src_pad (icydemux);
  gst_check_teardown_sink_pad (icydemux);
  gst_check_teardown_element (icydemux);

  srcpad = NULL;
  sinkpad = NULL;
  icydemux = NULL;
}

static void
push_data (const guint8 * data, int len, GstCaps * caps, gint64 offset)
{
  GstFlowReturn res;
  GstBuffer *buffer = gst_buffer_new_and_alloc (len);

  memcpy (GST_BUFFER_DATA (buffer), data, len);
  gst_buffer_set_caps (buffer, caps);

  GST_BUFFER_OFFSET (buffer) = offset;

  res = gst_pad_push (srcpad, buffer);

  fail_unless (res == GST_FLOW_OK, "Failed pushing buffer: %d", res);
}

GST_START_TEST (test_demux)
{
  GstMessage *message;
  GstTagList *tags;
  const GValue *tag_val;
  const gchar *tag;
  GstCaps *caps;

  fake_typefind_caps = TRUE;

  caps = gst_caps_from_string (ICYCAPS);

  create_icydemux ();

  push_data ((guint8 *) ICY_DATA, sizeof (ICY_DATA), caps, -1);

  message = gst_bus_poll (bus, GST_MESSAGE_TAG, -1);
  fail_unless (message != NULL);

  gst_message_parse_tag (message, &tags);
  fail_unless (tags != NULL);

  tag_val = gst_tag_list_get_value_index (tags, GST_TAG_TITLE, 0);
  fail_unless (tag_val != NULL);

  tag = g_value_get_string (tag_val);
  fail_unless (tag != NULL);

  fail_unless_equals_string (TEST_METADATA, (char *) tag);

  gst_tag_list_free (tags);
  gst_message_unref (message);
  gst_caps_unref (caps);

  cleanup_icydemux ();

  fake_typefind_caps = FALSE;
}

GST_END_TEST;

/* run this test first before the custom typefind function is set up */
GST_START_TEST (test_first_buf_offset_when_merged_for_typefinding)
{
  const guint8 buf1[] = { 'M' };
  const guint8 buf2[] = { 'P', '+', 0xff, 0xfb, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  GstCaps *icy_caps;
  GstPad *icy_srcpad;

  fake_typefind_caps = FALSE;

  create_icydemux ();

  icy_caps = gst_caps_from_string (ICYCAPS);

  push_data (buf1, G_N_ELEMENTS (buf1), icy_caps, 0);

  /* one byte isn't really enough for typefinding, can't have a srcpad yet */
  fail_unless (gst_element_get_pad (icydemux, "src") == NULL);

  push_data (buf2, G_N_ELEMENTS (buf2), icy_caps, -1);

  /* should have been enough to create a audio/x-musepack source pad .. */
  icy_srcpad = gst_element_get_pad (icydemux, "src");
  fail_unless (icy_srcpad != NULL);
  gst_object_unref (icy_srcpad);

  fail_unless (g_list_length (buffers) > 0);

  /* first buffer should have offset 0 even after it was merged with 2nd buf */
  fail_unless (GST_BUFFER_OFFSET (GST_BUFFER_CAST (buffers->data)) == 0);

  /* first buffer should have caps set */
  fail_unless (GST_BUFFER_CAPS (GST_BUFFER_CAST (buffers->data)) != NULL);

  gst_caps_unref (icy_caps);

  cleanup_icydemux ();
}

GST_END_TEST;

static Suite *
icydemux_suite (void)
{
  Suite *s = suite_create ("icydemux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_demux);
  tcase_add_test (tc_chain, test_first_buf_offset_when_merged_for_typefinding);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = icydemux_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
