/* GStreamer IIR equalizer
 * Copyright (C) <2004> Benjamin Otte <otte@gnome.org>
 *               <2007> Stefan Kost <ensonic@users.sf.net>
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

#ifndef __GST_IIR_EQUALIZER__
#define __GST_IIR_EQUALIZER__

#include <gst/audio/gstaudiofilter.h>
#include <gst/audio/gstringbuffer.h>
#include <gst/controller/gstcontroller.h>

typedef struct _GstIirEqualizer GstIirEqualizer;
typedef struct _GstIirEqualizerClass GstIirEqualizerClass;
typedef struct _GstIirEqualizerBand GstIirEqualizerBand;

#define GST_TYPE_IIR_EQUALIZER \
  (gst_iir_equalizer_get_type())
#define GST_IIR_EQUALIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_IIR_EQUALIZER,GstIirEqualizer))
#define GST_IIR_EQUALIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_IIR_EQUALIZER,GstIirEqualizerClass))
#define GST_IS_IIR_EQUALIZER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_IIR_EQUALIZER))
#define GST_IS_IIR_EQUALIZER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_IIR_EQUALIZER))

#define LOWEST_FREQ (20.0)
#define HIGHEST_FREQ (20000.0)

typedef void (*ProcessFunc) (GstIirEqualizer * eq, guint8 * data, guint size,
    guint channels);

struct _GstIirEqualizer
{
  GstAudioFilter audiofilter;

  /*< private >*/

  GstIirEqualizerBand **bands;

  /* properties */
  guint freq_band_count;
  gdouble band_width;
  /* for each band and channel */
  gpointer history;
  guint history_size;

  ProcessFunc process;
};

struct _GstIirEqualizerClass
{
  GstAudioFilterClass audiofilter_class;
};

extern void gst_iir_equalizer_compute_frequencies (GstIirEqualizer * equ, guint new_count);

extern GType gst_iir_equalizer_get_type(void);

#endif /* __GST_IIR_EQUALIZER__ */
