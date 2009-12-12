/* GStreamer Language Tag Utility Functions
 * Copyright (C) 2009 Tim-Philipp Müller <tim centricular net>
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

/* mklangtables.c:
 * little program that reads iso_639.xml and outputs tables for us as fallback
 * for when iso-codes are not available or we fail to read the file for some
 * reason, and so we don't have to parse the xml file just to map codes.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <string.h>

#if !GLIB_CHECK_VERSION (2, 22, 0)
#define g_mapped_file_unref g_mapped_file_free
#endif

#define ISO_639_XML_PATH ISO_CODES_PREFIX "/share/xml/iso-codes/iso_639.xml"

typedef struct
{
  gchar code_1[3];              /* de     */
  gchar code_2t[4];             /* deu    */
  gchar code_2b[4];             /* ger    */
  const gchar *name;            /* German */
  guint name_offset;            /* offset into string table */
} IsoLang;

static GArray *languages = NULL;

static void
dump_languages (void)
{
  GString *names;
  const char *s;
  int i, num_escaped;

  g_assert (languages != NULL);

  names = g_string_new ("");

  g_print ("/* generated by " __FILE__ " iso-codes " ISO_CODES_VERSION " */\n");
  g_print ("\n");
  g_print ("#include <glib.h>\n");
  g_print ("\n");
  g_print ("#define ISO_639_FLAG_2T  (1 << 0)\n");
  g_print ("#define ISO_639_FLAG_2B  (1 << 1)\n");
  g_print ("\n");
  g_print ("/* *INDENT-OFF* */\n");
  g_print ("\n");
  g_print ("static const struct\n");
  g_print ("{\n");
  g_print ("  const gchar iso_639_1[3];\n");
  g_print ("  const gchar iso_639_2[4];\n");
  g_print ("  guint8 flags;\n");
  g_print ("  guint16 name_offset;\n");
  g_print ("} iso_639_codes[] = {\n");

  for (i = 0, num_escaped = 0; i < languages->len; ++i) {
    IsoLang *lang = &g_array_index (languages, IsoLang, i);

    /* For now just print those where there's both a ISO-639-1 and -2 code */
    if (lang->code_1[0] == '\0')
      continue;

    /* save current offset */
    lang->name_offset = names->len;

    /* adjust for fact that \000 is 4 chars now but will take up only 1 later */
    lang->name_offset -= num_escaped * 3;

    /* append one char at a time, making sure to escape UTF-8 characters */
    for (s = lang->name; s != NULL && *s != '\0'; ++s) {
      if (g_ascii_isprint (*s) && *s != '"' && *s != '\\') {
        g_string_append_c (names, *s);
      } else {
        g_string_append_printf (names, "\\%03o", (unsigned char) *s);
        ++num_escaped;
      }
    }
    g_string_append (names, "\\000");
    ++num_escaped;

    g_print ("    /* %s */\n", lang->name);
    if (strcmp (lang->code_2b, lang->code_2t) == 0) {
      g_print ("  { \"%s\", \"%s\", ISO_639_FLAG_2T | ISO_639_FLAG_2B, %u },\n",
          lang->code_1, lang->code_2t, lang->name_offset);
    } else {
      /* if 639-2T and 639-2B differ, put 639-2T first */
      g_print ("  { \"%s\", \"%s\", ISO_639_FLAG_2T, %u },\n",
          lang->code_1, lang->code_2t, lang->name_offset);
      g_print ("  { \"%s\", \"%s\", ISO_639_FLAG_2B, %u },\n",
          lang->code_1, lang->code_2b, lang->name_offset);
    }
  }

  g_print ("};\n");
  g_print ("\n");
  g_print ("const gchar iso_639_names[] =\n");
  s = names->str;
  while (s != NULL && *s != '\0') {
    gchar line[74], *lastesc;
    guint left;

    left = strlen (s);
    g_strlcpy (line, s, MIN (left, sizeof (line)));
    s += sizeof (line) - 1;
    /* avoid partial escaped codes at the end of a line */
    if ((lastesc = strrchr (line, '\\')) && strlen (lastesc) < 4) {
      s -= strlen (lastesc);
      *lastesc = '\0';
    }
    g_print ("  \"%s\"", line);
    if (left < 74)
      break;
    g_print ("\n");
  }
  g_print (";\n");
  g_print ("\n");
  g_print ("/* *INDENT-ON* */\n");

  g_string_free (names, TRUE);
}

static gboolean
copy_attribute (gchar * dest, guint dest_len, const gchar ** attr_names,
    const gchar ** attr_vals, const gchar * needle)
{
  while (attr_names != NULL && *attr_names != NULL) {
    if (strcmp (*attr_names, needle) == 0) {
      g_strlcpy (dest, *attr_vals, dest_len);
      return TRUE;
    }
    ++attr_names;
    ++attr_vals;
  }
  dest[0] = '\0';
  return FALSE;
}

static void
xml_start_element (GMarkupParseContext * ctx, const gchar * element_name,
    const gchar ** attr_names, const gchar ** attr_vals,
    gpointer user_data, GError ** error)
{
  gchar name[256];
  IsoLang lang;

  if (strcmp (element_name, "iso_639_entry") != 0)
    return;

  copy_attribute (lang.code_1, 3, attr_names, attr_vals, "iso_639_1_code");
  copy_attribute (lang.code_2t, 4, attr_names, attr_vals, "iso_639_2T_code");
  copy_attribute (lang.code_2b, 4, attr_names, attr_vals, "iso_639_2B_code");

  copy_attribute (name, sizeof (name), attr_names, attr_vals, "name");
  lang.name = g_intern_string (name);

  g_array_append_val (languages, lang);
}

static void
parse_iso_639_xml (const gchar * data, gsize len)
{
  GMarkupParser xml_parser = { xml_start_element, NULL, NULL, NULL, NULL };
  GMarkupParseContext *ctx;
  GError *err = NULL;

  g_return_if_fail (g_utf8_validate (data, len, NULL));

  ctx = g_markup_parse_context_new (&xml_parser, 0, NULL, NULL);
  if (!g_markup_parse_context_parse (ctx, data, len, &err))
    g_error ("Parsing failed: %s", err->message);

  g_markup_parse_context_free (ctx);
}

static gint
languages_sort_func (IsoLang * l1, IsoLang * l2)
{
  if (l1 == l2)
    return 0;

  if (l1->code_1[0] == '\0' && l2->code_1[0] != '\0')
    return -1;

  return strcmp (l1->code_1, l2->code_1);
}

int
main (int argc, char **argv)
{
  GMappedFile *f;
  gchar *xml_data;
  gsize xml_len;

  f = g_mapped_file_new (ISO_639_XML_PATH, FALSE, NULL);
  if (f != NULL) {
    xml_data = (gchar *) g_mapped_file_get_contents (f);
    xml_len = g_mapped_file_get_length (f);
  } else {
    GError *err = NULL;

    if (!g_file_get_contents (ISO_639_XML_PATH, &xml_data, &xml_len, &err))
      g_error ("Could not read %s: %s", ISO_639_XML_PATH, err->message);
  }

  languages = g_array_new (FALSE, TRUE, sizeof (IsoLang));

  parse_iso_639_xml (xml_data, xml_len);

  g_array_sort (languages, (GCompareFunc) languages_sort_func);

  dump_languages ();

  g_array_free (languages, TRUE);

  if (f != NULL)
    g_mapped_file_unref (f);
  else
    g_free (xml_data);

  return 0;
}
