/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosshelper.c: OSS helper routines
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

#include "gst/gst-i18n-plugin.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/soundcard.h>

#include <gst/interfaces/propertyprobe.h>

#include "gstosshelper.h"
#include "gstossmixer.h"

#if 0

#ifdef HAVE_OSS_INCLUDE_IN_SYS
#include <sys/soundcard.h>
#else

#ifdef HAVE_OSS_INCLUDE_IN_ROOT
#include <soundcard.h>
#else

#include <machine/soundcard.h>

#endif /* HAVE_OSS_INCLUDE_IN_ROOT */

#endif /* HAVE_OSS_INCLUDE_IN_SYS */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <glib.h>
#endif /* 0 */

typedef struct _GstOssProbe GstOssProbe;
struct _GstOssProbe
{
  int fd;
  int format;
  int n_channels;
  GArray *rates;
  int min;
  int max;
};

typedef struct _GstOssRange GstOssRange;
struct _GstOssRange
{
  int min;
  int max;
};

static GstStructure *gst_oss_helper_get_format_structure (unsigned int
    format_bit);
static gboolean gst_oss_helper_rate_probe_check (GstOssProbe * probe);
static int gst_oss_helper_rate_check_rate (GstOssProbe * probe, int irate);
static void gst_oss_helper_rate_add_range (GQueue * queue, int min, int max);
static void gst_oss_helper_rate_add_rate (GArray * array, int rate);
static int gst_oss_helper_rate_int_compare (gconstpointer a, gconstpointer b);

GstCaps *
gst_oss_helper_probe_caps (gint fd)
{
  GstOssProbe *probe;
  int i;
  gboolean ret;
  GstStructure *structure;
  unsigned int format_bit;
  unsigned int format_mask;
  GstCaps *caps;

  /* FIXME test make sure we're not currently playing */
  /* FIXME test both mono and stereo */

  format_mask = AFMT_U8 | AFMT_S8;

  if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
    format_mask |= AFMT_S16_LE | AFMT_U16_LE;
  else
    format_mask |= AFMT_S16_BE | AFMT_U16_BE;

  caps = gst_caps_new_empty ();

  /* assume that the most significant bit of format_mask is 0 */
  for (format_bit = 1 << 31; format_bit > 0; format_bit >>= 1) {
    if (format_bit & format_mask) {
      GValue rate_value = { 0 };

      probe = g_new0 (GstOssProbe, 1);
      probe->fd = fd;
      probe->format = format_bit;
      probe->n_channels = 2;

      ret = gst_oss_helper_rate_probe_check (probe);
      if (probe->min == -1 || probe->max == -1) {
        g_array_free (probe->rates, TRUE);
        g_free (probe);
        continue;
      }

      if (ret) {
        GValue value = { 0 };

        g_array_sort (probe->rates, gst_oss_helper_rate_int_compare);

        g_value_init (&rate_value, GST_TYPE_LIST);
        g_value_init (&value, G_TYPE_INT);

        for (i = 0; i < probe->rates->len; i++) {
          g_value_set_int (&value, g_array_index (probe->rates, int, i));

          gst_value_list_append_value (&rate_value, &value);
        }

        g_value_unset (&value);
      } else {
        /* one big range */
        g_value_init (&rate_value, GST_TYPE_INT_RANGE);
        gst_value_set_int_range (&rate_value, probe->min, probe->max);
      }

      g_array_free (probe->rates, TRUE);
      g_free (probe);

      structure = gst_oss_helper_get_format_structure (format_bit);
      gst_structure_set (structure, "channels", GST_TYPE_INT_RANGE, 1, 2, NULL);
      gst_structure_set_value (structure, "rate", &rate_value);
      g_value_unset (&rate_value);

      gst_caps_append_structure (caps, structure);
    }
  }

  if (gst_caps_is_empty (caps))
    /* fixme: make user-visible */
    g_critical ("Your OSS device could not be probed correctly");

  GST_DEBUG ("probed caps: %" GST_PTR_FORMAT, caps);

  return caps;
}

static GstStructure *
gst_oss_helper_get_format_structure (unsigned int format_bit)
{
  GstStructure *structure;
  int endianness;
  gboolean sign;
  int width;

  switch (format_bit) {
    case AFMT_U8:
      endianness = 0;
      sign = FALSE;
      width = 8;
      break;
    case AFMT_S16_LE:
      endianness = G_LITTLE_ENDIAN;
      sign = TRUE;
      width = 16;
      break;
    case AFMT_S16_BE:
      endianness = G_BIG_ENDIAN;
      sign = TRUE;
      width = 16;
      break;
    case AFMT_S8:
      endianness = 0;
      sign = TRUE;
      width = 8;
      break;
    case AFMT_U16_LE:
      endianness = G_LITTLE_ENDIAN;
      sign = FALSE;
      width = 16;
      break;
    case AFMT_U16_BE:
      endianness = G_BIG_ENDIAN;
      sign = FALSE;
      width = 16;
      break;
    default:
      g_assert_not_reached ();
      return NULL;
  }

  structure = gst_structure_new ("audio/x-raw-int",
      "width", G_TYPE_INT, width,
      "depth", G_TYPE_INT, width, "signed", G_TYPE_BOOLEAN, sign, NULL);

  if (endianness) {
    gst_structure_set (structure, "endianness", G_TYPE_INT, endianness, NULL);
  }

  return structure;
}

static gboolean
gst_oss_helper_rate_probe_check (GstOssProbe * probe)
{
  GstOssRange *range;
  GQueue *ranges;
  int exact_rates = 0;
  gboolean checking_exact_rates = TRUE;
  int n_checks = 0;
  gboolean result = TRUE;

  ranges = g_queue_new ();

  probe->rates = g_array_new (FALSE, FALSE, sizeof (int));

  probe->min = gst_oss_helper_rate_check_rate (probe, 1000);
  n_checks++;
  probe->max = gst_oss_helper_rate_check_rate (probe, 100000);
  /* a little bug workaround */
  {
    int max;

    max = gst_oss_helper_rate_check_rate (probe, 48000);
    if (max > probe->max) {
      GST_ERROR
          ("Driver bug recognized (driver does not round rates correctly).  Please file a bug report.");
      probe->max = max;
    }
  }
  n_checks++;
  if (probe->min == -1 || probe->max == -1) {
    /* This is a workaround for drivers that return -EINVAL (or another
     * error) for rates outside of [8000,48000].  If this fails, the
     * driver is seriously buggy, and probably doesn't work with other
     * media libraries/apps.  */
    probe->min = gst_oss_helper_rate_check_rate (probe, 8000);
    probe->max = gst_oss_helper_rate_check_rate (probe, 48000);
  }
  if (probe->min == -1 || probe->max == -1) {
    GST_DEBUG ("unexpected check_rate error");
    return FALSE;
  }
  gst_oss_helper_rate_add_range (ranges, probe->min + 1, probe->max - 1);

  while ((range = g_queue_pop_head (ranges))) {
    int min1;
    int max1;
    int mid;
    int mid_ret;

    GST_DEBUG ("checking [%d,%d]", range->min, range->max);

    mid = (range->min + range->max) / 2;
    mid_ret = gst_oss_helper_rate_check_rate (probe, mid);
    if (mid_ret == -1) {
      /* FIXME ioctl returned an error.  do something */
      GST_DEBUG ("unexpected check_rate error");
    }
    n_checks++;

    if (mid == mid_ret && checking_exact_rates) {
      int max_exact_matches = 20;

      exact_rates++;
      if (exact_rates > max_exact_matches) {
        GST_DEBUG ("got %d exact rates, assuming all are exact",
            max_exact_matches);
        result = FALSE;
        g_free (range);
        break;
      }
    } else {
      checking_exact_rates = FALSE;
    }

    /* Assume that the rate is arithmetically rounded to the nearest
     * supported rate. */
    if (mid == mid_ret) {
      min1 = mid - 1;
      max1 = mid + 1;
    } else {
      if (mid < mid_ret) {
        min1 = mid - (mid_ret - mid);
        max1 = mid_ret + 1;
      } else {
        min1 = mid_ret - 1;
        max1 = mid + (mid - mid_ret);
      }
    }

    gst_oss_helper_rate_add_range (ranges, range->min, min1);
    gst_oss_helper_rate_add_range (ranges, max1, range->max);

    g_free (range);
  }

  while ((range = g_queue_pop_head (ranges))) {
    g_free (range);
  }
  g_queue_free (ranges);

  return result;
}

static void
gst_oss_helper_rate_add_range (GQueue * queue, int min, int max)
{
  if (min <= max) {
    GstOssRange *range = g_new0 (GstOssRange, 1);

    range->min = min;
    range->max = max;

    g_queue_push_tail (queue, range);
    /* push_head also works, but has different probing behavior */
    /*g_queue_push_head (queue, range); */
  }
}

static int
gst_oss_helper_rate_check_rate (GstOssProbe * probe, int irate)
{
  int rate;
  int format;
  int n_channels;
  int ret;

  rate = irate;
  format = probe->format;
  n_channels = probe->n_channels;

  GST_LOG ("checking format %d, channels %d, rate %d",
      format, n_channels, rate);
  ret = ioctl (probe->fd, SNDCTL_DSP_SETFMT, &format);
  if (ret < 0)
    return -1;
  ret = ioctl (probe->fd, SNDCTL_DSP_CHANNELS, &n_channels);
  if (ret < 0)
    return -1;
  ret = ioctl (probe->fd, SNDCTL_DSP_SPEED, &rate);
  if (ret < 0)
    return -1;

  GST_DEBUG ("rate %d -> %d", irate, rate);

  if (rate == irate - 1 || rate == irate + 1) {
    rate = irate;
  }
  gst_oss_helper_rate_add_rate (probe->rates, rate);
  return rate;
}

static void
gst_oss_helper_rate_add_rate (GArray * array, int rate)
{
  int i;
  int val;

  for (i = 0; i < array->len; i++) {
    val = g_array_index (array, int, i);

    if (val == rate)
      return;
  }
  GST_DEBUG ("supported rate: %d", rate);
  g_array_append_val (array, rate);
}

static int
gst_oss_helper_rate_int_compare (gconstpointer a, gconstpointer b)
{
  const int *va = (const int *) a;
  const int *vb = (const int *) b;

  if (*va < *vb)
    return -1;
  if (*va > *vb)
    return 1;
  return 0;
}
