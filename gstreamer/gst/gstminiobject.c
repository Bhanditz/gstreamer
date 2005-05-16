/* GStreamer
 * Copyright (C) 2005 David Schleef <ds@schleef.org>
 *
 * gstminiobject.h: Header for GstMiniObject
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

#include "gst/gstminiobject.h"
#include "gst/gstinfo.h"
#include "gst/gst_private.h"
#include <gobject/gvaluecollector.h>

static void gst_mini_object_base_init (gpointer g_class);
static void gst_mini_object_base_finalize (gpointer g_class);
static void gst_mini_object_class_init (gpointer g_class, gpointer class_data);
static void gst_mini_object_init (GTypeInstance * instance, gpointer klass);

static void gst_value_mini_object_init (GValue * value);
static void gst_value_mini_object_free (GValue * value);
static void gst_value_mini_object_copy (const GValue * src_value,
    GValue * dest_value);
static gpointer gst_value_mini_object_peek_pointer (const GValue * value);
static gchar *gst_value_mini_object_collect (GValue * value,
    guint n_collect_values, GTypeCValue * collect_values, guint collect_flags);
static gchar *gst_value_mini_object_lcopy (const GValue * value,
    guint n_collect_values, GTypeCValue * collect_values, guint collect_flags);

GType
gst_mini_object_get_type (void)
{
  static GType _gst_mini_object_type;

  if (!_gst_mini_object_type) {
    GTypeValueTable value_table = {
      gst_value_mini_object_init,
      gst_value_mini_object_free,
      gst_value_mini_object_copy,
      gst_value_mini_object_peek_pointer,
      "p",
      gst_value_mini_object_collect,
      "p",
      gst_value_mini_object_lcopy
    };
    GTypeInfo mini_object_info = {
      sizeof (GstMiniObjectClass),
      gst_mini_object_base_init,
      gst_mini_object_base_finalize,
      gst_mini_object_class_init,
      NULL,
      NULL,
      sizeof (GstMiniObject),
      0,
      (GInstanceInitFunc) gst_mini_object_init,
      NULL
    };
    static const GTypeFundamentalInfo mini_object_fundamental_info = {
      (G_TYPE_FLAG_CLASSED | G_TYPE_FLAG_INSTANTIATABLE |
          G_TYPE_FLAG_DERIVABLE | G_TYPE_FLAG_DEEP_DERIVABLE)
    };

    mini_object_info.value_table = &value_table;

    _gst_mini_object_type = g_type_fundamental_next ();
    g_type_register_fundamental (_gst_mini_object_type, "GstMiniObject",
        &mini_object_info, &mini_object_fundamental_info, G_TYPE_FLAG_ABSTRACT);
  }

  return _gst_mini_object_type;
}

static void
gst_mini_object_base_init (gpointer g_class)
{
  /* do nothing */
}

static void
gst_mini_object_base_finalize (gpointer g_class)
{
  /* do nothing */
}

static void
gst_mini_object_class_init (gpointer g_class, gpointer class_data)
{
  /* do nothing */
}

static void
gst_mini_object_init (GTypeInstance * instance, gpointer klass)
{
  GstMiniObject *mini_object = GST_MINI_OBJECT (instance);

  mini_object->refcount = 1;
}

GstMiniObject *
gst_mini_object_new (GType type)
{
  //GstMiniObjectClass *klass;
  GstMiniObject *mini_object;

#if 0
  klass = g_type_class_peek_static (type);
  if (!klass) {
    g_warning ("cannot find class for type '%s'", g_type_name (type));
    return NULL;
  }
#endif
  /* we don't support dynamic types because they really aren't useful,
   * and could cause refcount problems */

  mini_object = (GstMiniObject *) g_type_create_instance (type);

  return mini_object;
}

GstMiniObject *
gst_mini_object_copy (const GstMiniObject * mini_object)
{
  GstMiniObjectClass *mo_class;

  mo_class = GST_MINI_OBJECT_GET_CLASS (mini_object);

  return mo_class->copy (mini_object);
}

gboolean
gst_mini_object_is_writable (const GstMiniObject * mini_object)
{
  return (mini_object->refcount == 1) &&
      ((mini_object->flags & GST_MINI_OBJECT_FLAG_READONLY) == 0);
}

GstMiniObject *
gst_mini_object_make_writable (const GstMiniObject * mini_object)
{
  if (gst_mini_object_is_writable (mini_object)) {
    return (GstMiniObject *) mini_object;
  }
  return gst_mini_object_copy (mini_object);
}

GstMiniObject *
gst_mini_object_ref (GstMiniObject * mini_object)
{
  g_return_val_if_fail (mini_object != NULL, NULL);

  g_atomic_int_inc (&mini_object->refcount);

  return mini_object;
}


static void
gst_mini_object_free (GstMiniObject * mini_object)
{
  GstMiniObjectClass *mo_class;

  mo_class = GST_MINI_OBJECT_GET_CLASS (mini_object);
  mo_class->finalize (mini_object);

  g_type_free_instance ((GTypeInstance *) mini_object);
}

void
gst_mini_object_unref (GstMiniObject * mini_object)
{
  g_return_if_fail (mini_object != NULL);
  g_return_if_fail (mini_object->refcount > 0);

  if (g_atomic_int_dec_and_test (&mini_object->refcount)) {
    gst_mini_object_free (mini_object);
  }
}

void
gst_mini_object_replace (GstMiniObject ** olddata, GstMiniObject * newdata)
{
  GstMiniObject *olddata_val;

  if (newdata) {
    gst_mini_object_ref (newdata);
  }

  do {
    olddata_val = *olddata;
  } while (!g_atomic_pointer_compare_and_exchange ((gpointer *) olddata,
          olddata_val, newdata));

  if (olddata_val) {
    gst_mini_object_unref (olddata_val);
  }
}

static void
gst_value_mini_object_init (GValue * value)
{
  value->data[0].v_pointer = NULL;
}

static void
gst_value_mini_object_free (GValue * value)
{
  if (value->data[0].v_pointer) {
    gst_mini_object_unref (GST_MINI_OBJECT (value->data[0].v_pointer));
  }
}

static void
gst_value_mini_object_copy (const GValue * src_value, GValue * dest_value)
{
  if (src_value->data[0].v_pointer) {
    dest_value->data[0].v_pointer =
        gst_mini_object_ref (GST_MINI_OBJECT (src_value->data[0].v_pointer));
  } else {
    dest_value->data[0].v_pointer = NULL;
  }
}

static gpointer
gst_value_mini_object_peek_pointer (const GValue * value)
{
  return value->data[0].v_pointer;
}

static gchar *
gst_value_mini_object_collect (GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  value->data[0].v_pointer = collect_values[0].v_pointer;

  return NULL;
}

static gchar *
gst_value_mini_object_lcopy (const GValue * value, guint n_collect_values,
    GTypeCValue * collect_values, guint collect_flags)
{
  gpointer *mini_object_p = collect_values[0].v_pointer;

  if (!mini_object_p) {
    return g_strdup_printf ("value location for '%s' passed as NULL",
        G_VALUE_TYPE_NAME (value));
  }

  *mini_object_p = value->data[0].v_pointer;

  return NULL;
}

/* param spec */

static GType gst_param_spec_mini_object_get_type (void);

#define GST_TYPE_PARAM_SPEC_MINI_OBJECT (gst_param_spec_mini_object_get_type())
#define GST_PARAM_SPEC_MINI_OBJECT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_PARAM_SPEC_MINI_OBJECT, GstParamSpecMiniObject))

typedef struct _GstParamSpecMiniObject GstParamSpecMiniObject;
struct _GstParamSpecMiniObject
{
  GParamSpec parent_instance;
};

static void
param_mini_object_init (GParamSpec * pspec)
{
  /* GParamSpecMiniObject *ospec = G_PARAM_SPEC_MINI_OBJECT (pspec); */
}

static void
param_mini_object_set_default (GParamSpec * pspec, GValue * value)
{
  value->data[0].v_pointer = NULL;
}

static gboolean
param_mini_object_validate (GParamSpec * pspec, GValue * value)
{
  GstParamSpecMiniObject *ospec = GST_PARAM_SPEC_MINI_OBJECT (pspec);
  GstMiniObject *mini_object = value->data[0].v_pointer;
  guint changed = 0;

  if (mini_object
      && !g_value_type_compatible (G_OBJECT_TYPE (mini_object),
          G_PARAM_SPEC_VALUE_TYPE (ospec))) {
    gst_mini_object_unref (mini_object);
    value->data[0].v_pointer = NULL;
    changed++;
  }

  return changed;
}

static gint
param_mini_object_values_cmp (GParamSpec * pspec,
    const GValue * value1, const GValue * value2)
{
  guint8 *p1 = value1->data[0].v_pointer;
  guint8 *p2 = value2->data[0].v_pointer;

  /* not much to compare here, try to at least provide stable lesser/greater result */

  return p1 < p2 ? -1 : p1 > p2;
}

static GType
gst_param_spec_mini_object_get_type (void)
{
  static GType type;

  if (G_UNLIKELY (type) == 0) {
    static const GParamSpecTypeInfo pspec_info = {
      sizeof (GstParamSpecMiniObject),  /* instance_size */
      16,                       /* n_preallocs */
      param_mini_object_init,   /* instance_init */
      G_TYPE_OBJECT,            /* value_type */
      NULL,                     /* finalize */
      param_mini_object_set_default,    /* value_set_default */
      param_mini_object_validate,       /* value_validate */
      param_mini_object_values_cmp,     /* values_cmp */
    };
    type = g_param_type_register_static ("GParamSpecMiniObject", &pspec_info);
  }

  return type;
}

GParamSpec *
gst_param_spec_mini_object (const char *name, const char *nick,
    const char *blurb, GType object_type, GParamFlags flags)
{
  GstParamSpecMiniObject *ospec;

  g_return_val_if_fail (g_type_is_a (object_type, GST_TYPE_MINI_OBJECT), NULL);

  ospec = g_param_spec_internal (GST_TYPE_PARAM_SPEC_MINI_OBJECT,
      name, nick, blurb, flags);
  G_PARAM_SPEC (ospec)->value_type = object_type;

  return G_PARAM_SPEC (ospec);
}
