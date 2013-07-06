/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * GData Client
 * Copyright (C) Philip Withnall 2013 <philip@tecnocode.co.uk>
 * 
 * GData Client is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * GData Client is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GData Client.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <glib/gi18n.h>
#include <libsoup/soup.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "mock-resolver.h"
#include "mock-server.h"

static void gdata_mock_server_dispose (GObject *object);
static void gdata_mock_server_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec);
static void gdata_mock_server_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec);

static gboolean real_handle_message (GDataMockServer *self, SoupMessage *message, SoupClientContext *client);

static void server_handler_cb (SoupServer *server, SoupMessage *message, const gchar *path, GHashTable *query, SoupClientContext *client, gpointer user_data);
static void load_file_stream_thread_cb (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void load_file_iteration_thread_cb (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);

static GFileInputStream *load_file_stream (GFile *trace_file, GCancellable *cancellable, GError **error);
static SoupMessage *load_file_iteration (GFileInputStream *input_stream, GCancellable *cancellable, GError **error);

struct _GDataMockServerPrivate {
	SoupServer *server;
	GDataMockResolver *resolver;
	GThread *server_thread;

	GFile *trace_file;
	GFileInputStream *input_stream;
	GFileOutputStream *output_stream;
	SoupMessage *next_message;

	GFile *trace_directory;
	gboolean enable_online;
	gboolean enable_logging;
};

enum {
	PROP_TRACE_DIRECTORY = 1,
	PROP_ENABLE_ONLINE,
	PROP_ENABLE_LOGGING,
};

enum {
	SIGNAL_HANDLE_MESSAGE = 1,
	LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GDataMockServer, gdata_mock_server, G_TYPE_OBJECT)

static void
gdata_mock_server_class_init (GDataMockServerClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (GDataMockServerPrivate));

	gobject_class->get_property = gdata_mock_server_get_property;
	gobject_class->set_property = gdata_mock_server_set_property;
	gobject_class->dispose = gdata_mock_server_dispose;

	klass->handle_message = real_handle_message;

	/**
	 * TODO: Document me.
	 */
	g_object_class_install_property (gobject_class, PROP_TRACE_DIRECTORY,
	                                 g_param_spec_object ("trace-directory",
	                                                      "Trace Directory", "TODO",
	                                                      G_TYPE_FILE,
	                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * TODO: Document me.
	 */
	g_object_class_install_property (gobject_class, PROP_ENABLE_ONLINE,
	                                 g_param_spec_boolean ("enable-online",
	                                                       "Enable Online", "TODO",
	                                                       FALSE,
	                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * TODO: Document me.
	 */
	g_object_class_install_property (gobject_class, PROP_ENABLE_LOGGING,
	                                 g_param_spec_boolean ("enable-logging",
	                                                       "Enable Logging", "TODO",
	                                                       FALSE,
	                                                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

	/**
	 * TODO: Document me.
	 */
	signals[SIGNAL_HANDLE_MESSAGE] = g_signal_new ("handle-message", G_OBJECT_CLASS_TYPE (klass), G_SIGNAL_RUN_LAST,
	                                               G_STRUCT_OFFSET (GDataMockServerClass, handle_message),
	                                               g_signal_accumulator_true_handled, NULL,
	                                               g_cclosure_marshal_generic,
	                                               G_TYPE_BOOLEAN, 2,
	                                               SOUP_TYPE_MESSAGE, SOUP_TYPE_CLIENT_CONTEXT);
}

static void
gdata_mock_server_init (GDataMockServer *self)
{
	self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, GDATA_TYPE_MOCK_SERVER, GDataMockServerPrivate);
}

static void
gdata_mock_server_dispose (GObject *object)
{
	GDataMockServerPrivate *priv = GDATA_MOCK_SERVER (object)->priv;

	g_clear_object (&priv->resolver);
	g_clear_object (&priv->server);
	g_clear_object (&priv->input_stream);
	g_clear_object (&priv->trace_file);

	/* TODO: More. */
	if (priv->server_thread != NULL) {
		g_thread_unref (priv->server_thread);
		priv->server_thread = NULL;
	}

	/* Chain up to the parent class */
	G_OBJECT_CLASS (gdata_mock_server_parent_class)->dispose (object);
}

static void
gdata_mock_server_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
	GDataMockServerPrivate *priv = GDATA_MOCK_SERVER (object)->priv;

	switch (property_id) {
		case PROP_TRACE_DIRECTORY:
			g_value_set_object (value, priv->trace_directory);
			break;
		case PROP_ENABLE_ONLINE:
			g_value_set_boolean (value, priv->enable_online);
			break;
		case PROP_ENABLE_LOGGING:
			g_value_set_boolean (value, priv->enable_logging);
			break;
		default:
			/* We don't have any other property... */
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static void
gdata_mock_server_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	GDataMockServer *self = GDATA_MOCK_SERVER (object);

	switch (property_id) {
		case PROP_TRACE_DIRECTORY:
			gdata_mock_server_set_trace_directory (self, g_value_get_object (value));
			break;
		case PROP_ENABLE_ONLINE:
			gdata_mock_server_set_enable_online (self, g_value_get_boolean (value));
			break;
		case PROP_ENABLE_LOGGING:
			gdata_mock_server_set_enable_logging (self, g_value_get_boolean (value));
			break;
		default:
			/* We don't have any other property... */
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
			break;
	}
}

static inline gboolean
parts_equal (const char *one, const char *two, gboolean insensitive)
{
	if (!one && !two)
		return TRUE;
	if (!one || !two)
		return FALSE;
	return insensitive ? !g_ascii_strcasecmp (one, two) : !strcmp (one, two);
}

static gint
compare_incoming_message (SoupMessage *expected_message, SoupMessage *actual_message, SoupClientContext *actual_client)
{
	SoupURI *expected_uri, *actual_uri;

	expected_uri = soup_message_get_uri (expected_message);
	actual_uri = soup_message_get_uri (actual_message);

	/* TODO: Also compare actual_client auth domains and address? HTTP version? Method? */

	if (!parts_equal (expected_uri->user, actual_uri->user, FALSE) ||
	    !parts_equal (expected_uri->password, actual_uri->password, FALSE) ||
	    !parts_equal (expected_uri->path, actual_uri->path, FALSE) ||
	    !parts_equal (expected_uri->query, actual_uri->query, FALSE) ||
	    !parts_equal (expected_uri->fragment, actual_uri->fragment, FALSE)) {
		return 1;
	}

	return 0;
}

static void
header_append_cb (const gchar *name, const gchar *value, gpointer user_data)
{
	SoupMessage *message = user_data;

	soup_message_headers_append (message->response_headers, name, value);
}

/* TODO: Some problem with client-side cancellation and SSL handshakes in here. */
/* TODO: This needs to be overrideable. */


static void
server_process_message (GDataMockServer *self, SoupMessage *message, SoupClientContext *client)
{
	GDataMockServerPrivate *priv = self->priv;
	SoupBuffer *message_body;

	g_assert (priv->next_message != NULL);

	if (compare_incoming_message (priv->next_message, message, client) != 0) {
		gchar *body, *next_uri, *actual_uri;

		/* Received message is not what we expected. Return an error. */
		soup_message_set_status_full (message, SOUP_STATUS_BAD_REQUEST, _("Unexpected request to mock server"));

		next_uri = soup_uri_to_string (soup_message_get_uri (priv->next_message), TRUE);
		actual_uri = soup_uri_to_string (soup_message_get_uri (message), TRUE);
		body = g_strdup_printf ("Expected URI ‘%s’, but got ‘%s’.", next_uri, actual_uri);
		g_free (actual_uri);
		g_free (next_uri);
		soup_message_body_append_take (message->response_body, (guchar *) body, strlen (body));

		return;
	}

	/* The incoming message matches what we expected, so copy the headers and body from the expected response and return it. */
	soup_message_set_http_version (message, soup_message_get_http_version (priv->next_message));
	soup_message_set_status_full (message, priv->next_message->status_code, priv->next_message->reason_phrase);
	soup_message_headers_foreach (priv->next_message->response_headers, header_append_cb, message);

	message_body = soup_message_body_flatten (priv->next_message->response_body);
	if (message_body->length > 0) {
		soup_message_body_append_buffer (message->response_body, message_body);
	}
	soup_buffer_free (message_body);

	soup_message_body_complete (message->response_body);

	/* Clear the expected message. */
	g_clear_object (&priv->next_message);
}

static void
server_handler_cb (SoupServer *server, SoupMessage *message, const gchar *path, GHashTable *query, SoupClientContext *client, gpointer user_data)
{
	GDataMockServer *self = user_data;
	gboolean message_handled = FALSE;

	soup_server_pause_message (server, message);
	g_signal_emit (self, signals[SIGNAL_HANDLE_MESSAGE], 0, message, client, &message_handled);
	soup_server_unpause_message (server, message);

	/* The message should always be handled by real_handle_message() at least. */
	g_assert (message_handled == TRUE);
}

static gboolean
real_handle_message (GDataMockServer *self, SoupMessage *message, SoupClientContext *client)
{
	GDataMockServerPrivate *priv = self->priv;
	gboolean handled = FALSE;

	/* Asynchronously load the next expected message from the trace file. */
	if (priv->next_message == NULL) {
		GTask *task;
		GError *child_error = NULL;

		task = g_task_new (self, NULL, NULL, NULL);
		g_task_set_task_data (task, g_object_ref (priv->input_stream), g_object_unref);
		g_task_run_in_thread_sync (task, load_file_iteration_thread_cb);

		/* Handle the results. */
		priv->next_message = g_task_propagate_pointer (task, &child_error);

		g_object_unref (task);

		if (child_error != NULL) {
			gchar *body;

			soup_message_set_status_full (message, SOUP_STATUS_INTERNAL_SERVER_ERROR, _("Error loading expected request"));

			body = g_strdup_printf ("Error: %s", child_error->message);
			soup_message_body_append_take (message->response_body, (guchar *) body, strlen (body));
			handled = TRUE;

			g_error_free (child_error);
		} else if (priv->next_message == NULL) {
			gchar *body, *actual_uri;

			/* Received message is not what we expected. Return an error. */
			soup_message_set_status_full (message, SOUP_STATUS_BAD_REQUEST, _("Unexpected request to mock server"));

			actual_uri = soup_uri_to_string (soup_message_get_uri (message), TRUE);
			body = g_strdup_printf ("Expected no request, but got ‘%s’.", actual_uri);
			g_free (actual_uri);
			soup_message_body_append_take (message->response_body, (guchar *) body, strlen (body));
			handled = TRUE;
		}
	}

	/* Process the actual message if we already know the expected message. */
	g_assert (priv->next_message != NULL || handled == TRUE);
	if (handled == FALSE) {
		server_process_message (self, message, client);
		handled = TRUE;
	}

	g_assert (handled == TRUE);
	return handled;
}

/**
 * TODO: Document me.
 */
GDataMockServer *
gdata_mock_server_new (void)
{
	return g_object_new (GDATA_TYPE_MOCK_SERVER, NULL);
}

static gboolean
trace_to_soup_message_headers_and_body (SoupMessageHeaders *message_headers, SoupMessageBody *message_body, const gchar message_direction, const gchar **_trace)
{
	const gchar *i;
	const gchar *trace = *_trace;

	/* Parse headers. */
	while (TRUE) {
		gchar *header_name, *header_value;

		if (*trace == '\0') {
			/* No body. */
			goto done;
		} else if (*trace == ' ' && *(trace + 1) == ' ' && *(trace + 2) == '\n') {
			/* No body. */
			trace += 3;
			goto done;
		} else if (*trace != message_direction || *(trace + 1) != ' ') {
			g_warning ("Unrecognised start sequence ‘%c%c’.", *trace, *(trace + 1));
			goto error;
		}
		trace += 2;

		if (*trace == '\n') {
			/* Reached the end of the headers. */
			trace++;
			break;
		}

		i = strchr (trace, ':');
		if (i == NULL || *(i + 1) != ' ') {
			g_warning ("Missing spacer ‘: ’.");
			goto error;
		}

		header_name = g_strndup (trace, i - trace);
		trace += (i - trace) + 2;

		i = strchr (trace, '\n');
		if (i == NULL) {
			g_warning ("Missing spacer ‘\\n’.");
			goto error;
		}

		header_value = g_strndup (trace, i - trace);
		trace += (i - trace) + 1;

		/* Append the header. */
		soup_message_headers_append (message_headers, header_name, header_value);

		g_free (header_value);
		g_free (header_name);
	}

	/* Parse the body. */
	while (TRUE) {
		if (*trace == ' ' && *(trace + 1) == ' ' && *(trace + 2) == '\n') {
			/* End of the body. */
			trace += 3;
			break;
		} else if (*trace == '\0') {
			/* End of the body. */
			break;
		} else if (*trace != message_direction || *(trace + 1) != ' ') {
			g_warning ("Unrecognised start sequence ‘%c%c’.", *trace, *(trace + 1));
			goto error;
		}
		trace += 2;

		i = strchr (trace, '\n');
		if (i == NULL) {
			g_warning ("Missing spacer ‘\\n’.");
			goto error;
		}

		soup_message_body_append (message_body, SOUP_MEMORY_COPY, trace, i - trace + 1); /* include trailing \n */
		trace += (i - trace) + 1;
	}

done:
	/* Done. Update the output trace pointer. */
	soup_message_body_complete (message_body);
	*_trace = trace;

	return TRUE;

error:
	return FALSE;
}

static SoupMessage *
trace_to_soup_message (const gchar *trace)
{
	SoupMessage *message;
	const gchar *i, *j, *method;
	gchar *uri_string, *response_message;
	SoupHTTPVersion http_version;
	guint response_status;
	SoupURI *base_uri, *uri;

	g_return_val_if_fail (trace != NULL, NULL);

	/* The traces look somewhat like this:
	 * > POST /unauth HTTP/1.1
	 * > Soup-Debug-Timestamp: 1200171744
	 * > Soup-Debug: SoupSessionAsync 1 (0x612190), SoupMessage 1 (0x617000), SoupSocket 1 (0x612220)
	 * > Host: localhost
	 * > Content-Type: text/plain
	 * > Connection: close
	 * > 
	 * > This is a test.
	 *   
	 * < HTTP/1.1 201 Created
	 * < Soup-Debug-Timestamp: 1200171744
	 * < Soup-Debug: SoupMessage 1 (0x617000)
	 * < Date: Sun, 12 Jan 2008 21:02:24 GMT
	 * < Content-Length: 0
	 *
	 * This function parses a single request–response pair.
	 */

	/* Parse the method, URI and HTTP version first. */
	if (*trace != '>' || *(trace + 1) != ' ') {
		g_warning ("Unrecognised start sequence ‘%c%c’.", *trace, *(trace + 1));
		goto error;
	}
	trace += 2;

	/* Parse “POST /unauth HTTP/1.1”. */
	if (strncmp (trace, "POST", strlen ("POST")) == 0) {
		method = SOUP_METHOD_POST;
		trace += strlen ("POST");
	} else if (strncmp (trace, "GET", strlen ("GET")) == 0) {
		method = SOUP_METHOD_GET;
		trace += strlen ("GET");
	} else if (strncmp (trace, "DELETE", strlen ("DELETE")) == 0) {
		method = SOUP_METHOD_DELETE;
		trace += strlen ("DELETE");
	} else {
		g_warning ("Unknown method ‘%s’.", trace);
		goto error;
	}

	if (*trace != ' ') {
		g_warning ("Unrecognised spacer ‘%c’.", *trace);
		goto error;
	}
	trace++;

	i = strchr (trace, ' ');
	if (i == NULL) {
		g_warning ("Missing spacer ‘ ’.");
		goto error;
	}

	uri_string = g_strndup (trace, i - trace);
	trace += (i - trace) + 1;

	if (strncmp (trace, "HTTP/1.1", strlen ("HTTP/1.1")) == 0) {
		http_version = SOUP_HTTP_1_1;
		trace += strlen ("HTTP/1.1");
	} else if (strncmp (trace, "HTTP/1.0", strlen ("HTTP/1.0")) == 0) {
		http_version = SOUP_HTTP_1_0;
		trace += strlen ("HTTP/1.0");
	} else {
		g_warning ("Unrecognised HTTP version ‘%s’.", trace);
	}

	if (*trace != '\n') {
		g_warning ("Unrecognised spacer ‘%c’.", *trace);
		goto error;
	}
	trace++;

	/* Build the message. */
	base_uri = soup_uri_new ("https://127.0.0.1:443"); /* TODO */
	uri = soup_uri_new_with_base (base_uri, uri_string);
	message = soup_message_new_from_uri (method, uri);

	soup_uri_free (uri);
	soup_uri_free (base_uri);

	if (message == NULL) {
		g_warning ("Invalid URI ‘%s’.", uri_string);
		goto error;
	}

	soup_message_set_http_version (message, http_version);
	g_free (uri_string);

	/* Parse the request headers and body. */
	if (trace_to_soup_message_headers_and_body (message->request_headers, message->request_body, '>', &trace) == FALSE) {
		goto error;
	}

	/* Parse the response, starting with “HTTP/1.1 201 Created”. */
	if (*trace != '<' || *(trace + 1) != ' ') {
		g_warning ("Unrecognised start sequence ‘%c%c’.", *trace, *(trace + 1));
		goto error;
	}
	trace += 2;

	if (strncmp (trace, "HTTP/1.1", strlen ("HTTP/1.1")) == 0) {
		http_version = SOUP_HTTP_1_1;
		trace += strlen ("HTTP/1.1");
	} else if (strncmp (trace, "HTTP/1.0", strlen ("HTTP/1.0")) == 0) {
		http_version = SOUP_HTTP_1_0;
		trace += strlen ("HTTP/1.0");
	} else {
		g_warning ("Unrecognised HTTP version ‘%s’.", trace);
	}

	if (*trace != ' ') {
		g_warning ("Unrecognised spacer ‘%c’.", *trace);
		goto error;
	}
	trace++;

	i = strchr (trace, ' ');
	if (i == NULL) {
		g_warning ("Missing spacer ‘ ’.");
		goto error;
	}

	response_status = g_ascii_strtoull (trace, (gchar **) &j, 10);
	if (j != i) {
		g_warning ("Invalid status ‘%s’.", trace);
		goto error;
	}
	trace += (i - trace) + 1;

	i = strchr (trace, '\n');
	if (i == NULL) {
		g_warning ("Missing spacer ‘\n’.");
		goto error;
	}

	response_message = g_strndup (trace, i - trace);
	trace += (i - trace) + 1;

	soup_message_set_status_full (message, response_status, response_message);

	/* Parse the response headers and body. */
	if (trace_to_soup_message_headers_and_body (message->response_headers, message->response_body, '<', &trace) == FALSE) {
		goto error;
	}

	return message;

error:
	g_object_unref (message);

	return NULL;
}

static GFileInputStream *
load_file_stream (GFile *trace_file, GCancellable *cancellable, GError **error)
{
	return g_file_read (trace_file, cancellable, error);
}

static gboolean
load_message_half (GFileInputStream *input_stream, GString *current_message, gchar half_direction, GCancellable *cancellable, GError **error)
{
	guint8 buf[1024];
	gssize len;

	while (TRUE) {
		len = g_input_stream_read (G_INPUT_STREAM (input_stream), buf, sizeof (buf), cancellable, error);

		if (len == -1) {
			/* Error. */
			return FALSE;
		} else if (len == 0) {
			/* EOF. Try again to grab a response. */
			return TRUE;
		} else {
			const guint8 *i;

			/* Got some data. Parse it and see if we've reached the end of a message (i.e. read both the request and the response). */
			for (i = memchr (buf, half_direction, sizeof (buf)); i != NULL; i = memchr (i + 1, half_direction, buf + sizeof (buf) - i)) {
				if (*(i + 1) == ' ' && (i == buf || (i > buf && *(i - 1) == '\n'))) {
					/* Found the boundary between request and response.
					 * Fall through to try and find the boundary between this response and the next request.
					 * To make things simpler, seek back to the boundary and make a second read request below. */
					if (g_seekable_seek (G_SEEKABLE (input_stream), i - (buf + len), G_SEEK_CUR, cancellable, error) == FALSE) {
						/* Error. */
						return FALSE;
					}

					g_string_append_len (current_message, (gchar *) buf, i - buf);

					return TRUE;
				}
			}

			/* Reached the end of the buffer without finding a change in message. Loop around and load another buffer-full. */
			g_string_append_len (current_message, (gchar *) buf, len);
		}
	}

	return TRUE;
}

static SoupMessage *
load_file_iteration (GFileInputStream *input_stream, GCancellable *cancellable, GError **error)
{
	SoupMessage *output_message = NULL;
	GString *current_message = NULL;

	/* Start loading from the stream. */
	current_message = g_string_new (NULL);

	/* We should be at the start of a request (>). Search for the start of the response (<), then for the start of the next request (>). */
	if (load_message_half (input_stream, current_message, '<', cancellable, error) == FALSE ||
	    load_message_half (input_stream, current_message, '>', cancellable, error) == FALSE) {
		goto done;
	}

	if (current_message->len > 0) {
		output_message = trace_to_soup_message (current_message->str);
	} else {
		/* Reached the end of the file. */
		output_message = NULL;
	}

done:
	/* Tidy up. */
	if (current_message != NULL) {
		g_string_free (current_message, TRUE);
	}

	/* Postcondition: (output_message != NULL) => (*error == NULL). */
	g_assert (output_message == NULL || (error == NULL || *error == NULL));

	return output_message;
}

static void
load_file_stream_thread_cb (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
	GFile *trace_file;
	GFileInputStream *input_stream;
	GError *child_error = NULL;

	trace_file = task_data;
	g_assert (G_IS_FILE (trace_file));

	input_stream = load_file_stream (trace_file, cancellable, &child_error);

	if (child_error != NULL) {
		g_task_return_error (task, child_error);
	} else {
		g_task_return_pointer (task, input_stream, g_object_unref);
	}
}

static void
load_file_iteration_thread_cb (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
	GFileInputStream *input_stream;
	SoupMessage *output_message;
	GError *child_error = NULL;

	input_stream = task_data;
	g_assert (G_IS_FILE_INPUT_STREAM (input_stream));

	output_message = load_file_iteration (input_stream, cancellable, &child_error);

	if (child_error != NULL) {
		g_task_return_error (task, child_error);
	} else {
		g_task_return_pointer (task, output_message, g_object_unref);
	}
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_load_trace (GDataMockServer *self, GFile *trace_file, GCancellable *cancellable, GError **error)
{
	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (G_IS_FILE (trace_file));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (error == NULL || *error == NULL);
	g_return_if_fail (self->priv->trace_file == NULL && self->priv->input_stream == NULL && self->priv->next_message == NULL);

	self->priv->trace_file = g_object_ref (trace_file);
	self->priv->input_stream = load_file_stream (self->priv->trace_file, cancellable, error);
	if (self->priv->input_stream != NULL) {
		self->priv->next_message = load_file_iteration (self->priv->input_stream, cancellable, error);
	}
}

typedef struct {
	GAsyncReadyCallback callback;
	gpointer user_data;
} LoadTraceData;

static void
load_trace_async_cb (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
	GDataMockServer *self = GDATA_MOCK_SERVER (source_object);
	LoadTraceData *data = user_data;
	GTask *task;
	GError *child_error = NULL;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (G_IS_ASYNC_RESULT (result));
	g_return_if_fail (g_task_is_valid (result, self));

	self->priv->input_stream = g_task_propagate_pointer (G_TASK (result), &child_error);

	task = g_task_new (g_task_get_source_object (G_TASK (result)), g_task_get_cancellable (G_TASK (result)), data->callback, data->user_data);
	g_task_set_task_data (task, g_object_ref (self->priv->input_stream), g_object_unref);

	if (child_error != NULL) {
		g_task_return_error (task, child_error);
	} else {
		g_task_run_in_thread (task, load_file_iteration_thread_cb);
	}

	g_object_unref (task);

	g_slice_free (LoadTraceData, data);
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_load_trace_async (GDataMockServer *self, GFile *trace_file, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data)
{
	GTask *task;
	LoadTraceData *data;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (G_IS_FILE (trace_file));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (self->priv->trace_file == NULL && self->priv->input_stream == NULL && self->priv->next_message == NULL);

	self->priv->trace_file = g_object_ref (trace_file);

	data = g_slice_new (LoadTraceData);
	data->callback = callback;
	data->user_data = user_data;

	task = g_task_new (self, cancellable, load_trace_async_cb, data);
	g_task_set_task_data (task, g_object_ref (self->priv->trace_file), g_object_unref);
	g_task_run_in_thread (task, load_file_stream_thread_cb);
	g_object_unref (task);
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_load_trace_finish (GDataMockServer *self, GAsyncResult *result, GError **error)
{
	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (G_IS_ASYNC_RESULT (result));
	g_return_if_fail (error == NULL || *error == NULL);
	g_return_if_fail (g_task_is_valid (result, self));

	self->priv->next_message = g_task_propagate_pointer (G_TASK (result), error);
}

static gpointer
server_thread_cb (gpointer user_data)
{
	GDataMockServer *self = user_data;
	GDataMockServerPrivate *priv = self->priv;

	/* Run the server. */
	soup_server_run (priv->server);

	return NULL;
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_run (GDataMockServer *self)
{
	GDataMockServerPrivate *priv = self->priv;
	struct sockaddr_in sock;
	SoupAddress *addr;
	GMainContext *thread_context;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (priv->resolver == NULL);
	g_return_if_fail (priv->server == NULL);

	/* Grab a loopback IP to use. */
	memset (&sock, 0, sizeof (sock));
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = htonl (INADDR_LOOPBACK); /* TODO: don't hard-code this */
	sock.sin_port = htons (443);

	addr = soup_address_new_from_sockaddr ((struct sockaddr *) &sock, sizeof (sock));
	g_assert (addr != NULL);

	/* Set up the resolver, adding TODO */
	priv->resolver = gdata_mock_resolver_new ();
	gdata_mock_resolver_add_A (priv->resolver, "www.google.com", "127.0.0.1"); /* TODO: get IP from soup */
	gdata_mock_resolver_add_A (priv->resolver, "gdata.youtube.com", "127.0.0.1"); /* TODO */
	gdata_mock_resolver_add_A (priv->resolver, "uploads.gdata.youtube.com", "127.0.0.1"); /* TODO */

	g_resolver_set_default (G_RESOLVER (priv->resolver));

	/* Set up the server. */
	thread_context = g_main_context_new ();
	priv->server = soup_server_new ("interface", addr,
	                                "port", 443,
	                                "ssl-cert-file", "/etc/pki/tls/certs/localhost.crt", /* TODO */
	                                "ssl-key-file", "/etc/pki/tls/private/localhost.key" /* TODO */,
	                                "async-context", thread_context,
	                                NULL);
	soup_server_add_handler (priv->server, "/", server_handler_cb, self, NULL);

	g_main_context_unref (thread_context);
	g_object_unref (addr);

	/* Start the network thread. */
	priv->server_thread = g_thread_new ("mock-server-thread", server_thread_cb, self);
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_stop (GDataMockServer *self)
{
	GDataMockServerPrivate *priv = self->priv;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (priv->server != NULL);
	g_return_if_fail (priv->resolver != NULL);

	/* Stop the server. */
	soup_server_disconnect (priv->server);
	g_thread_join (priv->server_thread);
	priv->server_thread = NULL;
	gdata_mock_resolver_reset (priv->resolver);

	g_clear_object (&priv->server);
	g_clear_object (&priv->resolver);

	/* Reset the trace file. */
	g_clear_object (&priv->next_message);
	g_clear_object (&priv->input_stream);
	g_clear_object (&priv->trace_file);
}

/**
 * TODO: Document me.
 */
GFile *
gdata_mock_server_get_trace_directory (GDataMockServer *self)
{
	g_return_val_if_fail (GDATA_IS_MOCK_SERVER (self), NULL);

	return self->priv->trace_directory;
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_set_trace_directory (GDataMockServer *self, GFile *trace_directory)
{
	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (trace_directory == NULL || G_IS_FILE (trace_directory));

	if (trace_directory != NULL) {
		g_object_ref (trace_directory);
	}

	g_clear_object (&self->priv->trace_directory);
	self->priv->trace_directory = trace_directory;
	g_object_notify (G_OBJECT (self), "trace-directory");
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_start_trace (GDataMockServer *self, const gchar *trace_name)
{
	GFile *trace_file;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (trace_name != NULL && *trace_name != '\0');

	g_assert (self->priv->trace_directory != NULL);

	trace_file = g_file_get_child (self->priv->trace_directory, trace_name);
	gdata_mock_server_start_trace_full (self, trace_file);
	g_object_unref (trace_file);
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_start_trace_full (GDataMockServer *self, GFile *trace_file)
{
	GDataMockServerPrivate *priv = self->priv;
	GError *child_error = NULL;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (G_IS_FILE (trace_file));

	g_return_if_fail (priv->output_stream == NULL);

	/* TODO */
	if (priv->enable_logging == TRUE) {
		priv->output_stream = g_file_replace (trace_file, NULL, FALSE, G_FILE_CREATE_NONE, NULL, &child_error);

		if (child_error != NULL) {
			gchar *trace_file_path;

			trace_file_path = g_file_get_path (trace_file);
			g_warning ("Error replacing trace file ‘%s’: %s", trace_file_path, child_error->message);
			g_free (trace_file_path);

			g_error_free (child_error);

			return;
		}
	}

	if (priv->enable_online == FALSE) {
		gdata_mock_server_load_trace (self, trace_file, NULL, &child_error);

		if (child_error != NULL) {
			gchar *trace_file_path = g_file_get_path (trace_file);
			g_error ("Error loading trace file ‘%s’: %s", trace_file_path, child_error->message);
			g_free (trace_file_path);

			g_error_free (child_error);

			return;
		}

		gdata_mock_server_run (self);
	}
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_end_trace (GDataMockServer *self)
{
	GDataMockServerPrivate *priv = self->priv;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));

	if (priv->enable_online == FALSE) {
		gdata_mock_server_stop (self);
	}
	/* TODO: Integrate logging. */

	if (priv->enable_logging == TRUE) {
		g_clear_object (&self->priv->output_stream);
	}
}

/**
 * TODO: Document me.
 */
gboolean
gdata_mock_server_get_enable_online (GDataMockServer *self)
{
	g_return_val_if_fail (GDATA_IS_MOCK_SERVER (self), FALSE);

	return self->priv->enable_online;
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_set_enable_online (GDataMockServer *self, gboolean enable_online)
{
	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));

	self->priv->enable_online = enable_online;
	g_object_notify (G_OBJECT (self), "enable-online");
}

/**
 * TODO: Document me.
 */
gboolean
gdata_mock_server_get_enable_logging (GDataMockServer *self)
{
	g_return_val_if_fail (GDATA_IS_MOCK_SERVER (self), FALSE);

	return self->priv->enable_logging;
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_set_enable_logging (GDataMockServer *self, gboolean enable_logging)
{
	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));

	self->priv->enable_logging = enable_logging;
	g_object_notify (G_OBJECT (self), "enable-logging");

	/* TODO: Disable logging if we're currently in a trace section? */
}

/**
 * TODO: Document me.
 */
void
gdata_mock_server_log_message_chunk (GDataMockServer *self, const gchar *message_chunk)
{
	GDataMockServerPrivate *priv = self->priv;
	GError *child_error = NULL;

	g_return_if_fail (GDATA_IS_MOCK_SERVER (self));
	g_return_if_fail (message_chunk != NULL);

	/* Silently ignore the call if logging is disabled or if a trace file hasn't been specified. */
	if (priv->enable_logging == FALSE || priv->output_stream == NULL) {
		return;
	}

	/* Append to the trace file. */
	if (g_output_stream_write_all (G_OUTPUT_STREAM (priv->output_stream), message_chunk, strlen (message_chunk), NULL, NULL, &child_error) == FALSE ||
	    g_output_stream_write_all (G_OUTPUT_STREAM (priv->output_stream), "\n", 1, NULL, NULL, &child_error) == FALSE) {
		gchar *trace_file_path = g_file_get_path (priv->trace_file);
		g_warning ("Error appending to log file ‘%s’: %s", trace_file_path, child_error->message);
		g_free (trace_file_path);

		g_error_free (child_error);

		return;
	}
}