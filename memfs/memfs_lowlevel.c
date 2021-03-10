/*
 * Copyright (c) 2018      Manu Mathew
 * Copyright (c) 2016-2017 Bharath Kumar Reddy Vangoor
 * Copyright (c) 2017      Swaminathan Sivaraman
 * Copyright (c) 2016-2018 Vasily Tarasov
 * Copyright (c) 2016-2018 Erez Zadok
 * Copyright (c) 2016-2018 Stony Brook University
 * Copyright (c) 2016-2018 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define FUSE_USE_VERSION 31

#include <fuse_lowlevel.h>
#include <fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

/* 1024 * 1024 */
#define FILE_LEN 10485760

struct fuse_conn_info_opts *fuse_conn_info_opts_ptr;

struct memfs_info
{
	char *filename;
} memfs_info;

/*
 * arg passing example
 * https://libfuse.github.io/doxygen/hello_8c.html
 */

#define MEMFS_OPT(t, p)                      \
	{                                        \
		t, offsetof(struct memfs_info, p), 1 \
	}

static const struct fuse_opt memfs_opts[] = {
	MEMFS_OPT("--filename=%s", filename),
	FUSE_OPT_END};

static char *filecontent;

void init_buff()
{
	filecontent = (char *)malloc(FILE_LEN);
	memset(filecontent, 0, FILE_LEN);
}

static int mem_stat(fuse_ino_t ino, struct stat *stbuf)
{
	stbuf->st_ino = ino;
	switch (ino)
	{
	case 1:
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		break;

	case 2:
		//stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_mode = S_IFREG | 0777;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(filecontent);
		//stbuf->st_size = FILE_LEN;
		break;

	default:
		return -1;
	}
	return 0;
}

static void mem_ll_getattr(fuse_req_t req, fuse_ino_t ino,
						   struct fuse_file_info *fi)
{
	struct stat stbuf;

	(void)fi;

	memset(&stbuf, 0, sizeof(stbuf));
	if (mem_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void mem_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e;

	if (parent != 1 || strcmp(name, memfs_info.filename) != 0)

		fuse_reply_err(req, ENOENT);
	else
	{
		memset(&e, 0, sizeof(e));
		e.ino = 2;
		e.attr_timeout = 1.0;
		e.entry_timeout = 1.0;
		mem_stat(e.ino, &e.attr);

		fuse_reply_entry(req, &e);
	}
}

struct dirbuf
{
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
					   fuse_ino_t ino)
{
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *)realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
					  b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
							 off_t off, size_t maxsize)
{
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
							  min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static void mem_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
						   off_t off, struct fuse_file_info *fi)
{
	(void)fi;

	if (ino != 1)
		fuse_reply_err(req, ENOTDIR);
	else
	{
		struct dirbuf b;

		memset(&b, 0, sizeof(b));
		dirbuf_add(req, &b, ".", 1);
		dirbuf_add(req, &b, "..", 1);
		dirbuf_add(req, &b, memfs_info.filename, 2);
		reply_buf_limited(req, b.p, b.size, off, size);
		free(b.p);
	}
}

static void mem_ll_open(fuse_req_t req, fuse_ino_t ino,
						struct fuse_file_info *fi)
{
	if (ino != 2)
		fuse_reply_err(req, EISDIR);
	//	else if ((fi->flags & 3) != O_RDONLY)
	//		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static void mem_ll_write(fuse_req_t req, fuse_ino_t ino, const char *buf,
						 size_t size, off_t off, struct fuse_file_info *fi)
{
	printf("trying to write to offset [%d] size [%d]\n", (int)off, (int)size);
	(void)ino;

	if (off >= FILE_LEN)
		return (void)fuse_reply_err(req, errno);

	if (off + size > FILE_LEN)
		size = FILE_LEN - off;

	memcpy(filecontent + off, buf, size);
	fuse_reply_write(req, size);
}

static void mem_ll_init(void *userdata,
						struct fuse_conn_info *conn)
{
	/*
     * fuse_session_new() no longer accepts arguments
     * command line options can only be set using fuse_apply_conn_info_opts().
     */
	fuse_apply_conn_info_opts(fuse_conn_info_opts_ptr, conn);
}

static void mem_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
						off_t off, struct fuse_file_info *fi)
{
	(void)fi;
	char *buf;

	assert(ino == 2);
	buf = (char *)malloc(size);
	size_t len = strlen(filecontent);

	if (off >= len)
	{
		free(buf);
		return (void)fuse_reply_buf(req, NULL, 0);
	}

	if (off + size > len)
		size = len - off;
	memcpy(buf, filecontent + off, size);
	fuse_reply_buf(req, buf, size);
	free(buf);
}

static struct fuse_lowlevel_ops mem_ll_oper = {
	.init = mem_ll_init,
	.lookup = mem_ll_lookup,
	.getattr = mem_ll_getattr,
	.readdir = mem_ll_readdir,
	.open = mem_ll_open,
	.read = mem_ll_read,
	.write = mem_ll_write,
};

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	int ret = -1;

	//struct memfs_info m_info;
	/* default value of the filename */
	memfs_info.filename = strdup("00000001");

	/* accept options like -o writeback_cache */
	fuse_conn_info_opts_ptr = fuse_parse_conn_info_opts(&args);

	if (fuse_opt_parse(&args, &memfs_info, memfs_opts, NULL) == -1)
	{
		return 1;
	}
	init_buff();

	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help)
	{
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		printf("[options]: --filename=<filename>\n");
		printf("Example :   ./memfs --filename=testfile /mnt/tmp\n\n");
		fuse_cmdline_help();
		fuse_lowlevel_help();
		ret = 0;
		goto err_out1;
	}
	else if (opts.show_version)
	{
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	se = fuse_session_new(&args, &mem_ll_oper,
						  sizeof(mem_ll_oper), NULL);
	if (se == NULL)
		goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
		goto err_out2;

	if (fuse_session_mount(se, opts.mountpoint) != 0)
		goto err_out3;

	fuse_daemonize(opts.foreground);

	/* Block until ctrl+c or fusermount -u */
	printf("singlethread: %d\n", opts.singlethread);
	if (opts.singlethread)
		ret = fuse_session_loop(se);
	else
		ret = fuse_session_loop_mt(se, opts.clone_fd);

	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	if (filecontent)
		free(filecontent);

	return ret ? 1 : 0;
}
