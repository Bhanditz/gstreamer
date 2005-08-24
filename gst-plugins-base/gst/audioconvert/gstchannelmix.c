/* GStreamer
 * Copyright (C) 2004 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstchannelmix.c: setup of channel conversion matrices
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

#include <math.h>
#include <string.h>
#include <gst/audio/multichannel.h>

#include "gstchannelmix.h"

/* GLib < 2.4 compatibility */
#ifndef G_MININT32
#define G_MININT32	((gint32)  0x80000000)
#endif

#ifndef G_MAXINT32
#define G_MAXINT32	((gint32)  0x7fffffff)
#endif

/*
 * Channel matrix functions.
 */

void
gst_audio_convert_unset_matrix (GstAudioConvert * this)
{
  gint i;

  /* don't access if nothing there */
  if (!this->matrix)
    return;

  /* free */
  for (i = 0; i < this->sinkcaps.channels; i++)
    g_free (this->matrix[i]);
  g_free (this->matrix);

  this->matrix = NULL;
}

/*
 * Detect and fill in identical channels. E.g.
 * forward the left/right front channels in a
 * 5.1 to 2.0 conversion.
 */

static void
gst_audio_convert_fill_identical (GstAudioConvert * this)
{
  gint ci, co;

  /* Apart from the compatible channel assignments, we can also have
   * same channel assignments. This is much simpler, we simply copy
   * the value from source to dest! */
  for (co = 0; co < this->srccaps.channels; co++) {
    /* find a channel in input with same position */
    for (ci = 0; ci < this->sinkcaps.channels; ci++) {
      if (this->sinkcaps.pos[ci] == this->srccaps.pos[co]) {
        this->matrix[ci][co] = 1.0;
      }
    }
  }
}

/*
 * Detect and fill in compatible channels. E.g.
 * forward left/right front to mono (or the other
 * way around) when going from 2.0 to 1.0.
 */

static void
gst_audio_convert_fill_compatible (GstAudioConvert * this)
{
  /* Conversions from one-channel to compatible two-channel configs */
  struct
  {
    GstAudioChannelPosition pos1[2];
    GstAudioChannelPosition pos2[1];
  } conv[] = {
    /* front: mono <-> stereo */
    { {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
            GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}, {
    GST_AUDIO_CHANNEL_POSITION_FRONT_MONO}},
        /* front center: 2 <-> 1 */
    { {
    GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
            GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER}, {
    GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER}},
        /* rear: 2 <-> 1 */
    { {
    GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
            GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT}, {
    GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}}, { {
    GST_AUDIO_CHANNEL_POSITION_INVALID}}
  };
  gint c;

  /* conversions from compatible (but not the same) channel schemes. This
   * goes two ways: if the sink has both pos1[0,1] and src has pos2[0] or
   * if the src has both pos1[0,1] and sink has pos2[0], then we do the
   * conversion. We hereby assume that the existance of pos1[0,1] and
   * pos2[0] are mututally exclusive. There are no checks for that,
   * unfortunately. This shouldn't lead to issues (like crashes or so),
   * though. */
  for (c = 0; conv[c].pos1[0] != GST_AUDIO_CHANNEL_POSITION_INVALID; c++) {
    gint pos1_0 = -1, pos1_1 = -1, pos2_0 = -1, n;

    /* Try to go from the given 2 channels to the given 1 channel */
    for (n = 0; n < this->sinkcaps.channels; n++) {
      if (this->sinkcaps.pos[n] == conv[c].pos1[0])
        pos1_0 = n;
      else if (this->sinkcaps.pos[n] == conv[c].pos1[1])
        pos1_1 = n;
    }
    for (n = 0; n < this->srccaps.channels; n++) {
      if (this->srccaps.pos[n] == conv[c].pos2[0])
        pos2_0 = n;
    }

    if (pos1_0 != -1 && pos1_1 != -1 && pos2_0 != -1) {
      this->matrix[pos1_0][pos2_0] = 1.0;
      this->matrix[pos1_1][pos2_0] = 1.0;
    }

    /* Try to go from the given 1 channel to the given 2 channels */
    pos1_0 = -1;
    pos1_1 = -1;
    pos2_0 = -1;

    for (n = 0; n < this->srccaps.channels; n++) {
      if (this->srccaps.pos[n] == conv[c].pos1[0])
        pos1_0 = n;
      else if (this->srccaps.pos[n] == conv[c].pos1[1])
        pos1_1 = n;
    }
    for (n = 0; n < this->sinkcaps.channels; n++) {
      if (this->sinkcaps.pos[n] == conv[c].pos2[0])
        pos2_0 = n;
    }

    if (pos1_0 != -1 && pos1_1 != -1 && pos2_0 != -1) {
      this->matrix[pos2_0][pos1_0] = 1.0;
      this->matrix[pos2_0][pos1_1] = 1.0;
    }
  }
}

/*
 * Detect and fill in channels not handled by the
 * above two, e.g. center to left/right front in
 * 5.1 to 2.0 (or the other way around).
 *
 * Unfortunately, limited to static conversions
 * for now.
 */

static void
gst_audio_convert_detect_pos (GstAudioConvertCaps * caps,
    gint * f, gboolean * has_f,
    gint * c, gboolean * has_c, gint * r, gboolean * has_r,
    gint * s, gboolean * has_s, gint * b, gboolean * has_b)
{
  gint n;

  for (n = 0; n < caps->channels; n++) {
    switch (caps->pos[n]) {
      case GST_AUDIO_CHANNEL_POSITION_FRONT_MONO:
      case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT:
      case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT:
        *has_f = TRUE;
        if (f[0] == -1)
          f[0] = n;
        else
          f[1] = n;
        break;
      case GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER:
      case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER:
      case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER:
        *has_c = TRUE;
        if (c[0] == -1)
          c[0] = n;
        else
          c[1] = n;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_CENTER:
      case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
      case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
        *has_r = TRUE;
        if (r[0] == -1)
          r[0] = n;
        else
          r[1] = n;
        break;
      case GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT:
      case GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT:
        *has_s = TRUE;
        if (s[0] == -1)
          s[0] = n;
        else
          s[1] = n;
        break;
      case GST_AUDIO_CHANNEL_POSITION_LFE:
        *has_b = TRUE;
        b[0] = n;
        break;
      default:
        break;
    }
  }
}

static void
gst_audio_convert_fill_one_other (gfloat ** matrix,
    GstAudioConvertCaps * from_caps, gint * from_idx,
    GstAudioChannelPosition from_pos_l,
    GstAudioChannelPosition from_pos_r,
    GstAudioChannelPosition from_pos_c,
    GstAudioConvertCaps * to_caps, gint * to_idx,
    GstAudioChannelPosition to_pos_l,
    GstAudioChannelPosition to_pos_r,
    GstAudioChannelPosition to_pos_c, gfloat ratio)
{
  gfloat in_r, out_r[2] = { 0.f, 0.f };

  /*
   * The idea is that we add up from the input (which means that if we
   * have stereo input, we divide their sum by two) and put that in
   * the matrix for their output ratio (given in $ratio).
   * For left channels, we need to invert the signal sign (* -1).
   */

  if (from_caps->pos[from_idx[0]] == from_pos_c)
    in_r = 1.0;
  else
    in_r = 0.5;

  if (to_caps->pos[to_idx[0]] == to_pos_l)
    out_r[0] = in_r * -ratio;
  else
    out_r[0] = in_r * ratio;

  if (to_idx[1] != -1) {
    if (to_caps->pos[to_idx[1]] == to_pos_l)
      out_r[1] = in_r * -ratio;
    else
      out_r[1] = in_r * ratio;
  }

  matrix[from_idx[0]][to_idx[0]] = out_r[0];
  if (to_idx[1] != -1)
    matrix[from_idx[0]][to_idx[1]] = out_r[1];
  if (from_idx[1] != -1) {
    matrix[from_idx[1]][to_idx[0]] = out_r[0];
    if (to_idx[1] != -1)
      matrix[from_idx[1]][to_idx[1]] = out_r[1];
  }
}

#define RATIO_FRONT_CENTER (1.0 / sqrt (2.0))
#define RATIO_FRONT_REAR (1.0 / sqrt (2.0))
#define RATIO_FRONT_BASS (1.0)
#define RATIO_REAR_BASS (1.0 / sqrt (2.0))
#define RATIO_CENTER_BASS (1.0 / sqrt (2.0))

static void
gst_audio_convert_fill_others (GstAudioConvert * this)
{
  gboolean in_has_front = FALSE, out_has_front = FALSE,
      in_has_center = FALSE, out_has_center = FALSE,
      in_has_rear = FALSE, out_has_rear = FALSE,
      in_has_side = FALSE, out_has_side = FALSE,
      in_has_bass = FALSE, out_has_bass = FALSE;
  gint in_f[2] = { -1, -1 }, out_f[2] = {
  -1, -1}, in_c[2] = {
  -1, -1}, out_c[2] = {
  -1, -1}, in_r[2] = {
  -1, -1}, out_r[2] = {
  -1, -1}, in_s[2] = {
  -1, -1}, out_s[2] = {
  -1, -1}, in_b[2] = {
  -1, -1}, out_b[2] = {
  -1, -1};

  /* First see where (if at all) the various channels from/to
   * which we want to convert are located in our matrix/array. */
  gst_audio_convert_detect_pos (&this->sinkcaps,
      in_f, &in_has_front,
      in_c, &in_has_center, in_r, &in_has_rear,
      in_s, &in_has_side, in_b, &in_has_bass);
  gst_audio_convert_detect_pos (&this->srccaps,
      out_f, &out_has_front,
      out_c, &out_has_center, out_r, &out_has_rear,
      out_s, &out_has_side, out_b, &out_has_bass);

  /* center/front */
  if (!in_has_center && in_has_front && out_has_center) {
    gst_audio_convert_fill_one_other (this->matrix,
        &this->sinkcaps, in_f,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_MONO,
        &this->srccaps, out_c,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, RATIO_FRONT_CENTER);
  } else if (in_has_center && !out_has_center && out_has_front) {
    gst_audio_convert_fill_one_other (this->matrix,
        &this->sinkcaps, in_c,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        &this->srccaps, out_f,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_MONO, RATIO_FRONT_CENTER);
  }

  /* rear/front */
  if (!in_has_rear && in_has_front && out_has_rear) {
    gst_audio_convert_fill_one_other (this->matrix,
        &this->sinkcaps, in_f,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_MONO,
        &this->srccaps, out_r,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_CENTER, RATIO_FRONT_REAR);
  } else if (in_has_center && !out_has_center && out_has_front) {
    gst_audio_convert_fill_one_other (this->matrix,
        &this->sinkcaps, in_r,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
        &this->srccaps, out_f,
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_MONO, RATIO_FRONT_REAR);
  }

  /* bass/any */
  if (in_has_bass && !out_has_bass) {
    if (out_has_front) {
      gst_audio_convert_fill_one_other (this->matrix,
          &this->sinkcaps, in_b,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_LFE,
          &this->srccaps, out_f,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_MONO, RATIO_FRONT_BASS);
    }
    if (out_has_center) {
      gst_audio_convert_fill_one_other (this->matrix,
          &this->sinkcaps, in_b,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_LFE,
          &this->srccaps, out_c,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER, RATIO_CENTER_BASS);
    }
    if (out_has_rear) {
      gst_audio_convert_fill_one_other (this->matrix,
          &this->sinkcaps, in_b,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_LFE,
          &this->srccaps, out_r,
          GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
          GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_REAR_CENTER, RATIO_REAR_BASS);
    }
  } else if (!in_has_bass && out_has_bass) {
    if (in_has_front) {
      gst_audio_convert_fill_one_other (this->matrix,
          &this->sinkcaps, in_f,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_MONO,
          &this->srccaps, out_b,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_LFE, RATIO_FRONT_BASS);
    }
    if (in_has_center) {
      gst_audio_convert_fill_one_other (this->matrix,
          &this->sinkcaps, in_c,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          &this->srccaps, out_b,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_LFE, RATIO_CENTER_BASS);
    }
    if (in_has_rear) {
      gst_audio_convert_fill_one_other (this->matrix,
          &this->sinkcaps, in_r,
          GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
          GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_REAR_CENTER,
          &this->srccaps, out_b,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_INVALID,
          GST_AUDIO_CHANNEL_POSITION_LFE, RATIO_REAR_BASS);
    }
  }

  /* FIXME: side */
}

/*
 * Normalize output values.
 */

static void
gst_audio_convert_fill_normalize (GstAudioConvert * this)
{
  gfloat sum, top = 0;
  gint i, j;

  for (j = 0; j < this->srccaps.channels; j++) {
    /* calculate sum */
    sum = 0.0;
    for (i = 0; i < this->sinkcaps.channels; i++) {
      sum += fabs (this->matrix[i][j]);
    }
    if (sum > top) {
      top = sum;
    }
  }

  /* normalize to this */
  for (j = 0; j < this->srccaps.channels; j++) {
    for (i = 0; i < this->sinkcaps.channels; i++) {
      this->matrix[i][j] /= top;
    }
  }
}

/*
 * Automagically generate conversion matrix.
 */

static void
gst_audio_convert_fill_matrix (GstAudioConvert * this)
{
  gst_audio_convert_fill_identical (this);
  gst_audio_convert_fill_compatible (this);
  gst_audio_convert_fill_others (this);
  gst_audio_convert_fill_normalize (this);
}

/* only call after this->srccaps and this->sinkcaps are filled in */
void
gst_audio_convert_setup_matrix (GstAudioConvert * this)
{
  gint i, j;
  GString *s;

  /* don't lose memory */
  gst_audio_convert_unset_matrix (this);

  /* allocate */
  this->matrix = g_new0 (gfloat *, this->sinkcaps.channels);
  for (i = 0; i < this->sinkcaps.channels; i++) {
    this->matrix[i] = g_new (gfloat, this->srccaps.channels);
    for (j = 0; j < this->srccaps.channels; j++)
      this->matrix[i][j] = 0.;
  }

  /* setup the matrix' internal values */
  gst_audio_convert_fill_matrix (this);

  /* debug */
  s = g_string_new ("Matrix for");
  g_string_append_printf (s, " %d -> %d: ",
      this->sinkcaps.channels, this->srccaps.channels);
  g_string_append (s, "{");
  for (i = 0; i < this->sinkcaps.channels; i++) {
    if (i != 0)
      g_string_append (s, ",");
    g_string_append (s, " {");
    for (j = 0; j < this->srccaps.channels; j++) {
      if (j != 0)
        g_string_append (s, ",");
      g_string_append_printf (s, " %f", this->matrix[i][j]);
    }
    g_string_append (s, " }");
  }
  g_string_append (s, " }");
  GST_DEBUG (s->str);
  g_string_free (s, TRUE);
}

gboolean
gst_audio_convert_passthrough (GstAudioConvert * this)
{
  gint i;

  /* only NxN matrices can be identities */
  if (this->sinkcaps.channels != this->srccaps.channels)
    return FALSE;

  /* this assumes a normalized matrix */
  for (i = 0; i < this->sinkcaps.channels; i++)
    if (this->matrix[i][i] != 1.)
      return FALSE;

  return TRUE;
}

/* IMPORTANT: out_data == in_data is possible, make sure to not overwrite data
 * you might need later on! */
void
gst_audio_convert_mix (GstAudioConvert * this,
    gint32 * in_data, gint32 * out_data, gint samples)
{
  gint in, out, n;
  gint64 res;
  gint32 tmp[this->srccaps.channels];
  gboolean backwards = this->srccaps.channels > this->sinkcaps.channels;

  /* FIXME: use liboil here? */
  for (n = (backwards ? samples - 1 : 0); n < samples && n >= 0;
      backwards ? n-- : n++) {
    for (out = 0; out < this->srccaps.channels; out++) {
      /* convert */
      res = 0;
      for (in = 0; in < this->sinkcaps.channels; in++) {
        res += in_data[n * this->sinkcaps.channels + in] *
            this->matrix[in][out];
      }

      /* clip (shouldn't we use doubles instead as intermediate format?) */
      if (res < G_MININT32)
        res = G_MININT32;
      else if (res > G_MAXINT32)
        res = G_MAXINT32;
      tmp[out] = res;
    }
    memcpy (&out_data[n * this->srccaps.channels], tmp,
        sizeof (gint32) * this->srccaps.channels);
  }
}
