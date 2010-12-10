/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * GData Client
 * Copyright (C) Richard Schwarting 2009 <aquarichy@gmail.com>
 * Copyright (C) Philip Withnall 2009–2010 <philip@tecnocode.co.uk>
 *
 * GData Client is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * GData Client is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GData Client.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:gdata-picasaweb-service
 * @short_description: GData PicasaWeb service object
 * @stability: Unstable
 * @include: gdata/services/picasaweb/gdata-picasaweb-service.h
 *
 * #GDataPicasaWebService is a subclass of #GDataService for communicating with the GData API of Google PicasaWeb. It supports querying for files
 * and albums, and uploading files.
 *
 * For more details of PicasaWeb's GData API, see the <ulink type="http" url="http://code.google.com/apis/picasaweb/developers_guide_protocol.html">
 * online documentation</ulink>.
 *
 * <example>
 * 	<title>Authenticating and Creating a New Album</title>
 * 	<programlisting>
 *	GDataPicasaWebService *service;
 *	GDataPicasaWebAlbum *album, *inserted_album;
 *
 *	/<!-- -->* Create a service object and authenticate with the PicasaWeb server *<!-- -->/
 *	service = gdata_picasaweb_service_new ("companyName-applicationName-versionID");
 *	gdata_service_authenticate (GDATA_SERVICE (service), username, password, NULL, NULL);
 *
 *	/<!-- -->* Create a GDataPicasaWebAlbum entry for the new album, setting some information about it *<!-- -->/
 *	album = gdata_picasaweb_album_new (NULL);
 *	gdata_entry_set_title (GDATA_ENTRY (album), "Photos from the Rhine");
 *	gdata_entry_set_summary (GDATA_ENTRY (album), "An album of our adventures on the great river.");
 *	gdata_picasaweb_album_set_location (album, "The Rhine, Germany");
 *
 *	/<!-- -->* Insert the new album on the server. Note that this is a blocking operation. *<!-- -->/
 *	inserted_album = gdata_picasaweb_service_insert_album (service, album, NULL, NULL);
 *
 *	g_object_unref (album);
 *	g_object_unref (inserted_album);
 *	g_object_unref (service);
 *	</programlisting>
 * </example>
 *
 * <example>
 * 	<title>Uploading a Photo or Video</title>
 * 	<programlisting>
 *	GDataPicasaWebFile *file_entry, *uploaded_file_entry;
 *	GDataUploadStream *upload_stream;
 *	GFile *file_data;
 *	GFileInfo *file_info;
 *	GFileInputStream *file_stream;
 *
 *	/<!-- -->* Specify the GFile image on disk to upload *<!-- -->/
 *	file_data = g_file_new_for_path (path);
 *
 *	/<!-- -->* Get the file information for the file being uploaded. If another data source was being used for the upload, it would have to
 *	 * provide an appropriate slug and content type. Note that this is a blocking operation. *<!-- -->/
 *	file_info = g_file_query_info (file_data, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
 *	                               G_FILE_QUERY_INFO_NONE, NULL, NULL);
 *
 *	/<!-- -->* Create a GDataPicasaWebFile entry for the image, setting a title and caption/summary *<!-- -->/
 *	file_entry = gdata_picasaweb_file_new (NULL);
 *	gdata_entry_set_title (GDATA_ENTRY (file_entry), "Black Cat");
 *	gdata_entry_set_summary (GDATA_ENTRY (file_entry), "Photo of the world's most beautiful cat.");
 *
 *	/<!-- -->* Create an upload stream for the file. This is non-blocking. *<!-- -->/
 *	upload_stream = gdata_picasaweb_service_upload_file (service, album, file_entry, g_file_info_get_display_name (file_info),
 *	                                                     g_file_info_get_content_type (file_info), NULL);
 *	g_object_unref (file_info);
 *	g_object_unref (file_entry);
 *
 *	/<!-- -->* Prepare a file stream for the file to be uploaded. This is a blocking operation. *<!-- -->/
 *	file_stream = g_file_read (file_data, NULL, NULL);
 *	g_object_unref (file_data);
 *
 *	/<!-- -->* Upload the file to the server. Note that this is a blocking operation. *<!-- -->/
 *	g_output_stream_splice (G_OUTPUT_STREAM (upload_stream), G_INPUT_STREAM (file_stream),
 *	                        G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE | G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET, NULL, NULL);
 *
 *	/<!-- -->* Parse the resulting updated entry. This is a non-blocking operation. *<!-- -->/
 *	uploaded_file_entry = gdata_picasaweb_service_finish_file_upload (service, upload_stream, NULL);
 *	g_object_unref (file_stream);
 *	g_object_unref (upload_stream);
 *
 *	/<!-- -->* ... *<!-- -->/
 *
 *	g_object_unref (uploaded_file_entry);
 * 	</programlisting>
 * </example>
 *
 *
 * Since: 0.4.0
 **/

#include <config.h>
#include <glib.h>
#include <glib/gi18n-lib.h>

#include "gdata-service.h"
#include "gdata-picasaweb-service.h"
#include "gdata-private.h"
#include "gdata-parser.h"
#include "atom/gdata-link.h"
#include "gdata-upload-stream.h"
#include "gdata-picasaweb-feed.h"

G_DEFINE_TYPE (GDataPicasaWebService, gdata_picasaweb_service, GDATA_TYPE_SERVICE)

static void
gdata_picasaweb_service_class_init (GDataPicasaWebServiceClass *klass)
{
	GDataServiceClass *service_class = GDATA_SERVICE_CLASS (klass);
	service_class->service_name = "lh2";
	service_class->feed_type = GDATA_TYPE_PICASAWEB_FEED;
}

static void
gdata_picasaweb_service_init (GDataPicasaWebService *self)
{
	/* Nothing to see here */
}

/**
 * gdata_picasaweb_service_new:
 * @client_id: your application's client ID
 *
 * Creates a new #GDataPicasaWebService. The @client_id must be unique for your application, and as registered with Google.
 * The <ulink type="http" url="http://code.google.com/apis/accounts/docs/AuthForInstalledApps.html#Request">recommended
 * form</ulink> is "companyName-applicationName-versionID".
 *
 * Return value: a new #GDataPicasaWebService, or %NULL
 *
 * Since: 0.4.0
 **/
GDataPicasaWebService *
gdata_picasaweb_service_new (const gchar *client_id)
{
	g_return_val_if_fail (client_id != NULL, NULL);
	return g_object_new (GDATA_TYPE_PICASAWEB_SERVICE, "client-id", client_id, NULL);
}

/*
 * create_uri:
 * @self: a #GDataPicasaWebService
 * @username: (allow-none): the username to use, or %NULL to use the currently logged in user
 * @type: the type of object to access: "entry" for a user, or "feed" for an album
 *
 * Builds a URI to use when querying for albums or a user.
 *
 * Return value: a constructed URI; free with g_free()
 *
 * Since: 0.4.0
 */
static gchar *
create_uri (GDataPicasaWebService *self, const gchar *username, const gchar *type)
{
	if (username == NULL) {
		/* Ensure we're authenticated first */
		if (gdata_service_is_authenticated (GDATA_SERVICE (self)) == FALSE)
			return NULL;

		/* Querying Picasa albums for the "default" user when logged in returns the albums for the authenticated user */
		username = "default";
	}

	return _gdata_service_build_uri (TRUE, "http://picasaweb.google.com/data/%s/api/user/%s", type, username);
}

/**
 * gdata_picasaweb_service_get_user:
 * @self: a #GDataPicasaWebService
 * @username: (allow-none): the username of the user whose information you wish to retrieve, or %NULL for the currently authenticated user.
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: a #GError, or %NULL
 *
 * Queries the service to return the user specified by @username.
 *
 * Return value: (transfer full): a #GDataPicasaWebUser; unref with g_object_unref()
 *
 * Since: 0.6.0
 **/
GDataPicasaWebUser *
gdata_picasaweb_service_get_user (GDataPicasaWebService *self, const gchar *username, GCancellable *cancellable, GError **error)
{
	gchar *uri;
	GDataParsable *user;
	SoupMessage *message;

	g_return_val_if_fail (GDATA_IS_PICASAWEB_SERVICE (self), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	uri = create_uri (self, username, "entry");
	if (uri == NULL) {
		g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED,
		                     _("You must specify a username or be authenticated to query a user."));
		return NULL;
	}

	message = _gdata_service_query (GDATA_SERVICE (self), uri, NULL, cancellable, error);
	g_free (uri);

	if (message == NULL)
		return NULL;

	g_assert (message->response_body->data != NULL);
	user = gdata_parsable_new_from_xml (GDATA_TYPE_PICASAWEB_USER, message->response_body->data, message->response_body->length, error);
	g_object_unref (message);

	return GDATA_PICASAWEB_USER (user);
}

/**
 * gdata_picasaweb_service_query_all_albums:
 * @self: a #GDataPicasaWebService
 * @query: (allow-none): a #GDataQuery with the query parameters, or %NULL
 * @username: (allow-none): the username of the user whose albums you wish to retrieve, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @progress_callback: (scope call): a #GDataQueryProgressCallback to call when an entry is loaded, or %NULL
 * @progress_user_data: (closure): data to pass to the @progress_callback function
 * @error: a #GError, or %NULL
 *
 * Queries the service to return a list of all albums belonging to the specified @username which match the given
 * @query. If a user is authenticated with the service, @username can be set as %NULL to return a list of albums belonging
 * to the currently-authenticated user.
 *
 * Note that the #GDataQuery:q query parameter cannot be set on @query for album queries.
 *
 * For more details, see gdata_service_query().
 *
 * Return value: (transfer full): a #GDataFeed of query results; unref with g_object_unref()
 *
 * Since: 0.4.0
 **/
GDataFeed *
gdata_picasaweb_service_query_all_albums (GDataPicasaWebService *self, GDataQuery *query, const gchar *username, GCancellable *cancellable,
                                          GDataQueryProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	gchar *uri;
	GDataFeed *album_feed;

	g_return_val_if_fail (GDATA_IS_PICASAWEB_SERVICE (self), NULL);
	g_return_val_if_fail (query == NULL || GDATA_IS_QUERY (query), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (query != NULL && gdata_query_get_q (query) != NULL) {
		/* Bug #593336 — Query parameter "q=..." isn't valid for album kinds */
		g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_BAD_QUERY_PARAMETER,
		                     _("Query parameter not allowed for albums."));
		return NULL;
	}

	uri = create_uri (self, username, "feed");
	if (uri == NULL) {
		g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED,
		                     _("You must specify a username or be authenticated to query all albums."));
		return NULL;
	}

	/* Execute the query */
	album_feed = gdata_service_query (GDATA_SERVICE (self), uri, query, GDATA_TYPE_PICASAWEB_ALBUM,
	                                  cancellable, progress_callback, progress_user_data, error);
	g_free (uri);

	return album_feed;
}

/**
 * gdata_picasaweb_service_query_all_albums_async: (skip)
 * @self: a #GDataPicasaWebService
 * @query: (allow-none): a #GDataQuery with the query parameters, or %NULL
 * @username: (allow-none): the username of the user whose albums you wish to retrieve, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @progress_callback: a #GDataQueryProgressCallback to call when an entry is loaded, or %NULL
 * @progress_user_data: (closure): data to pass to the @progress_callback function
 * @callback: a #GAsyncReadyCallback to call when authentication is finished
 * @user_data: (closure): data to pass to the @callback function
 *
 * Queries the service to return a list of all albums belonging to the specified @username which match the given
 * @query. @self, @query and @username are all reffed/copied when this function is called, so can safely be unreffed/freed after
 * this function returns.
 *
 * For more details, see gdata_picasaweb_service_query_all_albums(), which is the synchronous version of
 * this function, and gdata_service_query_async(), which is the base asynchronous query function.
 *
 * Since: 0.4.0
 **/
void
gdata_picasaweb_service_query_all_albums_async (GDataPicasaWebService *self, GDataQuery *query, const gchar *username,
                                                GCancellable *cancellable, GDataQueryProgressCallback progress_callback, gpointer progress_user_data,
                                                GAsyncReadyCallback callback, gpointer user_data)
{
	gchar *uri;

	g_return_if_fail (GDATA_IS_PICASAWEB_SERVICE (self));
	g_return_if_fail (query == NULL || GDATA_IS_QUERY (query));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (callback != NULL);

	if (query != NULL && gdata_query_get_q (query) != NULL) {
		/* Bug #593336 — Query parameter "q=..." isn't valid for album kinds */
		g_simple_async_report_error_in_idle (G_OBJECT (self), callback, user_data,
		                                     GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_BAD_QUERY_PARAMETER,
		                                     _("Query parameter not allowed for albums."));
		return;
	}

	uri = create_uri (self, username, "feed");
	if (uri == NULL) {
		g_simple_async_report_error_in_idle (G_OBJECT (self), callback, user_data,
		                                     GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED,
		                                     _("You must specify a username or be authenticated to query all albums."));
		return;
	}

	/* Schedule the async query */
	gdata_service_query_async (GDATA_SERVICE (self), uri, query, GDATA_TYPE_PICASAWEB_ALBUM, cancellable, progress_callback, progress_user_data,
	                           callback, user_data);
	g_free (uri);
}

static const gchar *
get_query_files_uri (GDataPicasaWebAlbum *album, GError **error)
{
	if (album != NULL) {
		GDataLink *_link = gdata_entry_look_up_link (GDATA_ENTRY (album), "http://schemas.google.com/g/2005#feed");
		if (_link == NULL) {
			/* Error */
			g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_PROTOCOL_ERROR,
			                     _("The album did not have a feed link."));
			return NULL;
		}

		return gdata_link_get_uri (_link);
	} else {
		/* Default URI */
		return "http://picasaweb.google.com/data/feed/api/user/default/albumid/default";
	}
}

/**
 * gdata_picasaweb_service_query_files:
 * @self: a #GDataPicasaWebService
 * @album: (allow-none): a #GDataPicasaWebAlbum from which to retrieve the files, or %NULL
 * @query: (allow-none): a #GDataQuery with the query parameters, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @progress_callback: (scope call): a #GDataQueryProgressCallback to call when an entry is loaded, or %NULL
 * @progress_user_data: (closure): data to pass to the @progress_callback function
 * @error: a #GError, or %NULL
 *
 * Queries the specified @album for a list of the files which match the given @query. If @album is %NULL and a user is
 * authenticated with the service, the user's default album will be queried.
 *
 * For more details, see gdata_service_query().
 *
 * Return value: (transfer full): a #GDataFeed of query results; unref with g_object_unref()
 *
 * Since: 0.4.0
 **/
GDataFeed *
gdata_picasaweb_service_query_files (GDataPicasaWebService *self, GDataPicasaWebAlbum *album, GDataQuery *query, GCancellable *cancellable,
                                     GDataQueryProgressCallback progress_callback, gpointer progress_user_data, GError **error)
{
	/* TODO: Async variant */
	const gchar *uri;

	g_return_val_if_fail (GDATA_IS_PICASAWEB_SERVICE (self), NULL);
	g_return_val_if_fail (album == NULL || GDATA_IS_PICASAWEB_ALBUM (album), NULL);
	g_return_val_if_fail (query == NULL || GDATA_IS_QUERY (query), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	uri = get_query_files_uri (album, error);
	if (uri == NULL)
		return NULL;

	/* Execute the query */
	return gdata_service_query (GDATA_SERVICE (self), uri, GDATA_QUERY (query), GDATA_TYPE_PICASAWEB_FILE, cancellable,
	                            progress_callback, progress_user_data, error);
}

/**
 * gdata_picasaweb_service_query_files_async: (skip)
 * @self: a #GDataPicasaWebService
 * @album: (allow-none): a #GDataPicasaWebAlbum from which to retrieve the files, or %NULL
 * @query: (allow-none): a #GDataQuery with the query parameters, or %NULL
 * @cancellable: optional #GCancellable object, or %NULL
 * @progress_callback: a #GDataQueryProgressCallback to call when an entry is loaded, or %NULL
 * @progress_user_data: (closure): data to pass to the @progress_callback function
 * @callback: a #GAsyncReadyCallback to call when the query is finished
 * @user_data: (closure): data to pass to the @callback function
 *
 * Queries the specified @album for a list of the files which match the given @query. If @album is %NULL and a user is authenticated with the service,
 * the user's default album will be queried. @self, @album and @query are all reffed when this function is called, so can safely be unreffed after
 * this function returns.
 *
 * For more details, see gdata_picasaweb_service_query_files(), which is the synchronous version of this function, and gdata_service_query_async(),
 * which is the base asynchronous query function.
 *
 * Since: 0.8.0
 **/
void
gdata_picasaweb_service_query_files_async (GDataPicasaWebService *self, GDataPicasaWebAlbum *album, GDataQuery *query, GCancellable *cancellable,
                                           GDataQueryProgressCallback progress_callback, gpointer progress_user_data, GAsyncReadyCallback callback,
                                           gpointer user_data)
{
	const gchar *request_uri;
	GError *child_error = NULL;

	g_return_if_fail (GDATA_IS_PICASAWEB_SERVICE (self));
	g_return_if_fail (album == NULL || GDATA_IS_PICASAWEB_ALBUM (album));
	g_return_if_fail (query == NULL || GDATA_IS_QUERY (query));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));
	g_return_if_fail (callback != NULL);

	request_uri = get_query_files_uri (album, &child_error);
	if (request_uri == NULL) {
		g_simple_async_report_gerror_in_idle (G_OBJECT (self), callback, user_data, child_error);
		g_error_free (child_error);
		return;
	}

	gdata_service_query_async (GDATA_SERVICE (self), request_uri, GDATA_QUERY (query), GDATA_TYPE_PICASAWEB_FILE, cancellable, progress_callback,
	                           progress_user_data, callback, user_data);
}

/**
 * gdata_picasaweb_service_upload_file:
 * @self: a #GDataPicasaWebService
 * @album: (allow-none): a #GDataPicasaWebAlbum into which to insert the file, or %NULL
 * @file_entry: a #GDataPicasaWebFile to insert
 * @slug: the filename to give to the uploaded file
 * @content_type: the content type of the uploaded data
 * @error: a #GError, or %NULL
 *
 * Uploads a file (photo or video) to the given PicasaWeb @album, using the metadata from @file and the file data written to the resulting
 * #GDataUploadStream. If @album is %NULL, the file will be uploaded to the currently-authenticated user's "Drop Box" album. A user must be
 * authenticated to use this function.
 *
 * If @file has already been inserted, a %GDATA_SERVICE_ERROR_ENTRY_ALREADY_INSERTED error will be returned. If no user is authenticated
 * with the service, %GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED will be returned.
 *
 * The stream returned by this function should be written to using the standard #GOutputStream methods, asychronously or synchronously. Once the stream
 * is closed (using g_output_stream_close()), gdata_picasaweb_service_finish_file_upload() should be called on it to parse and return the updated
 * #GDataPicasaWebFile for the uploaded file. This must be done, as @file_entry isn't updated in-place.
 *
 * Any upload errors will be thrown by the stream methods, and may come from the #GDataServiceError domain.
 *
 * Return value: (transfer full): a #GDataUploadStream to write the file data to, or %NULL; unref with g_object_unref()
 *
 * Since: 0.8.0
 **/
GDataUploadStream *
gdata_picasaweb_service_upload_file (GDataPicasaWebService *self, GDataPicasaWebAlbum *album, GDataPicasaWebFile *file_entry, const gchar *slug,
                                     const gchar *content_type, GError **error)
{
	const gchar *user_id = NULL, *album_id = NULL;
	GDataUploadStream *upload_stream;
	gchar *upload_uri;

	g_return_val_if_fail (GDATA_IS_PICASAWEB_SERVICE (self), NULL);
	g_return_val_if_fail (album == NULL || GDATA_IS_PICASAWEB_ALBUM (album), NULL);
	g_return_val_if_fail (GDATA_IS_PICASAWEB_FILE (file_entry), NULL);
	g_return_val_if_fail (slug != NULL && *slug != '\0', NULL);
	g_return_val_if_fail (content_type != NULL && *content_type != '\0', NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (gdata_entry_is_inserted (GDATA_ENTRY (file_entry)) == TRUE) {
		g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_ENTRY_ALREADY_INSERTED,
		                     _("The entry has already been inserted."));
		return NULL;
	}

	if (gdata_service_is_authenticated (GDATA_SERVICE (self)) == FALSE) {
		g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED,
		                     _("You must be authenticated to upload a file."));
		return NULL;
	}

	/* PicasaWeb allows you to post to a default Dropbox */
	album_id = (album != NULL) ? gdata_entry_get_id (GDATA_ENTRY (album)) : "default";
	user_id = gdata_service_get_username (GDATA_SERVICE (self));

	/* Build the upload URI and upload stream */
	upload_uri = _gdata_service_build_uri (TRUE, "http://picasaweb.google.com/data/feed/api/user/%s/albumid/%s", user_id, album_id);
	upload_stream = GDATA_UPLOAD_STREAM (gdata_upload_stream_new (GDATA_SERVICE (self), SOUP_METHOD_POST, upload_uri, GDATA_ENTRY (file_entry),
	                                                              slug, content_type));
	g_free (upload_uri);

	return upload_stream;
}

/**
 * gdata_picasaweb_service_finish_file_upload:
 * @self: a #GDataPicasaWebService
 * @upload_stream: the #GDataUploadStream from the operation
 * @error: a #GError, or %NULL
 *
 * Finish off a file upload operation started by gdata_picasaweb_service_upload_file(), parsing the result and returning the new #GDataPicasaWebFile.
 *
 * If an error occurred during the upload operation, it will have been returned during the operation (e.g. by g_output_stream_splice() or one
 * of the other stream methods). In such a case, %NULL will be returned but @error will remain unset. @error is only set in the case that the server
 * indicates that the operation was successful, but an error is encountered in parsing the result sent by the server.
 *
 * Return value: (transfer full): the new #GDataPicasaWebFile, or %NULL; unref with g_object_unref()
 *
 * Since: 0.8.0
 */
GDataPicasaWebFile *
gdata_picasaweb_service_finish_file_upload (GDataPicasaWebService *self, GDataUploadStream *upload_stream, GError **error)
{
	const gchar *response_body;
	gssize response_length;

	g_return_val_if_fail (GDATA_IS_PICASAWEB_SERVICE (self), NULL);
	g_return_val_if_fail (GDATA_IS_UPLOAD_STREAM (upload_stream), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* Get the response from the server */
	response_body = gdata_upload_stream_get_response (upload_stream, &response_length);
	if (response_body == NULL || response_length == 0)
		return NULL;

	/* Parse the response to produce a GDataPicasaWebFile */
	return GDATA_PICASAWEB_FILE (gdata_parsable_new_from_xml (GDATA_TYPE_PICASAWEB_FILE, response_body, (gint) response_length, error));
}

/**
 * gdata_picasaweb_service_insert_album:
 * @self: a #GDataPicasaWebService
 * @album: a #GDataPicasaWebAlbum to create on the server
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: a #GError, or %NULL
 *
 * Inserts a new album described by @album. A user must be
 * authenticated to use this function.
 *
 * Return value: (transfer full): the inserted #GDataPicasaWebAlbum; unref with
 * g_object_unref()
 *
 * Since: 0.6.0
 **/
GDataPicasaWebAlbum *
gdata_picasaweb_service_insert_album (GDataPicasaWebService *self, GDataPicasaWebAlbum *album, GCancellable *cancellable, GError **error)
{
	g_return_val_if_fail (GDATA_IS_PICASAWEB_SERVICE (self), NULL);
	g_return_val_if_fail (GDATA_IS_PICASAWEB_ALBUM (album), NULL);
	g_return_val_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	if (gdata_entry_is_inserted (GDATA_ENTRY (album)) == TRUE) {
		g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_ENTRY_ALREADY_INSERTED,
		                     _("The album has already been inserted."));
		return NULL;
	}

	if (gdata_service_is_authenticated (GDATA_SERVICE (self)) == FALSE) {
		g_set_error_literal (error, GDATA_SERVICE_ERROR, GDATA_SERVICE_ERROR_AUTHENTICATION_REQUIRED,
		                     _("You must be authenticated to insert an album."));
		return NULL;
	}

	return GDATA_PICASAWEB_ALBUM (gdata_service_insert_entry (GDATA_SERVICE (self), "http://picasaweb.google.com/data/feed/api/user/default",
	                                                          GDATA_ENTRY (album), cancellable, error));
}

/**
 * gdata_picasaweb_service_insert_album_async:
 * @self: a #GDataPicasaWebService
 * @album: a #GDataPicasaWebAlbum to create on the server
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when insertion is finished
 * @user_data: (closure): data to pass to the @callback function
 *
 * Inserts a new album described by @album. The user must be authenticated to use this function. @self and @album are both reffed when this function
 * is called, so can safely be unreffed after this function returns.
 *
 * @callback should call gdata_service_insert_entry_finish() to obtain a #GDataPicasaWebAlbum representing the inserted album and to check for
 * possible errors.
 *
 * For more details, see gdata_picasaweb_service_insert_album(), which is the synchronous version of this function, and
 * gdata_service_insert_entry_async(), which is the base asynchronous insertion function.
 *
 * Since: 0.8.0
 **/
void
gdata_picasaweb_service_insert_album_async (GDataPicasaWebService *self, GDataPicasaWebAlbum *album, GCancellable *cancellable,
                                            GAsyncReadyCallback callback, gpointer user_data)
{
	g_return_if_fail (GDATA_IS_PICASAWEB_SERVICE (self));
	g_return_if_fail (GDATA_IS_PICASAWEB_ALBUM (album));
	g_return_if_fail (cancellable == NULL || G_IS_CANCELLABLE (cancellable));

	gdata_service_insert_entry_async (GDATA_SERVICE (self), "http://picasaweb.google.com/data/feed/api/user/default", GDATA_ENTRY (album),
	                                  cancellable, callback, user_data);
}
