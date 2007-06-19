/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>


/* we include this here to get the G_OS_* defines */
#include <glib.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#define EINPROGRESS WSAEINPROGRESS
#else
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include "rtspconnection.h"
#include "base64.h"

/* the select call is also performed on the control sockets, that way
 * we can send special commands to unlock or restart the select call */
#define CONTROL_RESTART        'R'      /* restart the select call */
#define CONTROL_STOP           'S'      /* stop the select call */
#define CONTROL_SOCKETS(conn)  conn->control_sock
#define WRITE_SOCKET(conn)     conn->control_sock[1]
#define READ_SOCKET(conn)      conn->control_sock[0]

#define SEND_COMMAND(conn, command)              \
G_STMT_START {                                  \
  unsigned char c; c = command;                 \
  write (WRITE_SOCKET(conn), &c, 1);             \
} G_STMT_END

#define READ_COMMAND(conn, command, res)         \
G_STMT_START {                                  \
  res = read(READ_SOCKET(conn), &command, 1);    \
} G_STMT_END

#ifdef G_OS_WIN32
#define IOCTL_SOCKET ioctlsocket
#define CLOSE_SOCKET(sock) closesocket(sock);
#else
#define IOCTL_SOCKET ioctl
#define CLOSE_SOCKET(sock) close(sock);
#endif

#ifdef G_OS_WIN32
static int
inet_aton (const char *c, struct in_addr *paddr)
{
  /* note that inet_addr is deprecated on unix because
   * inet_addr returns -1 (INADDR_NONE) for the valid 255.255.255.255
   * address. */
  paddr->s_addr = inet_addr (c);

  if (paddr->s_addr == INADDR_NONE)
    return 0;

  return 1;
}
#endif

RTSPResult
rtsp_connection_create (RTSPUrl * url, RTSPConnection ** conn)
{
  gint ret;
  RTSPConnection *newconn;

#ifdef G_OS_WIN32
  unsigned long flags;
#endif /* G_OS_WIN32 */

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

  newconn = g_new0 (RTSPConnection, 1);

#ifdef G_OS_WIN32
  /* This should work on UNIX too. PF_UNIX sockets replaced with pipe */
  /* pipe( CONTROL_SOCKETS(newconn) ) */
  if ((ret = pipe (CONTROL_SOCKETS (newconn))) < 0)
    goto no_socket_pair;

  ioctlsocket (READ_SOCKET (newconn), FIONBIO, &flags);
  ioctlsocket (WRITE_SOCKET (newconn), FIONBIO, &flags);
#else
  if ((ret =
          socketpair (PF_UNIX, SOCK_STREAM, 0, CONTROL_SOCKETS (newconn))) < 0)
    goto no_socket_pair;

  fcntl (READ_SOCKET (newconn), F_SETFL, O_NONBLOCK);
  fcntl (WRITE_SOCKET (newconn), F_SETFL, O_NONBLOCK);
#endif

  newconn->url = url;
  newconn->fd = -1;
  newconn->timer = g_timer_new ();

  newconn->auth_method = RTSP_AUTH_NONE;
  newconn->username = NULL;
  newconn->passwd = NULL;

  *conn = newconn;

  return RTSP_OK;

  /* ERRORS */
no_socket_pair:
  {
    g_free (newconn);
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_connect (RTSPConnection * conn, GTimeVal * timeout)
{
  gint fd;
  struct sockaddr_in sa_in;
  struct hostent *hostinfo;
  char **addrs;
  const gchar *ip;
  gchar ipbuf[INET_ADDRSTRLEN];
  struct in_addr addr;
  gint ret;
  guint16 port;
  RTSPUrl *url;
  fd_set writefds;
  fd_set readfds;
  struct timeval tv, *tvp;
  gint max_fd, retval;

#ifdef G_OS_WIN32
  unsigned long flags;
#endif /* G_OS_WIN32 */

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (conn->url != NULL, RTSP_EINVAL);
  g_return_val_if_fail (conn->fd < 0, RTSP_EINVAL);

  url = conn->url;

  /* first check if it already is an IP address */
  if (inet_aton (url->host, &addr)) {
    ip = url->host;
  } else {
    hostinfo = gethostbyname (url->host);
    if (!hostinfo)
      goto not_resolved;        /* h_errno set */

    if (hostinfo->h_addrtype != AF_INET)
      goto not_ip;              /* host not an IP host */

    addrs = hostinfo->h_addr_list;
    ip = inet_ntop (AF_INET, (struct in_addr *) addrs[0], ipbuf,
        sizeof (ipbuf));
  }

  /* get the port from the url */
  rtsp_url_get_port (url, &port);

  memset (&sa_in, 0, sizeof (sa_in));
  sa_in.sin_family = AF_INET;   /* network socket */
  sa_in.sin_port = htons (port);        /* on port */
  sa_in.sin_addr.s_addr = inet_addr (ip);       /* on host ip */

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    goto sys_error;

  /* set to non-blocking mode so that we can cancel the connect */
#ifndef G_OS_WIN32
  fcntl (fd, F_SETFL, O_NONBLOCK);
#else
  ioctlsocket (fd, FIONBIO, &flags);
#endif /* G_OS_WIN32 */

  /* we are going to connect ASYNC now */
  ret = connect (fd, (struct sockaddr *) &sa_in, sizeof (sa_in));
  if (ret == 0)
    goto done;
  if (errno != EINPROGRESS)
    goto sys_error;

  /* wait for connect to complete up to the specified timeout or until we got
   * interrupted. */
  FD_ZERO (&writefds);
  FD_SET (fd, &writefds);
  FD_ZERO (&readfds);
  FD_SET (READ_SOCKET (conn), &readfds);

  if (timeout->tv_sec != 0 || timeout->tv_usec != 0) {
    tv.tv_sec = timeout->tv_sec;
    tv.tv_usec = timeout->tv_usec;
    tvp = &tv;
  } else {
    tvp = NULL;
  }

  max_fd = MAX (fd, READ_SOCKET (conn));

  do {
    retval = select (max_fd + 1, &readfds, &writefds, NULL, tvp);
  } while ((retval == -1 && errno == EINTR));

  if (retval == 0)
    goto timeout;
  else if (retval == -1)
    goto sys_error;

done:
  conn->fd = fd;
  conn->ip = g_strdup (ip);

  return RTSP_OK;

sys_error:
  {
    if (fd != -1)
      CLOSE_SOCKET (fd);
    return RTSP_ESYS;
  }
not_resolved:
  {
    return RTSP_ENET;
  }
not_ip:
  {
    return RTSP_ENOTIP;
  }
timeout:
  {
    return RTSP_ETIMEOUT;
  }
}

static void
add_auth_header (RTSPConnection * conn, RTSPMessage * message)
{
  switch (conn->auth_method) {
    case RTSP_AUTH_BASIC:{
      gchar *user_pass =
          g_strdup_printf ("%s:%s", conn->username, conn->passwd);
      gchar *user_pass64 = util_base64_encode (user_pass, strlen (user_pass));
      gchar *auth_string = g_strdup_printf ("Basic %s", user_pass64);

      rtsp_message_add_header (message, RTSP_HDR_AUTHORIZATION, auth_string);

      g_free (user_pass);
      g_free (user_pass64);
      g_free (auth_string);
      break;
    }
    default:
      /* Nothing to do */
      break;
  }
}

static void
add_date_header (RTSPMessage * message)
{
  GTimeVal tv;
  gchar date_string[100];

  g_get_current_time (&tv);
  strftime (date_string, sizeof (date_string), "%a, %d %b %Y %H:%M:%S GMT",
      gmtime (&tv.tv_sec));

  rtsp_message_add_header (message, RTSP_HDR_DATE, date_string);
}

RTSPResult
rtsp_connection_write (RTSPConnection * conn, const guint8 * data, guint size,
    GTimeVal * timeout)
{
  guint towrite;
  fd_set writefds;
  fd_set readfds;
  int max_fd;
  gint retval;
  struct timeval tv, *tvp;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (data != NULL || size == 0, RTSP_EINVAL);

  FD_ZERO (&writefds);
  FD_SET (conn->fd, &writefds);
  FD_ZERO (&readfds);
  FD_SET (READ_SOCKET (conn), &readfds);

  max_fd = MAX (conn->fd, READ_SOCKET (conn));

  if (timeout) {
    tv.tv_sec = timeout->tv_sec;
    tv.tv_usec = timeout->tv_usec;
    tvp = &tv;
  } else {
    tvp = NULL;
  }

  towrite = size;

  while (towrite > 0) {
    gint written;

    do {
      retval = select (max_fd + 1, &readfds, &writefds, NULL, tvp);
    } while ((retval == -1 && errno == EINTR));

    if (retval == 0)
      goto timeout;

    if (retval == -1)
      goto select_error;

    if (FD_ISSET (READ_SOCKET (conn), &readfds)) {
      /* read all stop commands */
      while (TRUE) {
        gchar command;
        int res;

        READ_COMMAND (conn, command, res);
        if (res <= 0) {
          /* no more commands */
          break;
        }
      }
      goto stopped;
    }

    /* now we can write */
    written = write (conn->fd, data, towrite);
    if (written < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto write_error;
    } else {
      towrite -= written;
      data += written;
    }
  }
  return RTSP_OK;

  /* ERRORS */
timeout:
  {
    return RTSP_ETIMEOUT;
  }
select_error:
  {
    return RTSP_ESYS;
  }
stopped:
  {
    return RTSP_EINTR;
  }
write_error:
  {
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_send (RTSPConnection * conn, RTSPMessage * message,
    GTimeVal * timeout)
{
  GString *str;
  RTSPResult res;

#ifdef G_OS_WIN32
  WSADATA w;
  int error;
#endif

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (message != NULL, RTSP_EINVAL);

#ifdef G_OS_WIN32
  error = WSAStartup (0x0202, &w);

  if (error)
    goto startup_error;

  if (w.wVersion != 0x0202)
    goto version_error;
#endif

  str = g_string_new ("");

  switch (message->type) {
    case RTSP_MESSAGE_REQUEST:
      /* create request string, add CSeq */
      g_string_append_printf (str, "%s %s RTSP/1.0\r\n"
          "CSeq: %d\r\n",
          rtsp_method_as_text (message->type_data.request.method),
          message->type_data.request.uri, conn->cseq++);
      /* add session id if we have one */
      if (conn->session_id[0] != '\0') {
        rtsp_message_add_header (message, RTSP_HDR_SESSION, conn->session_id);
      }
      /* add any authentication headers */
      add_auth_header (conn, message);
      break;
    case RTSP_MESSAGE_RESPONSE:
      /* create response string */
      g_string_append_printf (str, "RTSP/1.0 %d %s\r\n",
          message->type_data.response.code, message->type_data.response.reason);
      break;
    case RTSP_MESSAGE_DATA:
    {
      guint8 data_header[4];

      /* prepare data header */
      data_header[0] = '$';
      data_header[1] = message->type_data.data.channel;
      data_header[2] = (message->body_size >> 8) & 0xff;
      data_header[3] = message->body_size & 0xff;

      /* create string with header and data */
      str = g_string_append_len (str, (gchar *) data_header, 4);
      str =
          g_string_append_len (str, (gchar *) message->body,
          message->body_size);
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  /* append headers and body */
  if (message->type != RTSP_MESSAGE_DATA) {
    /* add date header */
    add_date_header (message);

    /* append headers */
    rtsp_message_append_headers (message, str);

    /* append Content-Length and body if needed */
    if (message->body != NULL && message->body_size > 0) {
      gchar *len;

      len = g_strdup_printf ("%d", message->body_size);
      g_string_append_printf (str, "%s: %s\r\n",
          rtsp_header_as_text (RTSP_HDR_CONTENT_LENGTH), len);
      g_free (len);
      /* header ends here */
      g_string_append (str, "\r\n");
      str =
          g_string_append_len (str, (gchar *) message->body,
          message->body_size);
    } else {
      /* just end headers */
      g_string_append (str, "\r\n");
    }
  }

  /* write request */
  res = rtsp_connection_write (conn, (guint8 *) str->str, str->len, timeout);

  g_string_free (str, TRUE);

  return res;

#ifdef G_OS_WIN32
startup_error:
  {
    g_warning ("Error %d on WSAStartup", error);
    return RTSP_EWSASTART;
  }
version_error:
  {
    g_warning ("Windows sockets are not version 0x202 (current 0x%x)",
        w.wVersion);
    WSACleanup ();
    return RTSP_EWSAVERSION;
  }
#endif
}

static RTSPResult
read_line (gint fd, gchar * buffer, guint size)
{
  guint idx;
  gchar c;
  gint r;

  idx = 0;
  while (TRUE) {
    r = read (fd, &c, 1);
    if (r == 0) {
      goto eof;
    } else if (r < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto read_error;
    } else {
      if (c == '\n')            /* end on \n */
        break;
      if (c == '\r')            /* ignore \r */
        continue;

      if (idx < size - 1)
        buffer[idx++] = c;
    }
  }
  buffer[idx] = '\0';

  return RTSP_OK;

eof:
  {
    return RTSP_EEOF;
  }
read_error:
  {
    return RTSP_ESYS;
  }
}

static void
read_string (gchar * dest, gint size, gchar ** src)
{
  gint idx;

  idx = 0;
  /* skip spaces */
  while (g_ascii_isspace (**src))
    (*src)++;

  while (!g_ascii_isspace (**src) && **src != '\0') {
    if (idx < size - 1)
      dest[idx++] = **src;
    (*src)++;
  }
  if (size > 0)
    dest[idx] = '\0';
}

static void
read_key (gchar * dest, gint size, gchar ** src)
{
  gint idx;

  idx = 0;
  while (**src != ':' && **src != '\0') {
    if (idx < size - 1)
      dest[idx++] = **src;
    (*src)++;
  }
  if (size > 0)
    dest[idx] = '\0';
}

static RTSPResult
parse_response_status (gchar * buffer, RTSPMessage * msg)
{
  RTSPResult res;
  gchar versionstr[20];
  gchar codestr[4];
  gint code;
  gchar *bptr;

  bptr = buffer;

  read_string (versionstr, sizeof (versionstr), &bptr);
  read_string (codestr, sizeof (codestr), &bptr);
  code = atoi (codestr);

  while (g_ascii_isspace (*bptr))
    bptr++;

  if (strcmp (versionstr, "RTSP/1.0") == 0)
    RTSP_CHECK (rtsp_message_init_response (msg, code, bptr, NULL),
        parse_error);
  else if (strncmp (versionstr, "RTSP/", 5) == 0) {
    RTSP_CHECK (rtsp_message_init_response (msg, code, bptr, NULL),
        parse_error);
    msg->type_data.response.version = RTSP_VERSION_INVALID;
  } else
    goto parse_error;

  return RTSP_OK;

parse_error:
  {
    return RTSP_EPARSE;
  }
}

static RTSPResult
parse_request_line (gchar * buffer, RTSPMessage * msg)
{
  RTSPResult res = RTSP_OK;
  gchar versionstr[20];
  gchar methodstr[20];
  gchar urlstr[4096];
  gchar *bptr;
  RTSPMethod method;

  bptr = buffer;

  read_string (methodstr, sizeof (methodstr), &bptr);
  method = rtsp_find_method (methodstr);

  read_string (urlstr, sizeof (urlstr), &bptr);
  if (*urlstr == '\0')
    res = RTSP_EPARSE;

  read_string (versionstr, sizeof (versionstr), &bptr);

  if (*bptr != '\0')
    res = RTSP_EPARSE;

  if (strcmp (versionstr, "RTSP/1.0") == 0) {
    if (rtsp_message_init_request (msg, method, urlstr) != RTSP_OK)
      res = RTSP_EPARSE;
  } else if (strncmp (versionstr, "RTSP/", 5) == 0) {
    if (rtsp_message_init_request (msg, method, urlstr) != RTSP_OK)
      res = RTSP_EPARSE;
    msg->type_data.request.version = RTSP_VERSION_INVALID;
  } else {
    rtsp_message_init_request (msg, method, urlstr);
    msg->type_data.request.version = RTSP_VERSION_INVALID;
    res = RTSP_EPARSE;
  }

  return res;
}

/* parsing lines means reading a Key: Value pair */
static RTSPResult
parse_line (gchar * buffer, RTSPMessage * msg)
{
  gchar key[32];
  gchar *bptr;
  RTSPHeaderField field;

  bptr = buffer;

  /* read key */
  read_key (key, sizeof (key), &bptr);
  if (*bptr != ':')
    goto no_column;

  bptr++;

  field = rtsp_find_header_field (key);
  if (field != RTSP_HDR_INVALID) {
    while (g_ascii_isspace (*bptr))
      bptr++;
    rtsp_message_add_header (msg, field, bptr);
  }

  return RTSP_OK;

no_column:
  {
    return RTSP_EPARSE;
  }
}

RTSPResult
rtsp_connection_read (RTSPConnection * conn, guint8 * data, guint size,
    GTimeVal * timeout)
{
  fd_set readfds;
  guint toread;
  gint retval;
  struct timeval tv_timeout, *ptv_timeout = NULL;

#ifndef G_OS_WIN32
  gint avail;
#else
  gulong avail;
#endif

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (data != NULL, RTSP_EINVAL);

  if (size == 0)
    return RTSP_OK;

  toread = size;

  /* if the call fails, just go in the select.. it should not fail. Else if
   * there is enough data to read, skip the select call al together.*/
  if (IOCTL_SOCKET (conn->fd, FIONREAD, &avail) < 0)
    avail = 0;
  else if (avail >= toread)
    goto do_read;

  /* configure timeout if any */
  if (timeout != NULL) {
    tv_timeout.tv_sec = timeout->tv_sec;
    tv_timeout.tv_usec = timeout->tv_usec;
    ptv_timeout = &tv_timeout;
  }

  FD_ZERO (&readfds);
  FD_SET (conn->fd, &readfds);
  FD_SET (READ_SOCKET (conn), &readfds);

  while (toread > 0) {
    gint bytes;

    do {
      retval = select (FD_SETSIZE, &readfds, NULL, NULL, ptv_timeout);
    } while ((retval == -1 && errno == EINTR));

    if (retval == -1)
      goto select_error;

    /* check for timeout */
    if (retval == 0)
      goto select_timeout;

    if (FD_ISSET (READ_SOCKET (conn), &readfds)) {
      /* read all stop commands */
      while (TRUE) {
        gchar command;
        int res;

        READ_COMMAND (conn, command, res);
        if (res <= 0) {
          /* no more commands */
          break;
        }
      }
      goto stopped;
    }

  do_read:
    /* if we get here there is activity on the real fd since the select
     * completed and the control socket was not readable. */
    bytes = read (conn->fd, data, toread);

    if (bytes == 0) {
      goto eof;
    } else if (bytes < 0) {
      if (errno != EAGAIN && errno != EINTR)
        goto read_error;
    } else {
      toread -= bytes;
      data += bytes;
    }
  }
  return RTSP_OK;

  /* ERRORS */
select_error:
  {
    return RTSP_ESYS;
  }
select_timeout:
  {
    return RTSP_ETIMEOUT;
  }
stopped:
  {
    return RTSP_EINTR;
  }
eof:
  {
    return RTSP_EEOF;
  }
read_error:
  {
    return RTSP_ESYS;
  }
}

static RTSPResult
read_body (RTSPConnection * conn, glong content_length, RTSPMessage * msg,
    GTimeVal * timeout)
{
  guint8 *body;
  RTSPResult res;

  if (content_length <= 0) {
    body = NULL;
    content_length = 0;
    goto done;
  }

  body = g_malloc (content_length + 1);
  body[content_length] = '\0';

  RTSP_CHECK (rtsp_connection_read (conn, body, content_length, timeout),
      read_error);

  content_length += 1;

done:
  rtsp_message_take_body (msg, (guint8 *) body, content_length);

  return RTSP_OK;

  /* ERRORS */
read_error:
  {
    g_free (body);
    return res;
  }
}

RTSPResult
rtsp_connection_receive (RTSPConnection * conn, RTSPMessage * msg,
    GTimeVal * timeout)
{
  gchar buffer[4096];
  gint line;
  glong content_length;
  RTSPResult res;
  gboolean need_body;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (msg != NULL, RTSP_EINVAL);

  line = 0;

  need_body = TRUE;

  res = RTSP_OK;
  /* parse first line and headers */
  while (res == RTSP_OK) {
    guint8 c;

    /* read first character, this identifies data messages */
    RTSP_CHECK (rtsp_connection_read (conn, &c, 1, timeout), read_error);

    /* check for data packet, first character is $ */
    if (c == '$') {
      guint16 size;

      /* data packets are $<1 byte channel><2 bytes length,BE><data bytes> */

      /* read channel, which is the next char */
      RTSP_CHECK (rtsp_connection_read (conn, &c, 1, timeout), read_error);

      /* now we create a data message */
      rtsp_message_init_data (msg, c);

      /* next two bytes are the length of the data */
      RTSP_CHECK (rtsp_connection_read (conn, (guint8 *) & size, 2, timeout),
          read_error);

      size = GUINT16_FROM_BE (size);

      /* and read the body */
      res = read_body (conn, size, msg, timeout);
      need_body = FALSE;
      break;
    } else {
      gint offset = 0;

      /* we have a regular response */
      if (c != '\r') {
        buffer[0] = c;
        offset = 1;
      }
      /* should not happen */
      if (c == '\n')
        break;

      /* read lines */
      RTSP_CHECK (read_line (conn->fd, buffer + offset,
              sizeof (buffer) - offset), read_error);

      if (buffer[0] == '\0')
        break;

      if (line == 0) {
        /* first line, check for response status */
        if (g_str_has_prefix (buffer, "RTSP")) {
          res = parse_response_status (buffer, msg);
        } else {
          res = parse_request_line (buffer, msg);
        }
      } else {
        /* else just parse the line */
        parse_line (buffer, msg);
      }
    }
    line++;
  }

  /* read the rest of the body if needed */
  if (need_body) {
    gchar *session_id;
    gchar *hdrval;

    /* see if there is a Content-Length header */
    if (rtsp_message_get_header (msg, RTSP_HDR_CONTENT_LENGTH,
            &hdrval, 0) == RTSP_OK) {
      /* there is, read the body */
      content_length = atol (hdrval);
      RTSP_CHECK (read_body (conn, content_length, msg, timeout), read_error);
    }

    /* save session id in the connection for further use */
    if (rtsp_message_get_header (msg, RTSP_HDR_SESSION,
            &session_id, 0) == RTSP_OK) {
      gint maxlen, i;

      /* default session timeout */
      conn->timeout = 60;

      maxlen = sizeof (conn->session_id) - 1;
      /* the sessionid can have attributes marked with ;
       * Make sure we strip them */
      for (i = 0; session_id[i] != '\0'; i++) {
        if (session_id[i] == ';') {
          maxlen = i;
          /* parse timeout */
          do {
            i++;
          } while (g_ascii_isspace (session_id[i]));
          if (g_str_has_prefix (&session_id[i], "timeout=")) {
            gint to;

            /* if we parsed something valid, configure */
            if ((to = atoi (&session_id[i + 9])) > 0)
              conn->timeout = to;
          }
          break;
        }
      }

      /* make sure to not overflow */
      strncpy (conn->session_id, session_id, maxlen);
      conn->session_id[maxlen] = '\0';
    }
  }
  return res;

read_error:
  {
    return res;
  }
}

RTSPResult
rtsp_connection_close (RTSPConnection * conn)
{
  gint res;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

  g_free (conn->ip);
  conn->ip = NULL;

  if (conn->fd != -1) {
    res = CLOSE_SOCKET (conn->fd);
#ifdef G_OS_WIN32
    WSACleanup ();
#endif
    conn->fd = -1;
    if (res != 0)
      goto sys_error;
  }

  return RTSP_OK;

sys_error:
  {
    return RTSP_ESYS;
  }
}

RTSPResult
rtsp_connection_free (RTSPConnection * conn)
{
  RTSPResult res;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

#ifdef G_OS_WIN32
  WSACleanup ();
#endif
  res = rtsp_connection_close (conn);
  g_timer_destroy (conn->timer);
  g_free (conn->username);
  g_free (conn->passwd);
  g_free (conn);

  return res;
}

RTSPResult
rtsp_connection_next_timeout (RTSPConnection * conn, GTimeVal * timeout)
{
  gdouble elapsed;
  glong sec;
  gulong usec;

  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);
  g_return_val_if_fail (timeout != NULL, RTSP_EINVAL);

  elapsed = g_timer_elapsed (conn->timer, &usec);
  if (elapsed >= conn->timeout) {
    sec = 0;
    usec = 0;
  } else {
    sec = conn->timeout - elapsed;
  }

  timeout->tv_sec = sec;
  timeout->tv_usec = usec;

  return RTSP_OK;
}

RTSPResult
rtsp_connection_reset_timeout (RTSPConnection * conn)
{
  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

  g_timer_start (conn->timer);

  return RTSP_OK;
}

RTSPResult
rtsp_connection_flush (RTSPConnection * conn, gboolean flush)
{
  g_return_val_if_fail (conn != NULL, RTSP_EINVAL);

  if (flush) {
    SEND_COMMAND (conn, CONTROL_STOP);
  } else {
    while (TRUE) {
      gchar command;
      int res;

      READ_COMMAND (conn, command, res);
      if (res <= 0) {
        /* no more commands */
        break;
      }
    }
  }
  return RTSP_OK;
}

RTSPResult
rtsp_connection_set_auth (RTSPConnection * conn, RTSPAuthMethod method,
    gchar * user, gchar * pass)
{
  /* Digest isn't implemented yet */
  if (method == RTSP_AUTH_DIGEST)
    return RTSP_ENOTIMPL;

  /* Make sure the username and passwd are being set for authentication */
  if (method == RTSP_AUTH_NONE && (user == NULL || pass == NULL))
    return RTSP_EINVAL;

  /* ":" chars are not allowed in usernames for basic auth */
  if (method == RTSP_AUTH_BASIC && g_strrstr (user, ":") != NULL)
    return RTSP_EINVAL;

  g_free (conn->username);
  g_free (conn->passwd);

  conn->auth_method = method;
  conn->username = g_strdup (user);
  conn->passwd = g_strdup (pass);

  return RTSP_OK;
}
