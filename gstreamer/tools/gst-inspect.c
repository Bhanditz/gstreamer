#include <gst/gst.h>
#include <gst/control/control.h>
#include <string.h>

static void 
print_prop (GstPropsEntry *prop, gboolean showname, const gchar *pfx) 
{
  GstPropsType type;

  if (showname)
    g_print("%s%-20.20s: ", pfx, gst_props_entry_get_name (prop));
  else
    g_print(pfx);

  type = gst_props_entry_get_type (prop);

  switch (type) {
    case GST_PROPS_INT_TYPE:
    {
      gint val;
      gst_props_entry_get_int (prop, &val);
      g_print("Integer: %d\n", val);
      break;
    }
    case GST_PROPS_INT_RANGE_TYPE:
    {
      gint min, max;
      gst_props_entry_get_int_range (prop, &min, &max);
      g_print("Integer range: %d - %d\n", min, max);
      break;
    }
    case GST_PROPS_FLOAT_TYPE:
    {
      gfloat val;
      gst_props_entry_get_float (prop, &val);
      g_print("Float: %f\n", val);
      break;
    }
    case GST_PROPS_FLOAT_RANGE_TYPE:
    {
      gfloat min, max;
      gst_props_entry_get_float_range (prop, &min, &max);
      g_print("Float range: %f - %f\n", min, max);
      break;
    }
    case GST_PROPS_BOOLEAN_TYPE:
    {
      gboolean val;
      gst_props_entry_get_boolean (prop, &val);
      g_print("Boolean: %s\n", val ? "TRUE" : "FALSE");
      break;
    }
    case GST_PROPS_STRING_TYPE:
    {
      const gchar *val;
      gst_props_entry_get_string (prop, &val);
      g_print("String: \"%s\"\n", val);
      break;
    }
    case GST_PROPS_FOURCC_TYPE:
    {
      guint32 val;
      gst_props_entry_get_fourcc_int (prop, &val);
      g_print("FourCC: '%c%c%c%c'\n",
             (gchar)( val        & 0xff), 
	     (gchar)((val >> 8)  & 0xff),
             (gchar)((val >> 16) & 0xff), 
	     (gchar)((val >> 24) & 0xff));
      break;
    }
    case GST_PROPS_LIST_TYPE:
    {
      const GList *list;
      gchar *longprefix;

      gst_props_entry_get_list (prop, &list);
      g_print ("List:\n");
      longprefix = g_strdup_printf ("%s ", pfx);
      while (list) {
        GstPropsEntry *listentry;

        listentry = (GstPropsEntry*) (list->data);
        print_prop (listentry, FALSE, longprefix);

        list = g_list_next (list);
      }
      g_free (longprefix);
      break;
    }
    default:
      g_print("unknown props %d\n", type);
  }
}

static void 
print_props (GstProps *properties, const gchar *pfx) 
{
  GList *props;
  GstPropsEntry *prop;

  props = properties->properties;
  while (props) {
    prop = (GstPropsEntry*)(props->data);
    props = g_list_next(props);

    print_prop(prop,TRUE,pfx);
  }
}

static void 
print_caps (const GstCaps *caps, const gchar *pfx) 
{
  while (caps) {
    GstType *type;

    g_print ("%s'%s': (%sfixed)\n", pfx, caps->name, (GST_CAPS_IS_FIXED (caps) ? "" : "NOT "));

    type = gst_type_find_by_id (caps->id);
    if (type) 
      g_print ("%s  MIME type: '%s':\n", pfx, type->mime);
    else
      g_print ("%s  MIME type: 'unknown/unknown':\n", pfx);

    if (caps->properties) {
      gchar *prefix = g_strdup_printf ("%s  ", pfx);

      print_props(caps->properties, prefix);

      g_free (prefix);
    }

    caps = caps->next;
  }
}

static void 
print_formats (const GstFormat *formats) 
{
  while (formats && *formats) {
    const GstFormatDefinition *definition;

    definition = gst_format_get_details (*formats);
    if (definition)
      g_print ("\t\t(%d):\t%s (%s)\n", *formats,
	       definition->nick, definition->description);
    else
      g_print ("\t\t(%d):\tUnknown format\n", *formats);

    formats++;
  }
}

static void 
print_query_types (const GstQueryType *types) 
{
  while (types && *types) {
    const GstQueryTypeDefinition *definition;

    definition = gst_query_type_get_details (*types);
    if (definition)
      g_print ("\t\t(%d):\t%s (%s)\n", *types,
	       definition->nick, definition->description);
    else
      g_print ("\t\t(%d):\tUnknown query format\n", *types);

    types++;
  }
}

static void 
print_event_masks (const GstEventMask *masks) 
{
  GType event_type;
  GEnumClass *klass;
  GType event_flags;
  GFlagsClass *flags_class = NULL;

  event_type = gst_event_type_get_type();
  klass = (GEnumClass *) g_type_class_ref (event_type);

  while (masks && masks->type) {
    GEnumValue *value;
    gint flags = 0, index = 0;

    switch (masks->type) {
      case GST_EVENT_SEEK:
        flags = masks->flags;
	event_flags = gst_seek_type_get_type ();
  	flags_class = (GFlagsClass *) g_type_class_ref (event_flags);
        break;
      default:
        break;
    }
    
    value = g_enum_get_value (klass, masks->type);
    g_print ("\t\t%s ", value->value_nick);

    while (flags) {
      GFlagsValue *value;

      if (flags & 1) {
        value = g_flags_get_first_value (flags_class, 1 << index);

	if (value)
          g_print ("| %s ", value->value_nick);
	else
          g_print ("| ? ");
      }
      flags >>= 1;
      index++;
    }
    g_print ("\n");
    
    masks++;
  }
}

static void
output_hierarchy (GType type, gint level, gint *maxlevel)
{
  GType parent;
  gint i;

  parent = g_type_parent (type);

  *maxlevel = *maxlevel + 1;
  level++;

  if (parent)
    output_hierarchy (parent, level, maxlevel);
  
  for (i=1; i<*maxlevel-level; i++)
   g_print ("      ");
  if (*maxlevel-level)
    g_print (" +----");

  g_print ("%s\n", g_type_name (type));
	  
  if (level == 1)
    g_print ("\n");
}

static void
print_element_properties (GstElement *element) 
{
  GParamSpec **property_specs;
  gint num_properties,i;
  gboolean readable;
  

  property_specs = g_object_class_list_properties 
	             (G_OBJECT_GET_CLASS (element), &num_properties);
  g_print("\nElement Arguments:\n");

  for (i = 0; i < num_properties; i++) {
    GValue value = { 0, };
    GParamSpec *param = property_specs[i];
    readable = FALSE;

    g_value_init (&value, param->value_type);
    if (param->flags & G_PARAM_READABLE) {
      g_object_get_property (G_OBJECT (element), param->name, &value);
      readable = TRUE;
    }

    g_print("  %-20s: %s\n", g_param_spec_get_name (param),
		               g_param_spec_get_blurb (param));

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING: 
	g_print ("%-23.23s String. ", "");
	if (readable) g_print ("(Default \"%s\")", g_value_get_string (&value));
	break;
      case G_TYPE_BOOLEAN: 
	g_print ("%-23.23s Boolean. ", "");
	if (readable) g_print ("(Default %s)", (g_value_get_boolean (&value) ? "true" : "false"));
	break;
      case G_TYPE_ULONG: 
      {
	GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (param);
	g_print("%-23.23s Unsigned Long. ", ""); 
	if (readable) g_print("Range: %lu - %lu (Default %lu)",  
			pulong->minimum, pulong->maximum, g_value_get_ulong (&value));
	break;
      }
      case G_TYPE_LONG: 
      {
	GParamSpecLong *plong = G_PARAM_SPEC_LONG (param);
	g_print("%-23.23s Long. ", ""); 
	if (readable) g_print("Range: %ld - %ld (Default %ld)",  
			plong->minimum, plong->maximum, g_value_get_long (&value));
	break;
      }
      case G_TYPE_UINT: 
      {
	GParamSpecUInt *puint = G_PARAM_SPEC_UINT (param);
	g_print("%-23.23s Unsigned Integer. ", "");
	if (readable) g_print("Range: %u - %u (Default %u)",  
			puint->minimum, puint->maximum, g_value_get_uint (&value));
	break;
      }
      case G_TYPE_INT: 
      {
	GParamSpecInt *pint = G_PARAM_SPEC_INT (param);
	g_print("%-23.23s Integer. ", ""); 
	if (readable) g_print("Range: %d - %d (Default %d)", 
			pint->minimum, pint->maximum, g_value_get_int (&value));
	break;
      }
      case G_TYPE_UINT64: 
      {
	GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (param);
	g_print("%-23.23s Unsigned Integer64. ", ""); 
	if (readable) g_print("Range: %" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT " (Default %" G_GUINT64_FORMAT ")", 
			puint64->minimum, puint64->maximum, g_value_get_uint64 (&value));
	break;
      }
      case G_TYPE_INT64: 
      {
	GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (param);
	g_print("%-23.23s Integer64. ", ""); 
	if (readable) g_print("Range: %" G_GINT64_FORMAT " - %" G_GINT64_FORMAT " (Default %" G_GINT64_FORMAT ")", 
			pint64->minimum, pint64->maximum, g_value_get_int64 (&value));
	break;
      }
      case G_TYPE_FLOAT: 
      {
	GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (param);
	g_print("%-23.23s Float. Default: %-8.8s %15.7g\n", "", "", 
		g_value_get_float (&value));
	g_print("%-23.23s Range: %15.7g - %15.7g", "", 
	       pfloat->minimum, pfloat->maximum);
	break;
      }
      case G_TYPE_DOUBLE: 
      {
	GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (param);
	g_print("%-23.23s Double. Default: %-8.8s %15.7g\n", "", "", 
		g_value_get_double (&value));
	g_print("%-23.23s Range: %15.7g - %15.7g", "", 
	       pdouble->minimum, pdouble->maximum);
	break;
      }
      default:
        if (param->value_type == GST_TYPE_FILENAME) {
          g_print("%-23.23s Filename", "");
	}
        if (param->value_type == GST_TYPE_CAPS) {
          GstCaps *caps = g_value_peek_pointer (&value);

	  if (!caps) 
            g_print("%-23.23s Caps (NULL)", "");
	  else {
            print_caps (caps, "                           ");
	  }
	}
        else if (G_IS_PARAM_SPEC_ENUM (param)) {
          GEnumValue *values;
	  guint j = 0;
	  gint enum_value;

	  values = G_ENUM_CLASS (g_type_class_ref (param->value_type))->values;
	  enum_value = g_value_get_enum (&value);

	  while (values[j].value_name) {
	    if (values[j].value == enum_value)
	      break;
	    j++; 
	  }

          g_print ("%-23.23s Enum \"%s\" (default %d, \"%s\")", "", 
			  g_type_name (G_VALUE_TYPE (&value)),
			  enum_value, values[j].value_nick);

	  j = 0;
	  while (values[j].value_name) {
            g_print("\n%-23.23s    (%d): \t%s", "", 
			    values[j].value, values[j].value_nick);
	    j++; 
	  }
	  /* g_type_class_unref (ec); */
	}
        else if (G_IS_PARAM_SPEC_FLAGS (param)) {
          GFlagsValue *values;
	  guint j = 0;
	  gint flags_value;
	  GString *flags = NULL;

	  values = G_FLAGS_CLASS (g_type_class_ref (param->value_type))->values;
	  flags_value = g_value_get_flags (&value);

	  while (values[j].value_name) {
	    if (values[j].value & flags_value) {
	      if (flags) {
	        g_string_append_printf (flags, " | %s", values[j].value_nick);
	      }
	      else {
	        flags = g_string_new (values[j].value_nick);
	      }
	    }
	    j++;
	  }

          g_print ("%-23.23s Flags \"%s\" (default %d, \"%s\")", "", 
			  g_type_name (G_VALUE_TYPE (&value)),
			  flags_value, (flags ? flags->str : "(none)"));

	  j = 0;
	  while (values[j].value_name) {
            g_print("\n%-23.23s    (%d): \t%s", "", 
			    values[j].value, values[j].value_nick);
	    j++; 
	  }

	  if (flags)
	    g_string_free (flags, TRUE);
	}
	else if (G_IS_PARAM_SPEC_OBJECT (param)) {
	  g_print("%-23.23s Object of type \"%s\"", "",
			  g_type_name(param->value_type));
        }
	else {
          g_print ("%-23.23s Unknown type %ld \"%s\"", "",param->value_type, 
		  	g_type_name(param->value_type));
	}
        break;
    }
    if (!readable) 
      g_print (" Write only\n");
    else 
      g_print ("\n");
  }
  if (num_properties == 0) 
    g_print ("  none\n");
}

static gint
print_element_info (GstElementFactory *factory)
{
  GstElement *element;
  GstObjectClass *gstobject_class;
  GstElementClass *gstelement_class;
  GList *pads;
  GstPad *pad;
  GstRealPad *realpad;
  GstPadTemplate *padtemplate;
  GList *children;
  GstElement *child;
  gboolean have_flags;
  gint maxlevel = 0;

  element = gst_element_factory_create (factory, "element");
  if (!element) {
    g_print ("couldn't construct element for some reason\n");
    return -1;
  }

  gstobject_class = GST_OBJECT_CLASS (G_OBJECT_GET_CLASS (element));
  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  g_print ("Factory Details:\n");
  g_print ("  Long name:\t%s\n",   factory->details->longname);
  g_print ("  Class:\t%s\n",       factory->details->klass);
  g_print ("  License:\t%s\n",     factory->details->license);
  g_print ("  Description:\t%s\n", factory->details->description);
  g_print ("  Version:\t%s\n",     factory->details->version);
  g_print ("  Author(s):\t%s\n",   factory->details->author);
  g_print ("  Copyright:\t%s\n",   factory->details->copyright);
  g_print ("\n");

  output_hierarchy (G_OBJECT_TYPE (element), 0, &maxlevel);

  g_print ("Pad Templates:\n");
  if (factory->numpadtemplates) {
    pads = factory->padtemplates;
    while (pads) {
      padtemplate = (GstPadTemplate*)(pads->data);
      pads = g_list_next(pads);

      if (padtemplate->direction == GST_PAD_SRC)
        g_print ("  SRC template: '%s'\n", padtemplate->name_template);
      else if (padtemplate->direction == GST_PAD_SINK)
        g_print ("  SINK template: '%s'\n", padtemplate->name_template);
      else
        g_print ("  UNKNOWN!!! template: '%s'\n", padtemplate->name_template);

      if (padtemplate->presence == GST_PAD_ALWAYS)
        g_print ("    Availability: Always\n");
      else if (padtemplate->presence == GST_PAD_SOMETIMES)
        g_print ("    Availability: Sometimes\n");
      else if (padtemplate->presence == GST_PAD_REQUEST) {
        g_print ("    Availability: On request\n");
        g_print ("      Has request_new_pad() function: %s\n",
                GST_DEBUG_FUNCPTR_NAME (gstelement_class->request_new_pad));
      }
      else
        g_print ("    Availability: UNKNOWN!!!\n");

      if (padtemplate->caps) {
        g_print ("    Capabilities:\n");
	print_caps (padtemplate->caps, "      ");
      }

      g_print ("\n");
    }
  } else
    g_print ("  none\n");

  have_flags = FALSE;

  g_print ("\nElement Flags:\n");
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_COMPLEX)) {
    g_print ("  GST_ELEMENT_COMPLEX\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_DECOUPLED)) {
    g_print ("  GST_ELEMENT_DECOUPLED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_THREAD_SUGGESTED)) {
    g_print ("  GST_ELEMENT_THREADSUGGESTED\n");
    have_flags = TRUE;
  }
  if (GST_FLAG_IS_SET (element, GST_ELEMENT_EVENT_AWARE)) {
    g_print("  GST_ELEMENT_EVENT_AWARE\n");
    have_flags = TRUE;
  }
  if (!have_flags)
    g_print("  no flags set\n");

  if (GST_IS_BIN (element)) {
    g_print ("\nBin Flags:\n");
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_MANAGER)) {
      g_print ("  GST_BIN_FLAG_MANAGER\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET (element, GST_BIN_SELF_SCHEDULABLE)) {
      g_print ("  GST_BIN_SELF_SCHEDULABLE\n");
      have_flags = TRUE;
    }
    if (GST_FLAG_IS_SET (element, GST_BIN_FLAG_PREFER_COTHREADS)) {
      g_print ("  GST_BIN_FLAG_PREFER_COTHREADS\n");
      have_flags = TRUE;
    }
    if (!have_flags)
      g_print ("  no flags set\n");
  }



  g_print ("\nElement Implementation:\n");

  if (element->loopfunc)
    g_print ("  loopfunc()-based element: %s\n",
	    GST_DEBUG_FUNCPTR_NAME (element->loopfunc));
  else
    g_print ("  No loopfunc(), must be chain-based or not configured yet\n");

  g_print ("  Has change_state() function: %s\n",
          GST_DEBUG_FUNCPTR_NAME (gstelement_class->change_state));
#ifndef GST_DISABLE_LOADSAVE
  g_print ("  Has custom save_thyself() function: %s\n",
          GST_DEBUG_FUNCPTR_NAME (gstobject_class->save_thyself));
  g_print ("  Has custom restore_thyself() function: %s\n",
          GST_DEBUG_FUNCPTR_NAME (gstobject_class->restore_thyself));
#endif

  have_flags = FALSE;

  g_print ("\nClocking Interaction:\n");
  if (gst_element_requires_clock (element)) {
    g_print ("  element requires a clock\n");
    have_flags = TRUE;
  }
  if (gst_element_provides_clock (element)) {
    GstClock *clock;

    clock = gst_element_get_clock (element);
    if (clock)
      g_print ("  element provides a clock: %s\n", GST_OBJECT_NAME(clock));
    else
      g_print ("  element is supposed to provide a clock but returned NULL\n");
    have_flags = TRUE;
  }
  if (!have_flags) {
    g_print ("  none\n");
  }

  g_print ("\nIndexing capabilities:\n");
  if (gst_element_is_indexable (element)) {
    g_print ("  element can do indexing\n");
  }
  else {
    g_print ("  none\n");
  }

  g_print ("\nPads:\n");
  if (element->numpads) {
    const GList *pads;
    pads = gst_element_get_pad_list (element);
    while (pads) {
      pad = GST_PAD (pads->data);
      pads = g_list_next (pads);
      realpad = GST_PAD_REALIZE (pad);

      if (gst_pad_get_direction (pad) == GST_PAD_SRC)
        g_print ("  SRC: '%s'", gst_pad_get_name (pad));
      else if (gst_pad_get_direction (pad) == GST_PAD_SINK)
        g_print ("  SINK: '%s'", gst_pad_get_name (pad));
      else
        g_print ("  UNKNOWN!!!: '%s'\n", gst_pad_get_name (pad));

      if (GST_IS_GHOST_PAD (pad))
        g_print (", ghost of real pad %s:%s\n", GST_DEBUG_PAD_NAME (realpad));
      else
        g_print ("\n");

      g_print ("    Implementation:\n");
      if (realpad->chainfunc)
        g_print ("      Has chainfunc(): %s\n",
	        GST_DEBUG_FUNCPTR_NAME (realpad->chainfunc));
      if (realpad->getfunc)
        g_print ("      Has getfunc(): %s\n",
	        GST_DEBUG_FUNCPTR_NAME (realpad->getfunc));
      if (realpad->formatsfunc != gst_pad_get_formats_default) {
        g_print ("      Supports seeking/conversion/query formats:\n");
	print_formats (gst_pad_get_formats (GST_PAD (realpad)));
      }
      if (realpad->convertfunc != gst_pad_convert_default)
        g_print ("      Has custom convertfunc(): %s\n",
	        GST_DEBUG_FUNCPTR_NAME (realpad->convertfunc));
      if (realpad->eventfunc != gst_pad_event_default)
        g_print ("      Has custom eventfunc(): %s\n",
	        GST_DEBUG_FUNCPTR_NAME (realpad->eventfunc));
      if (realpad->eventmaskfunc != gst_pad_get_event_masks_default) {
        g_print ("        Provides event masks:\n");
	print_event_masks (gst_pad_get_event_masks (GST_PAD (realpad)));
      }
      if (realpad->queryfunc != gst_pad_query_default)
        g_print ("      Has custom queryfunc(): %s\n",
	        GST_DEBUG_FUNCPTR_NAME (realpad->queryfunc));
      if (realpad->querytypefunc != gst_pad_get_query_types_default) {
        g_print ("        Provides query types:\n");
	print_query_types (gst_pad_get_query_types (GST_PAD (realpad)));
      }

      if (realpad->intlinkfunc != gst_pad_get_internal_links_default)
        g_print ("      Has custom intconnfunc(): %s\n",
	        GST_DEBUG_FUNCPTR_NAME(realpad->intlinkfunc));

      if (realpad->bufferpoolfunc)
        g_print ("      Has bufferpoolfunc(): %s\n",
	        GST_DEBUG_FUNCPTR_NAME(realpad->bufferpoolfunc));

      if (pad->padtemplate)
        g_print ("    Pad Template: '%s'\n",
	        pad->padtemplate->name_template);

      if (realpad->caps) {
        g_print ("    Capabilities:\n");
	print_caps (realpad->caps, "      ");
      }
    }
  } else
    g_print ("  none\n");

  print_element_properties (element);

  /* Dynamic Parameters block */
  {
    GstDParamManager* dpman;
    GParamSpec** specs;
    gint x;
    
    g_print ("\nDynamic Parameters:\n");
    if((dpman = gst_dpman_get_manager (element))) {
      specs = gst_dpman_list_dparam_specs (dpman);
      for (x = 0; specs[x] != NULL; x++) {
        g_print ("  %-20.20s: ", g_param_spec_get_name (specs[x]));

        switch (G_PARAM_SPEC_VALUE_TYPE (specs[x])) {
          case G_TYPE_INT64: 
            g_print ("64 Bit Integer (Default %" G_GINT64_FORMAT ", Range %" G_GINT64_FORMAT " -> %" G_GINT64_FORMAT ")", 
            ((GParamSpecInt64 *) specs[x])->default_value,
            ((GParamSpecInt64 *) specs[x])->minimum, 
            ((GParamSpecInt64 *) specs[x])->maximum);
            break;
          case G_TYPE_INT: 
            g_print ("Integer (Default %d, Range %d -> %d)", 
            ((GParamSpecInt *) specs[x])->default_value,
            ((GParamSpecInt *) specs[x])->minimum, 
            ((GParamSpecInt *) specs[x])->maximum);
            break;
          case G_TYPE_FLOAT: 
	    g_print ("Float. Default: %-8.8s %15.7g\n", "",
              ((GParamSpecFloat *) specs[x])->default_value);
	    g_print ("%-23.23s Range: %15.7g - %15.7g", "", 
              ((GParamSpecFloat *) specs[x])->minimum, 
              ((GParamSpecFloat *) specs[x])->maximum);
            break;
        default: g_print ("unknown %ld", G_PARAM_SPEC_VALUE_TYPE (specs[x]));
        }
        g_print ("\n");
      }
      g_free (specs);
    }
    else {
      g_print ("  none\n");
    }
  }

  /* Signals/Actions Block */  
  {
    guint *signals;
    guint nsignals;
    gint i, k;
    GSignalQuery *query;
    
    signals = g_signal_list_ids (G_OBJECT_TYPE (element), &nsignals);
    for (k = 0; k < 2; k++) {
      gint counted = 0;

      if (k == 0)
        g_print ("\nElement Signals:\n");
      else
        g_print ("\nElement Actions:\n");

      for (i = 0; i < nsignals; i++) {
        gint n_params;
        GType return_type;
        const GType *param_types;
        gint j;
      
        query = g_new0 (GSignalQuery,1);
        g_signal_query (signals[i], query);

	if ((k == 0 && !(query->signal_flags & G_SIGNAL_ACTION)) ||
	    (k == 1 &&  (query->signal_flags & G_SIGNAL_ACTION))) {
          n_params = query->n_params;
          return_type = query->return_type;
          param_types = query->param_types;

          g_print ("  \"%s\" :\t %s user_function (%s* object", 
	          query->signal_name, g_type_name (return_type),
		  g_type_name (G_OBJECT_TYPE (element)));

          for (j = 0; j < n_params; j++) {
            g_print (",\n    \t\t\t\t%s arg%d", g_type_name (param_types[j]), j);
          }
	  if (k == 0)
            g_print (",\n    \t\t\t\tgpointer user_data);\n");
	  else
            g_print (");\n");

	  counted++;
	}

        g_free (query);
      }
      if (counted == 0) g_print ("  none\n");
    }
  }
  

  /* for compound elements */
  if (GST_IS_BIN (element)) {
    g_print ("\nChildren:\n");
    children = (GList *) gst_bin_get_list (GST_BIN (element));
    if (!children) 
      g_print ("  none\n");
    else {
      while (children) {
        child = GST_ELEMENT (children->data);
        children = g_list_next (children);

        g_print ("  %s\n", GST_ELEMENT_NAME (child));
      }
    }
  }

  return 0;
}

static void 
print_element_list (void) 
{
  GList *plugins;

  plugins = gst_registry_pool_plugin_list();
  while (plugins) {
    GList *features;
    GstPlugin *plugin;
    
    plugin = (GstPlugin*)(plugins->data);
    plugins = g_list_next (plugins);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;

        factory = GST_ELEMENT_FACTORY (feature);
        g_print ("%s:  %s: %s\n", plugin->name, 
	        GST_PLUGIN_FEATURE_NAME (factory) ,factory->details->longname);
      }
      else if (GST_IS_AUTOPLUG_FACTORY (feature)) {
        GstAutoplugFactory *factory;

        factory = GST_AUTOPLUG_FACTORY (feature);
        g_print ("%s:  %s: %s\n", plugin->name, 
	        GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      }
      else if (GST_IS_INDEX_FACTORY (feature)) {
        GstIndexFactory *factory;

        factory = GST_INDEX_FACTORY (feature);
        g_print ("%s:  %s: %s\n", plugin->name, 
	        GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      }
      else if (GST_IS_TYPE_FACTORY (feature)) {
        GstTypeFactory *factory;

        factory = GST_TYPE_FACTORY (feature);
        g_print ("%s type:  %s: %s\n", plugin->name, 
	        factory->mime, factory->exts);

        if (factory->typefindfunc)
          g_print ("      Has typefind function: %s\n",
	          GST_DEBUG_FUNCPTR_NAME (factory->typefindfunc));
      }
      else if (GST_IS_SCHEDULER_FACTORY (feature)) {
        GstSchedulerFactory *factory;

        factory = GST_SCHEDULER_FACTORY (feature);
        g_print ("%s:  %s: %s\n", plugin->name, 
	        GST_PLUGIN_FEATURE_NAME (factory), factory->longdesc);
      }
      else if (GST_IS_URI_HANDLER (feature)) {
        GstURIHandler *handler;

        handler = GST_URI_HANDLER (feature);
        g_print ("%s:  %s: \"%s\" (%s) element \"%s\" property \"%s\"\n", plugin->name, 
	        GST_PLUGIN_FEATURE_NAME (handler), handler->uri, handler->longdesc,
		handler->element, handler->property);
      }
      else {
        g_print ("%s:  %s (%s)\n", plugin->name, 
	        GST_PLUGIN_FEATURE_NAME (feature), 
	  	g_type_name (G_OBJECT_TYPE (feature)));
      }

      features = g_list_next (features);
    }
  }
}

static void
print_plugin_info (GstPlugin *plugin)
{
  GList *features;
  gint num_features = 0;
  gint num_elements = 0;
  gint num_autoplug = 0;
  gint num_types = 0;
  gint num_schedulers = 0;
  gint num_indexes = 0;
  gint num_other = 0;
  
  g_print ("Plugin Details:\n");
  g_print ("  Name:\t\t%s\n",    plugin->name);
  g_print ("  Long Name:\t%s\n", plugin->longname);
  g_print ("  Filename:\t%s\n",  plugin->filename);
  g_print ("\n");

  features = gst_plugin_get_feature_list (plugin);

  while (features) {
    GstPluginFeature *feature;

    feature = GST_PLUGIN_FEATURE (features->data);

    if (GST_IS_ELEMENT_FACTORY (feature)) {
      GstElementFactory *factory;

      factory = GST_ELEMENT_FACTORY (feature);
      g_print ("  %s: %s\n", GST_OBJECT_NAME (factory),
	      factory->details->longname);
      num_elements++;
    }
    else if (GST_IS_AUTOPLUG_FACTORY (feature)) {
      GstAutoplugFactory *factory;

      factory = GST_AUTOPLUG_FACTORY (feature);
      g_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
      num_autoplug++;
    }
    else if (GST_IS_INDEX_FACTORY (feature)) {
      GstIndexFactory *factory;

      factory = GST_INDEX_FACTORY (feature);
      g_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
      num_indexes++;
    }
    else if (GST_IS_TYPE_FACTORY (feature)) {
      GstTypeFactory *factory;

      factory = GST_TYPE_FACTORY (feature);
      g_print ("  %s: %s\n", factory->mime, factory->exts);

      if (factory->typefindfunc)
        g_print ("      Has typefind function: %s\n", 
	        GST_DEBUG_FUNCPTR_NAME (factory->typefindfunc));
      num_types++;
    }
    else if (GST_IS_SCHEDULER_FACTORY (feature)) {
      GstSchedulerFactory *factory;

      factory = GST_SCHEDULER_FACTORY (feature);
      g_print ("  %s: %s\n", GST_OBJECT_NAME (factory), factory->longdesc);
      num_schedulers++;
    }
    else {
      g_print ("  %s (%s)\n", gst_object_get_name (GST_OBJECT (feature)), 
		             g_type_name (G_OBJECT_TYPE (feature)));
      num_other++;
    }
    num_features++;
    features = g_list_next (features);
  }
  g_print ("\n  %d features:\n", num_features);
  if (num_elements > 0)
    g_print ("  +-- %d elements\n", num_elements);
  if (num_autoplug > 0)
    g_print ("  +-- %d autopluggers\n", num_autoplug);
  if (num_types > 0)
    g_print ("  +-- %d types\n", num_types);
  if (num_schedulers > 0)
    g_print ("  +-- %d schedulers\n", num_schedulers);
  if (num_indexes > 0)
    g_print ("  +-- %d indexes\n", num_indexes);
  if (num_other > 0)
    g_print ("  +-- %d other objects\n", num_other);
  
  g_print ("\n");
}


int 
main (int argc, char *argv[]) 
{
  GstElementFactory *factory;
  GstPlugin *plugin;
  gchar *so;
  struct poptOption options[] = {
    {"gst-inspect-plugin",  'p',  POPT_ARG_STRING|POPT_ARGFLAG_STRIP,   NULL,   0,
	           "Show plugin details", NULL},
    {"gst-inspect-scheduler",  's',  POPT_ARG_STRING|POPT_ARGFLAG_STRIP,   NULL,   0,
	           "Show scheduler details", NULL},
    POPT_TABLEEND
  };

  gst_init_with_popt_table (&argc, &argv, options);
  gst_control_init (&argc, &argv);
  
  /* if no arguments, print out list of elements */
  if (argc == 1) {
    print_element_list();

  /* else we try to get a factory */
  } else {
    /* first check for help */
    if (strstr (argv[1], "-help")) {
      g_print ("Usage: %s\t\t\tList all registered elements\n",argv[0]);
      g_print ("       %s element-name\tShow element details\n",argv[0]);
      g_print ("       %s plugin-name[.so]\tShow information about plugin\n",
	      argv[0]);
      return 0;
    }

    /* only search for a factory if there's not a '.so' */
    if (! strstr (argv[1], ".so")) {
      factory = gst_element_factory_find (argv[1]);

      /* if there's a factory, print out the info */
      if (factory)
        return print_element_info (factory);
      else {
	 GstPluginFeature* feature;

	 /* FIXME implement other pretty print function for these */
	 feature = gst_registry_pool_find_feature (argv[1], GST_TYPE_SCHEDULER_FACTORY);
	 if (feature) {
           g_print ("%s: a scheduler\n", argv[1]);
	   return 0;
	 }
	 feature = gst_registry_pool_find_feature (argv[1], GST_TYPE_INDEX_FACTORY);
	 if (feature) {
           g_print ("%s: an index\n", argv[1]);
	   return 0;
	 }
	 feature = gst_registry_pool_find_feature (argv[1], GST_TYPE_AUTOPLUG_FACTORY);
	 if (feature) {
           g_print ("%s: an autoplugger\n", argv[1]);
	   return 0;
	 }
	 feature = gst_registry_pool_find_feature (argv[1], GST_TYPE_TYPE_FACTORY);
	 if (feature) {
           g_print ("%s: an type\n", argv[1]);
	   return 0;
	 }
	 feature = gst_registry_pool_find_feature (argv[1], GST_TYPE_URI_HANDLER);
	 if (feature) {
           g_print ("%s: an uri handler\n", argv[1]);
	   return 0;
	 }
      }
    } else {
      /* strip the .so */
      so = strstr(argv[1],".so");
      so[0] = '\0';
    }

    /* otherwise assume it's a plugin */
    plugin = gst_registry_pool_find_plugin (argv[1]);

    /* if there is such a plugin, print out info */

    if (plugin) {
      print_plugin_info (plugin);

    } else {
      g_print("no such element or plugin '%s'\n", argv[1]);
      return -1;
    }
  }

  return 0;
}
