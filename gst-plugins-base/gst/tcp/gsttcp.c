/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gsttcp.c: TCP functions
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <glib.h>
#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>
#include <gst/dataprotocol/dataprotocol.h>

GST_DEBUG_CATEGORY_EXTERN (tcp_debug);
#define GST_CAT_DEFAULT tcp_debug

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/* resolve host to IP address, throwing errors if it fails */
/* host can already be an IP address */
/* returns a newly allocated gchar * with the dotted ip address,
   or NULL, in which case it already fired an error. */
gchar *
gst_tcp_host_to_ip (GstElement * element, const gchar * host)
{
  struct hostent *hostinfo;
  char **addrs;
  gchar *ip;
  struct in_addr addr;

  GST_DEBUG_OBJECT (element, "resolving host %s", host);
  /* first check if it already is an IP address */
  if (inet_aton (host, &addr)) {
    ip = g_strdup (host);
    goto beach;
  }

  /* FIXME: could do a localhost check here */

  /* perform a name lookup */
  hostinfo = gethostbyname (host);
  if (!hostinfo) {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND, (NULL),
        ("Could not find IP address for host \"%s\".", host));
    return NULL;
  }

  if (hostinfo->h_addrtype != AF_INET) {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND, (NULL),
        ("host \"%s\" is not an IP host", host));
    return NULL;
  }

  addrs = hostinfo->h_addr_list;
  /* There could be more than one IP address, but we just return the first */
  ip = g_strdup (inet_ntoa (*(struct in_addr *) *addrs));

beach:
  GST_DEBUG_OBJECT (element, "resolved to IP %s", ip);
  return ip;
}

/* write buffer to given socket incrementally.
 * Returns number of bytes written.
 */
gint
gst_tcp_socket_write (int socket, const void *buf, size_t count)
{
  size_t bytes_written = 0;

  while (bytes_written < count) {
    ssize_t wrote = send (socket, buf + bytes_written,
        count - bytes_written, MSG_NOSIGNAL);

    if (wrote <= 0) {
      return bytes_written;
    }
    bytes_written += wrote;
  }

  if (bytes_written < 0)
    GST_WARNING ("error while writing");
  else
    GST_LOG ("wrote %d bytes succesfully", bytes_written);
  return bytes_written;
}

/* read number of bytes from a socket into a given buffer incrementally.
 * Returns number of bytes read with same semantics as read(2):
 * < 0: error, see errno
 * = 0: EOF
 * > 0: bytes read
 */
gint
gst_tcp_socket_read (int socket, void *buf, size_t count)
{
  size_t bytes_read = 0;

  while (bytes_read < count) {
    ssize_t ret = read (socket, buf + bytes_read,
        count - bytes_read);

    if (ret < 0)
      GST_WARNING ("error while reading: %s", g_strerror (errno));
    if (ret <= 0)
      return bytes_read;
    bytes_read += ret;
  }

  if (bytes_read < 0)
    GST_WARNING ("error while reading: %s", g_strerror (errno));
  else
    GST_LOG ("read %d bytes succesfully", bytes_read);
  return bytes_read;
}

/* close the socket and reset the fd.  Used to clean up after errors. */
void
gst_tcp_socket_close (int *socket)
{
  close (*socket);
  *socket = -1;
}

/* read the gdp buffer header from the given socket
 * returns:
 * - a GstData representing a GstBuffer in which data should be read
 * - a GstData representing a GstEvent
 * - NULL, indicating a connection close or an error, to be handled with
 *         EOS
 */
GstData *
gst_tcp_gdp_read_header (GstElement * this, int socket)
{
  size_t header_length = GST_DP_HEADER_LENGTH;
  size_t readsize;
  guint8 *header = NULL;
  ssize_t ret;
  GstBuffer *buffer;

  header = g_malloc (header_length);
  readsize = header_length;

  GST_LOG_OBJECT (this, "Reading %d bytes for buffer packet header", readsize);
  ret = gst_tcp_socket_read (socket, header, readsize);
  /* if we read 0 bytes, and we're blocking, we hit eos */
  if (ret == 0) {
    GST_DEBUG ("blocking read returns 0, returning NULL");
    g_free (header);
    return NULL;
  }
  if (ret < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    g_free (header);
    return NULL;
  }
  if (ret != readsize) {
    g_warning ("Wanted %d bytes, got %d bytes", (int) readsize, (int) ret);
  }
  g_assert (ret == readsize);

  if (!gst_dp_validate_header (header_length, header)) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP buffer packet header does not validate"));
    g_free (header);
    return NULL;
  }
  GST_LOG_OBJECT (this, "validated buffer packet header");

  buffer = gst_dp_buffer_from_header (header_length, header);
  g_free (header);

  GST_LOG_OBJECT (this, "created new buffer %p from packet header", buffer);
  return GST_DATA (buffer);
}

/* read the GDP caps packet from the given socket
 * returns the caps, or NULL in case of an error */
GstCaps *
gst_tcp_gdp_read_caps (GstElement * this, int socket)
{
  size_t header_length = GST_DP_HEADER_LENGTH;
  size_t readsize;
  guint8 *header = NULL;
  guint8 *payload = NULL;
  ssize_t ret;
  GstCaps *caps;
  gchar *string;

  header = g_malloc (header_length);
  readsize = header_length;

  GST_LOG_OBJECT (this, "Reading %d bytes for caps packet header", readsize);
  ret = gst_tcp_socket_read (socket, header, readsize);
  if (ret < 0) {
    g_free (header);
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    return NULL;
  }
  if (ret == 0) {
    GST_WARNING_OBJECT (this, "read returned EOF");
    return NULL;
  }
  if (ret != readsize) {
    GST_WARNING_OBJECT (this, "Tried to read %d bytes but only read %d bytes",
        readsize, ret);
    return NULL;
  }

  if (!gst_dp_validate_header (header_length, header)) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP caps packet header does not validate"));
    g_free (header);
    return NULL;
  }

  readsize = gst_dp_header_payload_length (header);
  payload = g_malloc (readsize);
  GST_LOG_OBJECT (this, "Reading %d bytes for caps packet payload", readsize);
  ret = gst_tcp_socket_read (socket, payload, readsize);

  if (ret < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
    g_free (header);
    g_free (payload);
    return NULL;
  }
  if (gst_dp_header_payload_type (header) != GST_DP_PAYLOAD_CAPS) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("Header read doesn't describe CAPS payload"));
    g_free (header);
    g_free (payload);
    return NULL;
  }
  g_assert (ret == readsize);

  if (!gst_dp_validate_payload (readsize, header, payload)) {
    GST_ELEMENT_ERROR (this, RESOURCE, READ, (NULL),
        ("GDP caps packet payload does not validate"));
    g_free (header);
    g_free (payload);
    return NULL;
  }

  caps = gst_dp_caps_from_packet (header_length, header, payload);
  string = gst_caps_to_string (caps);
  GST_LOG_OBJECT (this, "retrieved GDP caps from packet payload: %s", string);
  g_free (string);

  g_free (header);
  g_free (payload);

  return caps;
}

/* write a GDP header to the socket.  Return false if fails. */
gboolean
gst_tcp_gdp_write_header (GstElement * this, int socket, GstBuffer * buffer,
    gboolean fatal, const gchar * host, int port)
{
  guint length;
  guint8 *header;
  size_t wrote;

  if (!gst_dp_header_from_buffer (buffer, 0, &length, &header)) {
    if (fatal)
      GST_ELEMENT_ERROR (this, CORE, TOO_LAZY, (NULL),
          ("Could not create GDP header from buffer"));
    return FALSE;
  }

  GST_LOG_OBJECT (this, "writing %d bytes for GDP buffer header", length);
  wrote = gst_tcp_socket_write (socket, header, length);
  g_free (header);
  if (wrote != length) {
    if (fatal)
      GST_ELEMENT_ERROR (this, RESOURCE, WRITE,
          (_("Error while sending data to \"%s:%d\"."), host, port),
          ("Only %d of %d bytes written: %s",
              wrote, GST_BUFFER_SIZE (buffer), g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}

/* write GDP header and payload to the given socket for the given caps.
 * Return false if fails. */
gboolean
gst_tcp_gdp_write_caps (GstElement * this, int socket, const GstCaps * caps,
    gboolean fatal, const char *host, int port)
{
  guint length;
  guint8 *header;
  guint8 *payload;
  size_t wrote;

  if (!gst_dp_packet_from_caps (caps, 0, &length, &header, &payload)) {
    if (fatal)
      GST_ELEMENT_ERROR (this, CORE, TOO_LAZY, (NULL),
          ("Could not create GDP packet from caps"));
    return FALSE;
  }
  GST_LOG_OBJECT (this, "writing %d bytes for GDP caps header", length);
  wrote = gst_tcp_socket_write (socket, header, length);
  if (wrote != length) {
    g_free (header);
    g_free (payload);
    if (fatal)
      GST_ELEMENT_ERROR (this, RESOURCE, WRITE,
          (_("Error while sending gdp header data to \"%s:%d\"."), host, port),
          ("Only %d of %d bytes written: %s",
              wrote, length, g_strerror (errno)));
    return FALSE;
  }

  length = gst_dp_header_payload_length (header);
  g_free (header);
  GST_LOG_OBJECT (this, "writing %d bytes for GDP caps payload", length);
  wrote = gst_tcp_socket_write (socket, payload, length);
  g_free (payload);
  if (wrote != length) {
    if (fatal)
      GST_ELEMENT_ERROR (this, RESOURCE, WRITE,
          (_("Error while sending gdp payload data to \"%s:%d\"."), host, port),
          ("Only %d of %d bytes written: %s",
              wrote, length, g_strerror (errno)));
    return FALSE;
  }
  return TRUE;
}
