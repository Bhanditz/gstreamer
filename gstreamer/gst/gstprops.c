/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstprops.c: Properties subsystem for generic usage
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

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

#include "gstprops.h"
#include "gstpropsprivate.h"

static GMemChunk *_gst_props_entries_chunk;
static GMutex *_gst_props_entries_chunk_lock;

static GMemChunk *_gst_props_chunk;
static GMutex *_gst_props_chunk_lock;

static gboolean 	gst_props_entry_check_compatibility 	(GstPropsEntry *entry1, GstPropsEntry *entry2);
	
static guint _arg_len[] = {
  0,  // GST_PROPS_END_ID_NUM = 0,
  0,  // GST_PROPS_LIST_ID_NUM,
  1,  // GST_PROPS_INT_ID_NUM,
  2,  // GST_PROPS_INT_RANGE_ID_NUM,
  1,  // GST_PROPS_FOURCC_ID_NUM,
  1,  // GST_PROPS_BOOL_ID_NUM,
};

void 
_gst_props_initialize (void) 
{
  _gst_props_entries_chunk = g_mem_chunk_new ("GstPropsEntries", 
		  sizeof (GstPropsEntry), sizeof (GstPropsEntry) * 256, 
		  G_ALLOC_AND_FREE);
  _gst_props_entries_chunk_lock = g_mutex_new ();

  _gst_props_chunk = g_mem_chunk_new ("GstProps", 
		  sizeof (GstProps), sizeof (GstProps) * 256, 
		  G_ALLOC_AND_FREE);
  _gst_props_chunk_lock = g_mutex_new ();
}

static GstPropsEntry*
gst_props_create_entry (GstPropsFactory factory, gint *skipped)
{
  GstPropsFactoryEntry tag;
  GstPropsEntry *entry;
  guint i=0;

  g_mutex_lock (_gst_props_entries_chunk_lock);
  entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
  g_mutex_unlock (_gst_props_entries_chunk_lock);

  tag = factory[i++];
  switch (GPOINTER_TO_INT (tag)) {
    case GST_PROPS_INT_ID_NUM:
      entry->propstype = GST_PROPS_INT_ID_NUM;
      entry->data.int_data = GPOINTER_TO_INT (factory[i++]);
      break;
    case GST_PROPS_INT_RANGE_ID_NUM:
      entry->propstype = GST_PROPS_INT_RANGE_ID_NUM;
      entry->data.int_range_data.min = GPOINTER_TO_INT (factory[i++]);
      entry->data.int_range_data.max = GPOINTER_TO_INT (factory[i++]);
      break;
    case GST_PROPS_FOURCC_ID_NUM:
      entry->propstype = GST_PROPS_FOURCC_ID_NUM;
      entry->data.fourcc_data = GPOINTER_TO_INT (factory[i++]);
      break;
    case GST_PROPS_LIST_ID_NUM:
      g_warning ("gstprops: list not allowed in list\n");
      break;
    case GST_PROPS_BOOL_ID_NUM:
      entry->propstype = GST_PROPS_BOOL_ID_NUM;
      entry->data.bool_data = GPOINTER_TO_INT (factory[i++]);
      break;
    default:
      g_warning ("gstprops: unknown props id found\n");
      g_mutex_lock (_gst_props_entries_chunk_lock);
      g_mem_chunk_free (_gst_props_entries_chunk, entry);
      g_mutex_unlock (_gst_props_entries_chunk_lock);
      entry = NULL;
      break;
  }

  *skipped = i;

  return entry;
}


static gint 
props_compare_func (gconstpointer a,
		    gconstpointer b) 
{
  GstPropsEntry *entry1 = (GstPropsEntry *)a;
  GstPropsEntry *entry2 = (GstPropsEntry *)b;

  return (entry1->propid - entry2->propid);
}

static gint 
props_find_func (gconstpointer a,
		 gconstpointer b) 
{
  GstPropsEntry *entry2 = (GstPropsEntry *)a;
  GQuark entry1 = (GQuark) GPOINTER_TO_INT (b);

  return (entry1 - entry2->propid);
}

/**
 * gst_props_register:
 * @factory: the factory to register
 *
 * Register the factory. 
 *
 * Returns: the new property created from the factory
 */
GstProps *
gst_props_register (GstPropsFactory factory)
{
  guint dummy;

  return gst_props_register_count (factory, &dummy);
}

/**
 * gst_props_register_count:
 * @factory: the factory to register
 * @counter: count how many fields were consumed
 *
 * Register the factory. 
 *
 * Returns: the new property created from the factory
 */
GstProps *
gst_props_register_count (GstPropsFactory factory, guint *counter)
{
  GstPropsFactoryEntry tag;
  gint i = 0;
  GstProps *props = NULL;
  gint skipped;
  
  g_return_val_if_fail (factory != NULL, NULL);

  tag = factory[i++];

  if (!tag) goto end;

  g_mutex_lock (_gst_props_chunk_lock);
  props = g_mem_chunk_alloc (_gst_props_chunk);
  g_mutex_unlock (_gst_props_chunk_lock);

  g_return_val_if_fail (props != NULL, NULL);

  props->properties = NULL;
  props->refcount = 1;
  
  while (tag) {
    GQuark quark;
    GstPropsEntry *entry;
    
    if (tag < GST_PROPS_LAST_ID) {
      g_warning ("properties seem to be wrong\n");
      return NULL;
    }
      
    quark = g_quark_from_string ((gchar *)tag);

    tag = factory[i];
    switch (GPOINTER_TO_INT (tag)) {
      case GST_PROPS_LIST_ID_NUM: 
      {
        GstPropsEntry *list_entry;

        g_mutex_lock (_gst_props_entries_chunk_lock);
        entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
        g_mutex_unlock (_gst_props_entries_chunk_lock);

	entry->propid = quark;
	entry->propstype = GST_PROPS_LIST_ID_NUM;
	entry->data.list_data.entries = NULL;

	i++; // skip list tag
        tag = factory[i];
	while (tag) {
	  list_entry = gst_props_create_entry (&factory[i], &skipped);
	  list_entry->propid = quark;
	  i += skipped;
          tag = factory[i];
	  entry->data.list_data.entries = g_list_prepend (entry->data.list_data.entries, list_entry);
	}
	entry->data.list_data.entries = g_list_reverse (entry->data.list_data.entries);
	i++; //skip NULL (list end)
	break;
      }
      default:
      {
	entry = gst_props_create_entry (&factory[i], &skipped);
	entry->propid = quark;
	i += skipped;
	break;
      }
    }
    props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
     
    tag = factory[i++];
  }

end:
  *counter = i;

  return props;
}

/**
 * gst_props_new:
 * @entry: the property entries for the property
 * @...: the property entries for the property
 *
 * Create a new property from the list of entries.
 *
 * Returns: the new property created from the list of entries
 */
GstProps*
gst_props_new (GstPropsFactoryEntry entry, ...)
{
  va_list var_args;
  GstPropsFactoryEntry value;
  gint i = 0;
  gint size, skip;
  GstPropsFactoryEntry *factory;
  gboolean inlist = FALSE;
  GstProps *props;

#define add_value(value) {\
    GST_DEBUG (0,"%d %p\n", i, value);\
    factory[i++] = value;  \
    if (i >= size) {       \
      size += 16;          \
      factory = (GstPropsFactoryEntry *) g_realloc (factory, size*sizeof(GstPropsFactoryEntry));\
    }\
}

  size = 16;
  factory = (GstPropsFactoryEntry *) g_malloc (size*sizeof(GstPropsFactoryEntry));

  va_start (var_args, entry);
  // property name
  value = (GstPropsFactoryEntry) entry;
  
  // properties
  while (value) {
    if (!inlist) {
      // add name
      add_value (value);

      // get value
      value = va_arg (var_args, GstPropsFactoryEntry);
    }
    switch (GPOINTER_TO_INT (value)) {
      case GST_PROPS_END_ID_NUM: 
	g_assert (inlist == TRUE);

	inlist = FALSE;
	skip = 0;
	break;
      case GST_PROPS_LIST_ID_NUM: 
      {
	g_assert (inlist == FALSE);

	skip = 0;
	inlist = TRUE;
	break;
      }
      default:
	skip = _arg_len[GPOINTER_TO_INT (value)];
        break;
    }
    do {
      add_value (value);
      value = va_arg (var_args, GstPropsFactoryEntry);
    }
    while (skip--);
  }
  factory[i++] = NULL;

  props = gst_props_register (factory);

  return props;
}

/**
 * gst_props_set:
 * @props: the props to modify
 * @name: the name of the entry to modify
 * @entry: The new value of the property entry
 * @...: More property entries.
 *
 * Modifies the value of the given entry in the props struct.
 *
 * Returns: the new modified property structure.
 */
GstProps*
gst_props_set (GstProps *props, const gchar *name, GstPropsFactoryEntry entry, ...)
{
  GQuark quark;
  GList *lentry;
  va_list var_args;
  
  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;
    GstPropsFactoryEntry value;

    thisentry = (GstPropsEntry *)lentry->data;

    va_start (var_args, entry);
    // property name
    value = (GstPropsFactoryEntry) entry;

    switch (GPOINTER_TO_INT (value)) {
      case GST_PROPS_INT_ID:
        thisentry->propstype = GST_PROPS_INT_ID_NUM;
        value = va_arg (var_args, GstPropsFactoryEntry);
        thisentry->data.int_data = GPOINTER_TO_INT (value);
	break;
      case GST_PROPS_FOURCC_ID_NUM:
        thisentry->propstype = GST_PROPS_FOURCC_ID_NUM;
        value = va_arg (var_args, GstPropsFactoryEntry);
        thisentry->data.fourcc_data = GPOINTER_TO_INT (value);
        break;
      case GST_PROPS_BOOL_ID_NUM:
        thisentry->propstype = GST_PROPS_BOOL_ID_NUM;
        value = va_arg (var_args, GstPropsFactoryEntry);
        thisentry->data.bool_data = GPOINTER_TO_INT (value);
        break;
      default:
        g_print("gstprops: type not allowed\n");
	break;
    }
  }
  else {
    g_print("gstprops: no property '%s' to change\n", name);
  }

  return props;
}

/**
 * gst_props_unref:
 * @props: the props to unref
 *
 * Decrease the refcount of the property structure, destroying
 * the property if the refcount is 0.
 */
void
gst_props_unref (GstProps *props)
{
  g_return_if_fail (props != NULL);
  
  props->refcount--;

  if (props->refcount == 0)
    gst_props_destroy (props);
}

/**
 * gst_props_ref:
 * @props: the props to ref
 *
 * Increase the refcount of the property structure.
 */
void
gst_props_ref (GstProps *props)
{
  g_return_if_fail (props != NULL);
  
  props->refcount++;
}

/**
 * gst_props_destroy:
 * @props: the props to destroy
 *
 * Destroy the property, freeing all the memory that
 * was allocated.
 */
void
gst_props_destroy (GstProps *props)
{
  GList *entries;

  g_return_if_fail (props != NULL);
  
  entries = props->properties;

  while (entries) {
    GstPropsEntry *entry = (GstPropsEntry *)entries->data;

    // FIXME also free the lists
    g_mutex_lock (_gst_props_entries_chunk_lock);
    g_mem_chunk_free (_gst_props_entries_chunk, entry);
    g_mutex_unlock (_gst_props_entries_chunk_lock);

    entries = g_list_next (entries);
  }

  g_list_free (props->properties);
}

/**
 * gst_props_copy:
 * @props: the props to copy
 *
 * Copy the property structure.
 *
 * Returns: the new property that is a copy of the original
 * one.
 */
GstProps*
gst_props_copy (GstProps *props)
{
  GstProps *new;
  GList *properties;

  g_return_val_if_fail (props != NULL, NULL);

  g_mutex_lock (_gst_props_chunk_lock);
  new = g_mem_chunk_alloc (_gst_props_chunk);
  g_mutex_unlock (_gst_props_chunk_lock);

  new->properties = NULL;

  properties = props->properties;

  while (properties) {
    GstPropsEntry *entry = (GstPropsEntry *)properties->data;
    GstPropsEntry *newentry;

    g_mutex_lock (_gst_props_entries_chunk_lock);
    newentry = g_mem_chunk_alloc (_gst_props_entries_chunk);
    g_mutex_unlock (_gst_props_entries_chunk_lock);

    // FIXME copy lists too
    memcpy (newentry, entry, sizeof (GstPropsEntry));

    new->properties = g_list_prepend (new->properties, newentry);
    
    properties = g_list_next (properties);
  }
  new->properties = g_list_reverse (new->properties);

  return new;
}

/**
 * gst_props_copy_on_write:
 * @props: the props to copy on write
 *
 * Copy the property structure if the refcount is >1.
 *
 * Returns: A new props that can be safely written to.
 */
GstProps*
gst_props_copy_on_write (GstProps *props)
{
  GstProps *new = props;;

  g_return_val_if_fail (props != NULL, NULL);

  if (props->refcount > 1) {
    new = gst_props_copy (props);
    gst_props_unref (props);
  }

  return props;
}

/**
 * gst_props_get_int:
 * @props: the props to get the int value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as an integer.
 *
 * Returns: the integer value of the named entry, 0 if not found.
 */
gint
gst_props_get_int (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.int_data;
  }
  
  return 0;
}

/**
 * gst_props_get_fourcc_int:
 * @props: the props to get the fourcc value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as a gulong fourcc.
 *
 * Returns: the fourcc value of the named entry, 0 if not found.
 */
gulong
gst_props_get_fourcc_int (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.fourcc_data;
  }
  
  return 0;
}

/**
 * gst_props_get_boolean:
 * @props: the props to get the fourcc value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as a boolean value.
 *
 * Returns: the boolean value of the named entry, 0 if not found.
 */
gboolean
gst_props_get_boolean (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.bool_data;
  }
  
  return 0;
}

/**
 * gst_props_merge:
 * @props: the property to merge into
 * @tomerge: the property to merge 
 *
 * Merge the properties of tomerge into props.
 *
 * Returns: the new merged property 
 */
GstProps*
gst_props_merge (GstProps *props, GstProps *tomerge)
{
  GList *merge_props;

  g_return_val_if_fail (props != NULL, NULL);
  g_return_val_if_fail (tomerge != NULL, NULL);

  merge_props = tomerge->properties;

  // FIXME do proper merging here...
  while (merge_props) {
    GstPropsEntry *entry = (GstPropsEntry *)merge_props->data;

    props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
	  
    merge_props = g_list_next (merge_props);
  }

  return props;
}


/* entry2 is always a list, entry1 never is */
static gboolean
gst_props_entry_check_list_compatibility (GstPropsEntry *entry1, GstPropsEntry *entry2)
{
  GList *entrylist = entry2->data.list_data.entries;
  gboolean found = FALSE;

  while (entrylist && !found) {
    GstPropsEntry *entry = (GstPropsEntry *) entrylist->data;

    found |= gst_props_entry_check_compatibility (entry1, entry);

    entrylist = g_list_next (entrylist);
  }

  return found;
}

static gboolean
gst_props_entry_check_compatibility (GstPropsEntry *entry1, GstPropsEntry *entry2)
{
  GST_DEBUG (0,"compare: %s %s\n", g_quark_to_string (entry1->propid),
	                     g_quark_to_string (entry2->propid));
  switch (entry1->propstype) {
    case GST_PROPS_LIST_ID_NUM:
    {
      GList *entrylist = entry1->data.list_data.entries;
      gboolean valid = TRUE;    // innocent until proven guilty

      while (entrylist && valid) {
	GstPropsEntry *entry = (GstPropsEntry *) entrylist->data;

	valid &= gst_props_entry_check_compatibility (entry, entry2);
	
	entrylist = g_list_next (entrylist);
      }
      
      return valid;
    }
    case GST_PROPS_INT_RANGE_ID_NUM:
      switch (entry2->propstype) {
	// a - b   <--->   a - c
        case GST_PROPS_INT_RANGE_ID_NUM:
	  return (entry2->data.int_range_data.min <= entry1->data.int_range_data.min &&
	          entry2->data.int_range_data.max >= entry1->data.int_range_data.max);
        case GST_PROPS_LIST_ID_NUM:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_FOURCC_ID_NUM:
      switch (entry2->propstype) {
	// b   <--->   a
        case GST_PROPS_FOURCC_ID_NUM:
	  return (entry2->data.fourcc_data == entry1->data.fourcc_data);
	// b   <--->   a,b,c
        case GST_PROPS_LIST_ID_NUM:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_INT_ID_NUM:
      switch (entry2->propstype) {
	// b   <--->   a - d
        case GST_PROPS_INT_RANGE_ID_NUM:
	  return (entry2->data.int_range_data.min <= entry1->data.int_data &&
	          entry2->data.int_range_data.max >= entry1->data.int_data);
	// b   <--->   a
        case GST_PROPS_INT_ID_NUM:
	  return (entry2->data.int_data == entry1->data.int_data);
	// b   <--->   a,b,c
        case GST_PROPS_LIST_ID_NUM:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_BOOL_ID_NUM:
      switch (entry2->propstype) {
	// t   <--->   t
        case GST_PROPS_BOOL_ID_NUM:
          return (entry2->data.bool_data == entry1->data.bool_data);
        case GST_PROPS_LIST_ID_NUM:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
    default:
      break;
  }

  return FALSE;
}

/**
 * gst_props_check_compatibility:
 * @fromprops: a property
 * @toprops: a property
 *
 * Checks whether two capabilities are compatible.
 *
 * Returns: TRUE if compatible, FALSE otherwise
 */
gboolean
gst_props_check_compatibility (GstProps *fromprops, GstProps *toprops)
{
  GList *sourcelist;
  GList *sinklist;
  gint missing = 0;
  gint more = 0;
  gboolean compatible = TRUE;

  g_return_val_if_fail (fromprops != NULL, FALSE);
  g_return_val_if_fail (toprops != NULL, FALSE);
	
  sourcelist = fromprops->properties;
  sinklist   = toprops->properties;

  while (sourcelist && sinklist && compatible) {
    GstPropsEntry *entry1;
    GstPropsEntry *entry2;

    entry1 = (GstPropsEntry *)sourcelist->data;
    entry2 = (GstPropsEntry *)sinklist->data;

    while (entry1->propid < entry2->propid) {
      GST_DEBUG (0,"source is more specific in \"%s\"\n", g_quark_to_string (entry1->propid));
      more++;
      sourcelist = g_list_next (sourcelist);
      if (sourcelist) entry1 = (GstPropsEntry *)sourcelist->data;
      else goto end;
    }
    while (entry1->propid > entry2->propid) {
      GST_DEBUG (0,"source has missing property \"%s\"\n", g_quark_to_string (entry2->propid));
      missing++;
      sinklist = g_list_next (sinklist);
      if (sinklist) entry2 = (GstPropsEntry *)sinklist->data;
      else goto end;
    }

    if (!gst_props_entry_check_compatibility (entry1, entry2)) {
	compatible = FALSE;
	GST_DEBUG (0, "%s and %s are not compatible\n",
		   g_quark_to_string (entry1->propid),
		   g_quark_to_string (entry2->propid));
    }

    sourcelist = g_list_next (sourcelist);
    sinklist = g_list_next (sinklist);
  }
  if (sinklist && compatible) {
    GstPropsEntry *entry2;
    entry2 = (GstPropsEntry *)sinklist->data;
    missing++;
    GST_DEBUG (0,"source has missing property \"%s\"\n", g_quark_to_string (entry2->propid));
  }
end:

  if (missing)
    return FALSE;

  return compatible;
}

static xmlNodePtr
gst_props_save_thyself_func (GstPropsEntry *entry, xmlNodePtr parent)
{
  xmlNodePtr subtree;
  gchar *str;

  switch (entry->propstype) {
    case GST_PROPS_INT_ID_NUM: 
      subtree = xmlNewChild (parent, NULL, "int", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%d", entry->data.int_data);
      xmlNewProp (subtree, "value", str);
      g_free(str);
      break;
    case GST_PROPS_INT_RANGE_ID_NUM: 
      subtree = xmlNewChild (parent, NULL, "range", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%d", entry->data.int_range_data.min);
      xmlNewProp (subtree, "min", str);
      g_free(str);
      str = g_strdup_printf ("%d", entry->data.int_range_data.max);
      xmlNewProp (subtree, "max", str);
      g_free(str);
      break;
    case GST_PROPS_FOURCC_ID_NUM: 
      str = g_strdup_printf ("%4.4s", (gchar *)&entry->data.fourcc_data);
      xmlAddChild (parent, xmlNewComment (str));
      g_free(str);
      subtree = xmlNewChild (parent, NULL, "fourcc", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%08x", entry->data.fourcc_data);
      xmlNewProp (subtree, "hexvalue", str);
      g_free(str);
      break;
    case GST_PROPS_BOOL_ID_NUM: 
      subtree = xmlNewChild (parent, NULL, "boolean", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      xmlNewProp (subtree, "value", (entry->data.bool_data ?  "true" : "false"));
      break;
    default:
      break;
  }

  return parent;
}

/**
 * gst_props_save_thyself:
 * @props: a property to save
 * @parent: the parent XML tree
 *
 * Saves the property into an XML representation.
 *
 * Returns: the new XML tree
 */
xmlNodePtr
gst_props_save_thyself (GstProps *props, xmlNodePtr parent)
{
  GList *proplist;
  xmlNodePtr subtree;

  g_return_val_if_fail (props != NULL, NULL);

  proplist = props->properties;

  while (proplist) {
    GstPropsEntry *entry = (GstPropsEntry *) proplist->data;

    switch (entry->propstype) {
      case GST_PROPS_LIST_ID_NUM: 
        subtree = xmlNewChild (parent, NULL, "list", NULL);
        xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
        g_list_foreach (entry->data.list_data.entries, (GFunc) gst_props_save_thyself_func, subtree);
      default:
    	gst_props_save_thyself_func (entry, parent);
    }

    proplist = g_list_next (proplist);
  }
  
  return parent;
}

static GstPropsEntry*
gst_props_load_thyself_func (xmlNodePtr field)
{
  GstPropsEntry *entry;
  gchar *prop;

  g_mutex_lock (_gst_props_entries_chunk_lock);
  entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
  g_mutex_unlock (_gst_props_entries_chunk_lock);

  if (!strcmp(field->name, "int")) {
    entry->propstype = GST_PROPS_INT_ID_NUM;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp(field, "value");
    sscanf (prop, "%d", &entry->data.int_data);
    g_free (prop);
  }
  else if (!strcmp(field->name, "range")) {
    entry->propstype = GST_PROPS_INT_RANGE_ID_NUM;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "min");
    sscanf (prop, "%d", &entry->data.int_range_data.min);
    g_free (prop);
    prop = xmlGetProp (field, "max");
    sscanf (prop, "%d", &entry->data.int_range_data.max);
    g_free (prop);
  }
  else if (!strcmp(field->name, "boolean")) {
    entry->propstype = GST_PROPS_BOOL_ID_NUM;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "value");
    if (!strcmp (prop, "false")) entry->data.bool_data = 0;
    else entry->data.bool_data = 1;
    g_free (prop);
  }
  else if (!strcmp(field->name, "fourcc")) {
    entry->propstype = GST_PROPS_FOURCC_ID_NUM;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "hexvalue");
    sscanf (prop, "%08x", &entry->data.fourcc_data);
    g_free (prop);
  }
  else {
    g_mutex_lock (_gst_props_entries_chunk_lock);
    g_mem_chunk_free (_gst_props_entries_chunk, entry);
    g_mutex_unlock (_gst_props_entries_chunk_lock);
    entry = NULL;
  }

  return entry;
}

/**
 * gst_props_load_thyself:
 * @parent: the XML tree to load from
 *
 * Creates a new property out of an XML tree.
 *
 * Returns: the new property
 */
GstProps*
gst_props_load_thyself (xmlNodePtr parent)
{
  GstProps *props;
  xmlNodePtr field = parent->xmlChildrenNode;
  gchar *prop;

  g_mutex_lock (_gst_props_chunk_lock);
  props = g_mem_chunk_alloc (_gst_props_chunk);
  g_mutex_unlock (_gst_props_chunk_lock);

  props->properties = NULL;
  props->refcount = 1;

  while (field) {
    if (!strcmp (field->name, "list")) {
      GstPropsEntry *entry;
      xmlNodePtr subfield = field->xmlChildrenNode;

      g_mutex_lock (_gst_props_entries_chunk_lock);
      entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
      g_mutex_unlock (_gst_props_entries_chunk_lock);

      entry->propstype = GST_PROPS_LIST_ID_NUM;
      entry->data.list_data.entries = NULL;
      prop = xmlGetProp (field, "name");
      entry->propid = g_quark_from_string (prop);
      g_free (prop);

      while (subfield) {
        GstPropsEntry *subentry = gst_props_load_thyself_func (subfield);

	if (subentry)
	  entry->data.list_data.entries = g_list_prepend (entry->data.list_data.entries, subentry);

        subfield = subfield->next;
      }
      entry->data.list_data.entries = g_list_reverse (entry->data.list_data.entries);
      props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
    }
    else {
      GstPropsEntry *entry;

      entry = gst_props_load_thyself_func (field);

      props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
    }
    field = field->next;
  }

  return props;
}

