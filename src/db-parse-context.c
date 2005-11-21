/*
 *  Copyright (C) 2005 Christophe Fergeau
 *
 * 
 *  The code contained in this file is free software; you can redistribute
 *  it and/or modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either version
 *  2.1 of the License, or (at your option) any later version.
 *  
 *  This file is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *  
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this code; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 * 
 *  iTunes and iPod are trademarks of Apple
 * 
 *  This product is not supported/written/published by Apple!
 *
 */


#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include "glib-compat.h"
#include "db-parse-context.h"
#include "db-itunes-parser.h"

DBParseContext *
db_parse_context_new (const unsigned char *buffer, off_t len)
{
	DBParseContext *result;

	result = g_new0 (DBParseContext, 1);
	if (result == NULL) {
		return NULL;
	}

	result->buffer = buffer;
	result->cur_pos = buffer;
	result->total_len = len;

	return result;
}

void
db_parse_context_destroy (DBParseContext *ctx, gboolean unmap) 
{
	g_return_if_fail (ctx != NULL);

	if (unmap) {
		munmap ((void*)ctx->buffer, ctx->total_len);
	}
	g_free (ctx);
}

static void
db_parse_context_set_header_len (DBParseContext *ctx, off_t len)
{
	/* FIXME: this can probably happen in malformed itunesdb files, 
	 * don't g_assert on this, only output a warning 
	 */
	g_assert ((ctx->cur_pos - ctx->buffer) <= len);
	g_assert (len <= ctx->total_len);
	ctx->header_len = len;
}

void
db_parse_context_set_total_len (DBParseContext *ctx, off_t len)
{
	/* FIXME: this can probably happen in malformed itunesdb files, 
	 * don't g_assert on this, only output a warning 
	 */
	g_assert ((ctx->cur_pos - ctx->buffer) <= len);
	if (ctx->header_len != 0) {
		g_assert (len >= ctx->header_len);
	}
	ctx->total_len = len;
}


off_t
db_parse_context_get_remaining_length (DBParseContext *ctx)
{
	if (ctx->header_len != 0) {
		return ctx->header_len - (ctx->cur_pos - ctx->buffer); 
	} else {
		return ctx->total_len - (ctx->cur_pos - ctx->buffer); 
	}
}

DBParseContext *
db_parse_context_get_sub_context (DBParseContext *ctx, off_t offset)
{
	if (offset >= ctx->total_len) {
		return NULL;
	}
	return db_parse_context_new (&ctx->buffer[offset], 
				     ctx->total_len - offset);
}


DBParseContext *
db_parse_context_get_next_child (DBParseContext *ctx)
{
	if (ctx->header_len == 0) {
		return NULL;
	}
	if (ctx->header_len >= ctx->total_len) {
		return NULL;
	}

	return db_parse_context_get_sub_context (ctx, ctx->header_len);
}

void *
db_parse_context_get_m_header_internal (DBParseContext *ctx, const char *id, off_t size) 
{
	MHeader *h;

	if (db_parse_context_get_remaining_length (ctx) < 8) {
		return NULL;
	}

	h = (MHeader *)ctx->cur_pos;
	if (strncmp (id, (char *)h->header_id, 4) != 0) {
		return NULL;
	}

	/* FIXME: this test sucks for compat: if a field is smaller than 
	 * expected, we probably should create a buffer of the appropriate 
	 * size inited to 0, copy the data that is available in it and use
	 * that buffer in the rest of the code (maybe it's harmful to have
	 * some fields at 0 in some headers though...)
	 */
	if (GINT_FROM_LE (h->header_len) < size) {
		return NULL;
	}

	db_parse_context_set_header_len (ctx, GINT_FROM_LE (h->header_len));

	return h;
}

DBParseContext *
db_parse_context_new_from_file (const char *filename)
{
	int fd;
	struct stat stat_buf;
	int result;
	unsigned char *buffer;
	DBParseContext *ctx;

	buffer = NULL;
	ctx = NULL;

	fd = open (filename, O_RDONLY);
	if (fd == -1) {
		g_print ("Failed to open %s: %s\n", 
			 filename, strerror (errno));
		return NULL;
	}

	result = fstat (fd, &stat_buf);
	if (result == -1) {
		g_print ("Failed to read %s size: %s\n", 
			 filename, strerror (errno));
		goto error;
	}

	if (!S_ISREG (stat_buf.st_mode)) {
		g_print ("%s is not a regular file\n", filename);
		goto error;
	}

	if (stat_buf.st_size > ITUNESDB_MAX_SIZE) {
		g_print ("%s is too big to be an buffer file\n", filename);
		goto error;
	}

	buffer = mmap (NULL, stat_buf.st_size, PROT_READ, MAP_SHARED, fd, 0);

	if (buffer == MAP_FAILED) {
		g_print ("Error while mmap'ing %s: %s\n", 
			 filename, strerror (errno));
		goto error;
	}

	ctx = db_parse_context_new (buffer, stat_buf.st_size);
	if (ctx == NULL) {
		munmap (buffer, stat_buf.st_size);
	}
 error:
	close (fd);
	return ctx;
}
