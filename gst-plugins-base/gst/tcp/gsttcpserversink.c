/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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
#include <gst/gst-i18n-plugin.h>

#include <sys/ioctl.h>

#ifdef HAVE_FIONREAD_IN_SYS_FILIO
#include <sys/filio.h>
#endif

#include <glib.h>

#include "gsttcpserversink.h"
#include "gsttcp-marshal.h"

#define TCP_DEFAULT_HOST	"127.0.0.1"
#define TCP_DEFAULT_PORT	4953
#define TCP_BACKLOG		5

/* elementfactory information */
static GstElementDetails gst_tcpserversink_details =
GST_ELEMENT_DETAILS ("TCP Server sink",
    "Sink/Network",
    "Send data as a server over the network via TCP",
    "Thomas Vander Stichele <thomas at apestaart dot org>");

GST_DEBUG_CATEGORY (tcpserversink_debug);
#define GST_CAT_DEFAULT (tcpserversink_debug)

/* TCPServerSink signals and args */
enum
{
  SIGNAL_CLIENT_ADDED,
  SIGNAL_CLIENT_REMOVED,
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_HOST,
  ARG_PORT,
  ARG_PROTOCOL
};

static void gst_tcpserversink_base_init (gpointer g_class);
static void gst_tcpserversink_class_init (GstTCPServerSink * klass);
static void gst_tcpserversink_init (GstTCPServerSink * tcpserversink);

static void gst_tcpserversink_set_clock (GstElement * element,
    GstClock * clock);

static void gst_tcpserversink_chain (GstPad * pad, GstData * _data);
static GstElementStateReturn gst_tcpserversink_change_state (GstElement *
    element);

static void gst_tcpserversink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tcpserversink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstElementClass *parent_class = NULL;

static guint gst_tcpserversink_signals[LAST_SIGNAL] = { 0 };

GType
gst_tcpserversink_get_type (void)
{
  static GType tcpserversink_type = 0;


  if (!tcpserversink_type) {
    static const GTypeInfo tcpserversink_info = {
      sizeof (GstTCPServerSinkClass),
      gst_tcpserversink_base_init,
      NULL,
      (GClassInitFunc) gst_tcpserversink_class_init,
      NULL,
      NULL,
      sizeof (GstTCPServerSink),
      0,
      (GInstanceInitFunc) gst_tcpserversink_init,
      NULL
    };

    tcpserversink_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstTCPServerSink",
        &tcpserversink_info, 0);
  }
  return tcpserversink_type;
}

static void
gst_tcpserversink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_tcpserversink_details);
}

static void
gst_tcpserversink_class_init (GstTCPServerSink * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_HOST,
      g_param_spec_string ("host", "host", "The host/IP to send the packets to",
          TCP_DEFAULT_HOST, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_PORT,
      g_param_spec_int ("port", "port", "The port to send the packets to",
          0, 32768, TCP_DEFAULT_PORT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PROTOCOL,
      g_param_spec_enum ("protocol", "Protocol", "The protocol to wrap data in",
          GST_TYPE_TCP_PROTOCOL_TYPE, GST_TCP_PROTOCOL_TYPE_NONE,
          G_PARAM_READWRITE));

  gst_tcpserversink_signals[SIGNAL_CLIENT_ADDED] =
      g_signal_new ("client-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstTCPServerSinkClass, client_added),
      NULL, NULL, gst_tcp_marshal_VOID__STRING_UINT, G_TYPE_NONE, 2,
      G_TYPE_STRING, G_TYPE_UINT);
  gst_tcpserversink_signals[SIGNAL_CLIENT_REMOVED] =
      g_signal_new ("client-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstTCPServerSinkClass,
          client_removed), NULL, NULL, gst_tcp_marshal_VOID__STRING_UINT,
      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_UINT);

  gobject_class->set_property = gst_tcpserversink_set_property;
  gobject_class->get_property = gst_tcpserversink_get_property;

  gstelement_class->change_state = gst_tcpserversink_change_state;
  gstelement_class->set_clock = gst_tcpserversink_set_clock;

  GST_DEBUG_CATEGORY_INIT (tcpserversink_debug, "tcpserversink", 0, "TCP sink");
}

static void
gst_tcpserversink_set_clock (GstElement * element, GstClock * clock)
{
  GstTCPServerSink *tcpserversink;

  tcpserversink = GST_TCPSERVERSINK (element);

  tcpserversink->clock = clock;
}

static void
gst_tcpserversink_init (GstTCPServerSink * this)
{
  /* create the sink pad */
  this->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (this), this->sinkpad);
  gst_pad_set_chain_function (this->sinkpad, gst_tcpserversink_chain);

  this->server_port = TCP_DEFAULT_PORT;
  /* should support as minimum 576 for IPV4 and 1500 for IPV6 */
  /* this->mtu = 1500; */

  this->server_sock_fd = -1;
  GST_FLAG_UNSET (this, GST_TCPSERVERSINK_OPEN);

  this->protocol = GST_TCP_PROTOCOL_TYPE_NONE;
  this->clock = NULL;
  this->host = NULL;
}

static void
gst_tcpserversink_debug_fdset (GstTCPServerSink * sink, fd_set * testfds)
{
  int fd;

  for (fd = 0; fd < FD_SETSIZE; fd++) {
    if (FD_ISSET (fd, testfds)) {
      GST_LOG_OBJECT (sink, "fd %d", fd);
    }
  }
}

/* handle a read request on the server,
 * which indicates a new client connection */
static gboolean
gst_tcpserversink_handle_server_read (GstTCPServerSink * sink)
{
  /* new client */
  int client_sock_fd;
  struct sockaddr_storage client_address;
  int client_address_len;
  char clienthost[NI_MAXHOST];
  char clientservice[NI_MAXSERV];
  int error;

  client_sock_fd =
      accept (sink->server_sock_fd, (struct sockaddr *) &client_address,
      &client_address_len);
  if (client_sock_fd == -1) {
    GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (NULL),
        ("Could not accept client on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
  FD_SET (client_sock_fd, &(sink->clientfds));
  error = getnameinfo ((struct sockaddr *) &client_address, client_address_len,
      clienthost, sizeof (clienthost), clientservice,
      sizeof (clientservice), NI_NUMERICHOST);
  if (error != 0) {
    GST_DEBUG_OBJECT (sink, "added new client address %s with fd %d",
        clienthost, client_sock_fd);
  } else {
    GST_DEBUG_OBJECT (sink, "problem error received from getnameinfo: %d",
        error);
  }
  g_signal_emit (G_OBJECT (sink),
      gst_tcpserversink_signals[SIGNAL_CLIENT_ADDED], 0,
      g_strdup (clienthost), client_sock_fd);

  return TRUE;
}

static void
gst_tcpserversink_client_remove (GstTCPServerSink * sink, int fd)
{
  /* FIXME: if we keep track of ip we can log it here and signal */
  GST_DEBUG_OBJECT (sink, "removing client on fd %d", fd);
  if (close (fd) != 0) {
    GST_DEBUG_OBJECT (sink, "error closing fd %d: %s", fd, g_strerror (errno));
  }
  FD_CLR (fd, &sink->clientfds);
  FD_CLR (fd, &sink->caps_sent);
  FD_CLR (fd, &sink->streamheader_sent);
  g_signal_emit (G_OBJECT (sink),
      gst_tcpserversink_signals[SIGNAL_CLIENT_REMOVED], 0, NULL, fd);
}

/* handle a read on a client fd,
 * which either indicates a close or should be ignored
 * returns FALSE if the client has been closed. */
static gboolean
gst_tcpserversink_handle_client_read (GstTCPServerSink * sink, int fd)
{
  int nread;

  GST_LOG_OBJECT (sink, "select reports client read on fd %d", fd);

  ioctl (fd, FIONREAD, &nread);
  if (nread == 0) {
    /* client sent close, so remove it */
    GST_DEBUG_OBJECT (sink, "client asked for close, removing on fd %d", fd);
    gst_tcpserversink_client_remove (sink, fd);
    return FALSE;
  } else {
    /* FIXME: we should probably just Read 'n' Drop */
    g_warning ("Don't know what to do with %d bytes to read", nread);
  }
  return TRUE;
}

/* Write a buffer to the given fd for the given element using the given
 * protocol.
 * Return number of buffer bytes written.
 */
static gint
gst_tcp_buffer_write (GstBuffer * buf, int fd, GstElement * element,
    GstTCPProtocolType protocol)
{
  gint wrote = 0;

  /* write the buffer header if we have one */
  switch (protocol) {
    case GST_TCP_PROTOCOL_TYPE_NONE:
      break;

    case GST_TCP_PROTOCOL_TYPE_GDP:
      GST_LOG_OBJECT (element, "Sending buffer header through GDP");
      if (!gst_tcp_gdp_write_header (element, fd, buf, FALSE, "unknown", 0))
        return 0;
      break;
    default:
      g_warning ("Unhandled protocol type");
      break;
  }

  /* serve data to client */
  GST_LOG_OBJECT (element, "serving data buffer of size %d to client on fd %d",
      GST_BUFFER_SIZE (buf), fd);

  wrote =
      gst_tcp_socket_write (fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  if (wrote < GST_BUFFER_SIZE (buf)) {
/* FIXME: keep track of client ip and port and so on */
/*
              GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
                (_("Error while sending data to \"%s:%d\"."),
                    sink->host, sink->port),
                ("Only %d of %d bytes written: %s",
                  bytes_written, GST_BUFFER_SIZE (buf), g_strerror (errno)));
*/
    /* FIXME: there should be a better way to report problems, since we
       want to continue for other clients and just drop this particular one */
    GST_DEBUG_OBJECT (element, "Write failed: %d of %d bytes written", wrote,
        GST_BUFFER_SIZE (buf));
  }

  return wrote;
}

/* handle a write on a client fd,
 * which indicates a read request from a client */
static gboolean
gst_tcpserversink_handle_client_write (GstTCPServerSink * sink, int fd,
    GstPad * pad, GstBuffer * buf)
{
  gint wrote = 0;

  /* when using GDP, first check if we have sent caps yet */
  if (sink->protocol == GST_TCP_PROTOCOL_TYPE_GDP) {
    if (!FD_ISSET (fd, &(sink->caps_sent))) {
      const GstCaps *caps;
      gchar *string;

      caps = GST_PAD_CAPS (GST_PAD_PEER (pad));
      string = gst_caps_to_string (caps);
      GST_DEBUG_OBJECT (sink, "Sending caps %s for fd %d through GDP", string,
          fd);
      if (!gst_tcp_gdp_write_caps (GST_ELEMENT (sink), fd, caps, FALSE,
              "unknown", 0)) {
        GST_DEBUG_OBJECT (sink, "Failed sending caps, removing client");
        gst_tcpserversink_client_remove (sink, fd);
        g_free (string);
        return FALSE;
      }
      g_free (string);
      FD_SET (fd, &(sink->caps_sent));
    }
  }
  /* if we have streamheader buffers, and haven't sent them to this client
   * yet, send them out one by one */
  if (!FD_ISSET (fd, &(sink->streamheader_sent))) {
    if (sink->streamheader) {
      GList *l;

      for (l = sink->streamheader; l; l = l->next) {
        wrote = gst_tcp_buffer_write (l->data, fd, GST_ELEMENT (sink),
            sink->protocol);
        if (wrote < GST_BUFFER_SIZE (l->data)) {
          GST_DEBUG_OBJECT (sink,
              "Failed sending streamheader, removing client");
          gst_tcpserversink_client_remove (sink, fd);
        }
      }
    }
    FD_SET (fd, &(sink->streamheader_sent));
  }

  /* now we write the data buffer */
  wrote = gst_tcp_buffer_write (buf, fd, GST_ELEMENT (sink), sink->protocol);
  if (wrote < GST_BUFFER_SIZE (buf)) {
    gst_tcpserversink_client_remove (sink, fd);
    /* write failed, so drop the client */
    GST_DEBUG_OBJECT (sink, "removing client on fd %d", fd);
    if (close (fd) != 0) {
      GST_DEBUG_OBJECT (sink, "error closing fd %d after failed write: %s",
          fd, g_strerror (errno));
    }
    return FALSE;
  }
  return TRUE;
}

static void
gst_tcpserversink_chain (GstPad * pad, GstData * _data)
{
  int result;
  int fd;
  fd_set testreadfds, testwritefds;
  struct timeval timeout;
  struct timeval *timeoutp;

  GstBuffer *buf = GST_BUFFER (_data);
  GstTCPServerSink *sink;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  sink = GST_TCPSERVERSINK (GST_OBJECT_PARENT (pad));
  g_return_if_fail (GST_FLAG_IS_SET (sink, GST_TCPSERVERSINK_OPEN));

  if (GST_IS_EVENT (buf)) {
    g_warning ("FIXME: handle events");
    return;
  }

  /* if the incoming buffer is marked as IN CAPS, then we assume for now
   * it's a streamheader that needs to be sent to each new client, so we
   * put it on our internal list of streamheader buffers.
   * After that we return, since we only send these out when we get
   * non IN_CAPS buffers so we properly keep track of clients that got
   * streamheaders. */
  if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_IN_CAPS)) {
    GST_DEBUG_OBJECT (sink,
        "appending IN_CAPS buffer with length %d to streamheader",
        GST_BUFFER_SIZE (buf));
    sink->streamheader = g_list_append (sink->streamheader, buf);
    return;
  }
  /* if the incoming buffer has a duration, we can use that as the timeout
   * value; otherwise, we block */
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  timeoutp = NULL;
  GST_LOG_OBJECT (sink, "incoming buffer duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DURATION (buf))) {
    GST_TIME_TO_TIMEVAL (GST_BUFFER_DURATION (buf), timeout);
    timeoutp = &timeout;
    GST_LOG_OBJECT (sink, "select will be with timeout %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
    GST_LOG_OBJECT (sink, "select will be with timeout %d.%d",
        timeout.tv_sec, timeout.tv_usec);
  }
  /* check for:
   * - server socket input (ie, new client connections)
   * - client socket input (ie, clients saying goodbye)
   * - client socket output (ie, client reads)          */
  testwritefds = sink->clientfds;
  testreadfds = sink->clientfds;
  FD_SET (sink->server_sock_fd, &testreadfds);

  GST_LOG_OBJECT (sink, "doing select on server + client fds for reads");
  gst_tcpserversink_debug_fdset (sink, &testreadfds);
  GST_LOG_OBJECT (sink, "doing select on client fds for writes");
  gst_tcpserversink_debug_fdset (sink, &testwritefds);

  result = select (FD_SETSIZE, &testreadfds, &testwritefds, (fd_set *) 0,
      timeoutp);
  /* < 0 is an error, 0 just means a timeout happened */
  if (result < 0) {
    GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
        ("select failed: %s", g_strerror (errno)));
    return;
  }
  GST_LOG_OBJECT (sink, "%d sockets had action", result);
  GST_LOG_OBJECT (sink, "done select on server/client fds for reads");
  gst_tcpserversink_debug_fdset (sink, &testreadfds);
  GST_LOG_OBJECT (sink, "done select on client fds for writes");
  gst_tcpserversink_debug_fdset (sink, &testwritefds);

  /* Check the reads */
  for (fd = 0; fd < FD_SETSIZE; fd++) {
    if (FD_ISSET (fd, &testreadfds)) {
      if (fd == sink->server_sock_fd) {
        /* handle new client connection on server socket */
        if (!gst_tcpserversink_handle_server_read (sink))
          return;
      } else {
        /* handle client read */
        if (!gst_tcpserversink_handle_client_read (sink, fd))
          return;
      }
    }
  }

  /* Check the writes */
  for (fd = 0; fd < FD_SETSIZE; fd++) {
    if (FD_ISSET (fd, &testwritefds)) {
      if (!gst_tcpserversink_handle_client_write (sink, fd, pad, buf)) {
        gst_buffer_unref (buf);
        return;
      }
    }
  }
  sink->data_written += GST_BUFFER_SIZE (buf);

  gst_buffer_unref (buf);

}

static void
gst_tcpserversink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTCPServerSink *tcpserversink;

  g_return_if_fail (GST_IS_TCPSERVERSINK (object));
  tcpserversink = GST_TCPSERVERSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_free (tcpserversink->host);
      tcpserversink->host = g_strdup (g_value_get_string (value));
      break;
    case ARG_PORT:
      tcpserversink->server_port = g_value_get_int (value);
      break;
    case ARG_PROTOCOL:
      tcpserversink->protocol = g_value_get_enum (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tcpserversink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstTCPServerSink *tcpserversink;

  g_return_if_fail (GST_IS_TCPSERVERSINK (object));
  tcpserversink = GST_TCPSERVERSINK (object);

  switch (prop_id) {
    case ARG_HOST:
      g_value_set_string (value, tcpserversink->host);
      break;
    case ARG_PORT:
      g_value_set_int (value, tcpserversink->server_port);
      break;
    case ARG_PROTOCOL:
      g_value_set_enum (value, tcpserversink->protocol);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


/* create a socket for sending to remote machine */
static gboolean
gst_tcpserversink_init_send (GstTCPServerSink * this)
{
  int ret, error;
  struct addrinfo hints, *res, *ressave;
  char *tempport;


  /* name the socket */
  memset (&hints, 0, sizeof (struct addrinfo));
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  tempport = g_strdup_printf ("%d", this->server_port);

  error = getaddrinfo (this->host, tempport, &hints, &res);
  g_free (tempport);
  if (error != 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("getaddrinfo failed: %s", gai_strerror (error)));
    return FALSE;
  }
  ressave = res;

  /* Try open socket with each address getaddrinfo returned, until getting 
     a valid listening socket */
  this->server_sock_fd = -1;
  while (res) {
    this->server_sock_fd =
        socket (res->ai_family, res->ai_socktype, res->ai_protocol);
    if (this->server_sock_fd >= 0) {
      ret = bind (this->server_sock_fd, res->ai_addr, res->ai_addrlen);
      if (ret == 0)
        break;
      close (this->server_sock_fd);
      this->server_sock_fd = -1;
    }
    res = res->ai_next;
  }
  freeaddrinfo (ressave);

  if (this->server_sock_fd < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("bind failed: %s", g_strerror (errno)));
    return FALSE;
  }

  /* make address reusable */
  if (setsockopt (this->server_sock_fd, SOL_SOCKET, SO_REUSEADDR, &ret,
          sizeof (int)) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
  /* keep connection alive; avoids SIGPIPE during write */
  if (setsockopt (this->server_sock_fd, SOL_SOCKET, SO_KEEPALIVE, &ret,
          sizeof (int)) < 0) {
    GST_ELEMENT_ERROR (this, RESOURCE, SETTINGS, (NULL),
        ("Could not setsockopt: %s", g_strerror (errno)));
    return FALSE;
  }
  /* set the server socket to nonblocking */
  fcntl (this->server_sock_fd, F_SETFL, O_NONBLOCK);

  GST_DEBUG_OBJECT (this, "listening on server socket %d with queue of %d",
      this->server_sock_fd, TCP_BACKLOG);
  if (listen (this->server_sock_fd, TCP_BACKLOG) == -1) {
    GST_ELEMENT_ERROR (this, RESOURCE, OPEN_READ, (NULL),
        ("Could not listen on server socket: %s", g_strerror (errno)));
    return FALSE;
  }
  GST_DEBUG_OBJECT (this,
      "listened on server socket %d, returning from connection setup",
      this->server_sock_fd);

  FD_ZERO (&this->clientfds);
  FD_ZERO (&this->caps_sent);
  FD_ZERO (&this->streamheader_sent);
  FD_SET (this->server_sock_fd, &this->clientfds);
  GST_FLAG_SET (this, GST_TCPSERVERSINK_OPEN);
  this->streamheader = NULL;

  this->data_written = 0;

  return TRUE;
}

static void
gst_tcpserversink_close (GstTCPServerSink * this)
{
  if (this->server_sock_fd != -1) {
    close (this->server_sock_fd);
    this->server_sock_fd = -1;
  }

  if (this->streamheader) {
    GList *l;

    for (l = this->streamheader; l; l = l->next) {
      gst_buffer_unref (l->data);
    }
    g_list_free (this->streamheader);
  }
  GST_FLAG_UNSET (this, GST_TCPSERVERSINK_OPEN);
}

static GstElementStateReturn
gst_tcpserversink_change_state (GstElement * element)
{
  g_return_val_if_fail (GST_IS_TCPSERVERSINK (element), GST_STATE_FAILURE);

  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_TCPSERVERSINK_OPEN))
      gst_tcpserversink_close (GST_TCPSERVERSINK (element));
  } else {
    if (!GST_FLAG_IS_SET (element, GST_TCPSERVERSINK_OPEN)) {
      if (!gst_tcpserversink_init_send (GST_TCPSERVERSINK (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
