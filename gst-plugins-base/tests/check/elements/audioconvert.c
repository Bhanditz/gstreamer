/* GStreamer
 *
 * unit test for audioconvert
 *
 * Copyright (C) <2005> Thomas Vander Stichele <thomas at apestaart dot org>
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
#include <gst/audio/multichannel.h>

gboolean have_eos = FALSE;

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define CONVERT_CAPS_TEMPLATE_STRING    \
  "audio/x-raw-float, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) BYTE_ORDER, " \
    "width = (int) 32;" \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 32, " \
    "depth = (int) [ 1, 32 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 24, " \
    "depth = (int) [ 1, 24 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 16, " \
    "depth = (int) [ 1, 16 ], " \
    "signed = (boolean) { true, false }; " \
  "audio/x-raw-int, " \
    "rate = (int) [ 1, MAX ], " \
    "channels = (int) [ 1, 8 ], " \
    "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, " \
    "width = (int) 8, " \
    "depth = (int) [ 1, 8 ], " \
    "signed = (boolean) { true, false } "

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CONVERT_CAPS_TEMPLATE_STRING)
    );
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CONVERT_CAPS_TEMPLATE_STRING)
    );

/* takes over reference for outcaps */
GstElement *
setup_audioconvert (GstCaps * outcaps)
{
  GstElement *audioconvert;

  GST_DEBUG ("setup_audioconvert with caps %" GST_PTR_FORMAT, outcaps);
  audioconvert = gst_check_setup_element ("audioconvert");
  mysrcpad = gst_check_setup_src_pad (audioconvert, &srctemplate, NULL);
  mysinkpad = gst_check_setup_sink_pad (audioconvert, &sinktemplate, NULL);
  /* this installs a getcaps func that will always return the caps we set
   * later */
  gst_pad_use_fixed_caps (mysinkpad);
  gst_pad_set_caps (mysinkpad, outcaps);
  gst_caps_unref (outcaps);
  outcaps = gst_pad_get_negotiated_caps (mysinkpad);
  fail_unless (gst_caps_is_fixed (outcaps));
  gst_caps_unref (outcaps);

  gst_pad_set_active (mysrcpad, TRUE);
  gst_pad_set_active (mysinkpad, TRUE);

  return audioconvert;
}

void
cleanup_audioconvert (GstElement * audioconvert)
{
  GST_DEBUG ("cleanup_audioconvert");

  gst_pad_set_active (mysrcpad, FALSE);
  gst_pad_set_active (mysinkpad, FALSE);
  gst_check_teardown_src_pad (audioconvert);
  gst_check_teardown_sink_pad (audioconvert);
  gst_check_teardown_element (audioconvert);
}

/* returns a newly allocated caps */
static GstCaps *
get_int_caps (guint channels, gchar * endianness, guint width,
    guint depth, gboolean signedness)
{
  GstCaps *caps;
  gchar *string;

  string = g_strdup_printf ("audio/x-raw-int, "
      "rate = (int) 44100, "
      "channels = (int) %d, "
      "endianness = (int) %s, "
      "width = (int) %d, "
      "depth = (int) %d, "
      "signed = (boolean) %s ",
      channels, endianness, width, depth, signedness ? "true" : "false");
  GST_DEBUG ("creating caps from %s", string);
  caps = gst_caps_from_string (string);
  g_free (string);
  fail_unless (caps != NULL);
  GST_DEBUG ("returning caps %p", caps);
  return caps;
}

/* returns a newly allocated caps */
static GstCaps *
get_float_caps (guint channels, gchar * endianness, guint width)
{
  GstCaps *caps;
  gchar *string;

  string = g_strdup_printf ("audio/x-raw-float, "
      "rate = (int) 44100, "
      "channels = (int) %d, "
      "endianness = (int) %s, "
      "width = (int) %d ", channels, endianness, width);
  GST_DEBUG ("creating caps from %s", string);
  caps = gst_caps_from_string (string);
  g_free (string);
  fail_unless (caps != NULL);
  GST_DEBUG ("returning caps %p", caps);
  return caps;
}

/* Copied from vorbis; the particular values used don't matter */
static GstAudioChannelPosition channelpositions[][6] = {
  {                             /* Mono */
      GST_AUDIO_CHANNEL_POSITION_FRONT_MONO},
  {                             /* Stereo */
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {                             /* Stereo + Centre */
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
      GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT},
  {                             /* Quadraphonic */
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      },
  {                             /* Stereo + Centre + rear stereo */
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
      },
  {                             /* Full 5.1 Surround */
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_LFE,
      },
};

static void
set_channel_positions (GstCaps * caps, int channels,
    GstAudioChannelPosition * channelpositions)
{
  GValue chanpos = { 0 };
  GValue pos = { 0 };
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  int c;

  g_value_init (&chanpos, GST_TYPE_ARRAY);
  g_value_init (&pos, GST_TYPE_AUDIO_CHANNEL_POSITION);

  for (c = 0; c < channels; c++) {
    g_value_set_enum (&pos, channelpositions[c]);
    gst_value_array_append_value (&chanpos, &pos);
  }
  g_value_unset (&pos);

  gst_structure_set_value (structure, "channel-positions", &chanpos);
  g_value_unset (&chanpos);
}

/* For channels > 2, caps have to have channel positions. This adds some simple
 * ones. Only implemented for channels between 1 and 6.
 */
static GstCaps *
get_float_mc_caps (guint channels, gchar * endianness, guint width)
{
  GstCaps *caps = get_float_caps (channels, endianness, width);

  if (channels <= 6)
    set_channel_positions (caps, channels, channelpositions[channels - 1]);

  return caps;
}

static GstCaps *
get_int_mc_caps (guint channels, gchar * endianness, guint width,
    guint depth, gboolean signedness)
{
  GstCaps *caps = get_int_caps (channels, endianness, width, depth, signedness);

  if (channels <= 6)
    set_channel_positions (caps, channels, channelpositions[channels - 1]);

  return caps;
}

/* eats the refs to the caps */
static void
verify_convert (const gchar * which, void *in, int inlength,
    GstCaps * incaps, void *out, int outlength, GstCaps * outcaps)
{
  GstBuffer *inbuffer, *outbuffer;
  GstElement *audioconvert;

  GST_DEBUG ("verifying conversion %s", which);
  GST_DEBUG ("incaps: %" GST_PTR_FORMAT, incaps);
  GST_DEBUG ("outcaps: %" GST_PTR_FORMAT, outcaps);
  ASSERT_CAPS_REFCOUNT (incaps, "incaps", 1);
  ASSERT_CAPS_REFCOUNT (outcaps, "outcaps", 1);
  audioconvert = setup_audioconvert (outcaps);
  ASSERT_CAPS_REFCOUNT (outcaps, "outcaps", 1);

  fail_unless (gst_element_set_state (audioconvert,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  GST_DEBUG ("Creating buffer of %d bytes", inlength);
  inbuffer = gst_buffer_new_and_alloc (inlength);
  memcpy (GST_BUFFER_DATA (inbuffer), in, inlength);
  gst_buffer_set_caps (inbuffer, incaps);
  ASSERT_CAPS_REFCOUNT (incaps, "incaps", 2);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* pushing gives away my reference ... */
  GST_DEBUG ("push it");
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  GST_DEBUG ("pushed it");
  /* ... and puts a new buffer on the global list */
  fail_unless (g_list_length (buffers) == 1);
  fail_if ((outbuffer = (GstBuffer *) buffers->data) == NULL);

  ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
  fail_unless_equals_int (GST_BUFFER_SIZE (outbuffer), outlength);

  if (memcmp (GST_BUFFER_DATA (outbuffer), out, outlength) != 0) {
    g_print ("\nConverted data:\n");
    gst_util_dump_mem (GST_BUFFER_DATA (outbuffer), outlength);
    g_print ("\nExpected data:\n");
    gst_util_dump_mem (out, outlength);
  }
  fail_unless (memcmp (GST_BUFFER_DATA (outbuffer), out, outlength) == 0,
      "failed converting %s", which);
  buffers = g_list_remove (buffers, outbuffer);
  gst_buffer_unref (outbuffer);
  fail_unless (gst_element_set_state (audioconvert,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");
  /* cleanup */
  GST_DEBUG ("cleanup audioconvert");
  cleanup_audioconvert (audioconvert);
  GST_DEBUG ("cleanup, unref incaps");
  ASSERT_CAPS_REFCOUNT (incaps, "incaps", 1);
  gst_caps_unref (incaps);
}


#define RUN_CONVERSION(which, inarray, in_get_caps, outarray, out_get_caps)    \
  verify_convert (which, inarray, sizeof (inarray),                            \
        in_get_caps, outarray, sizeof (outarray), out_get_caps)

GST_START_TEST (test_int16)
{
  /* stereo to mono */
  {
    gint16 in[] = { 16384, -256, 1024, 1024 };
    gint16 out[] = { 8064, 1024 };

    RUN_CONVERSION ("int16 stereo to mono",
        in, get_int_caps (2, "BYTE_ORDER", 16, 16, TRUE),
        out, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE));
  }
  /* mono to stereo */
  {
    gint16 in[] = { 512, 1024 };
    gint16 out[] = { 512, 512, 1024, 1024 };

    RUN_CONVERSION ("int16 mono to stereo",
        in, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE),
        out, get_int_caps (2, "BYTE_ORDER", 16, 16, TRUE));
  }
  /* signed -> unsigned */
  {
    gint16 in[] = { 0, -32767, 32767, -32768 };
    guint16 out[] = { 32768, 1, 65535, 0 };

    RUN_CONVERSION ("int16 signed to unsigned",
        in, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE),
        out, get_int_caps (1, "BYTE_ORDER", 16, 16, FALSE));
    RUN_CONVERSION ("int16 unsigned to signed",
        out, get_int_caps (1, "BYTE_ORDER", 16, 16, FALSE),
        in, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE));
  }
}

GST_END_TEST;

GST_START_TEST (test_int_conversion)
{
  /* 8 <-> 16 signed */
  /* NOTE: if audioconvert was doing dithering we'd have a problem */
  {
    gint8 in[] = { 0, 1, 2, 127, -127 };
    gint16 out[] = { 0, 256, 512, 32512, -32512 };

    RUN_CONVERSION ("int 8bit to 16bit signed",
        in, get_int_caps (1, "BYTE_ORDER", 8, 8, TRUE),
        out, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE)
        );
    RUN_CONVERSION ("int 16bit signed to 8bit",
        out, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE),
        in, get_int_caps (1, "BYTE_ORDER", 8, 8, TRUE)
        );
  }
  /* 16 -> 8 signed */
  {
    gint16 in[] = { 0, 255, 256, 257 };
    gint8 out[] = { 0, 0, 1, 1 };

    RUN_CONVERSION ("16 bit to 8 signed",
        in, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE),
        out, get_int_caps (1, "BYTE_ORDER", 8, 8, TRUE)
        );
  }
  /* 8 unsigned <-> 16 signed */
  /* NOTE: if audioconvert was doing dithering we'd have a problem */
  {
    guint8 in[] = { 128, 129, 130, 255, 1 };
    gint16 out[] = { 0, 256, 512, 32512, -32512 };
    GstCaps *incaps, *outcaps;

    /* exploded for easier valgrinding */
    incaps = get_int_caps (1, "BYTE_ORDER", 8, 8, FALSE);
    outcaps = get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE);
    GST_DEBUG ("incaps: %" GST_PTR_FORMAT, incaps);
    GST_DEBUG ("outcaps: %" GST_PTR_FORMAT, outcaps);
    RUN_CONVERSION ("8 unsigned to 16 signed", in, incaps, out, outcaps);
    RUN_CONVERSION ("16 signed to 8 unsigned", out, get_int_caps (1,
            "BYTE_ORDER", 16, 16, TRUE), in, get_int_caps (1, "BYTE_ORDER", 8,
            8, FALSE)
        );
  }
  /* 8 <-> 24 signed */
  /* NOTE: if audioconvert was doing dithering we'd have a problem */
  {
    gint8 in[] = { 0, 1, 127 };
    guint8 out[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x7f };
    /* out has the bytes in little-endian, so that's how they should be
     * interpreted during conversion */

    RUN_CONVERSION ("8 to 24 signed", in, get_int_caps (1, "BYTE_ORDER", 8, 8,
            TRUE), out, get_int_caps (1, "LITTLE_ENDIAN", 24, 24, TRUE)
        );
    RUN_CONVERSION ("24 signed to 8", out, get_int_caps (1, "LITTLE_ENDIAN", 24,
            24, TRUE), in, get_int_caps (1, "BYTE_ORDER", 8, 8, TRUE)
        );
  }
}

GST_END_TEST;

GST_START_TEST (test_float_conversion)
{
  /* 32 float <-> 16 signed */
  /* NOTE: if audioconvert was doing dithering we'd have a problem */
  {
    gfloat in[] = { 0.0, 1.0, -1.0, 0.5, -0.5, 1.1, -1.1 };
    gint16 out[] = { 0, 32767, -32768, 16384, -16384, 32767, -32768 };

    /* only one direction conversion, the other direction does
     * not produce exactly the same as the input due to floating
     * point rounding errors etc. */
    RUN_CONVERSION ("32 float to 16 signed", in, get_float_caps (1,
            "BYTE_ORDER", 32), out, get_int_caps (1, "BYTE_ORDER", 16, 16, TRUE)
        );
  }
}

GST_END_TEST;

GST_START_TEST (test_multichannel_conversion)
{
  {
    /* Ensure that audioconvert prefers to convert to integer, rather than mix
     * to mono
     */
    gfloat in[] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    gfloat out[] = { 0.0, 0.0 };

    /* only one direction conversion, the other direction does
     * not produce exactly the same as the input due to floating
     * point rounding errors etc. */
    RUN_CONVERSION ("3 channels to 1", in, get_float_mc_caps (3,
            "BYTE_ORDER", 32), out, get_float_caps (1, "BYTE_ORDER", 32));
  }
}

GST_END_TEST;

Suite *
audioconvert_suite (void)
{
  Suite *s = suite_create ("audioconvert");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_int16);
  tcase_add_test (tc_chain, test_int_conversion);
  tcase_add_test (tc_chain, test_float_conversion);
  tcase_add_test (tc_chain, test_multichannel_conversion);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = audioconvert_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
