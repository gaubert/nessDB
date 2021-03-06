/*
 * nessDB storage engine
 * Copyright (c) 2011-2012, BohuTANG <overred.shuttler at gmail dot com>
 * All rights reserved.
 * Code is licensed with BSD. See COPYING.BSD file.
 *
 */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>

#include "buffer.h"
#include "log.h"
#include "debug.h"
#include "xmalloc.h"

#define DB_MAGIC (2011)

int _file_exists(const char *path)
{
	int fd = n_open(path, O_RDWR);

	if (fd > 0) {
		close(fd);
		return 1;
	}

	return 0;
}

struct log *log_new(const char *basedir, int islog)
{
	int result;
	struct log *l;
	char log_name[FILE_PATH_SIZE];
	char db_name[FILE_PATH_SIZE];

	l = xcalloc(1, sizeof(struct log));
	l->islog = islog;

	memset(log_name, 0, FILE_PATH_SIZE);
	snprintf(log_name, FILE_PATH_SIZE, "%s/%d.log", basedir, 0);
	memcpy(l->name, log_name, FILE_PATH_SIZE);

	memset(l->basedir, 0, FILE_PATH_SIZE);
	memcpy(l->basedir, basedir, FILE_PATH_SIZE);

	memset(db_name, 0, FILE_PATH_SIZE);
	snprintf(db_name, FILE_PATH_SIZE, "%s/%s", basedir, DB_NAME);

	if (_file_exists(db_name)) {
		l->db_wfd = n_open(db_name, LSM_OPEN_FLAGS, 0644);
		if (l->db_wfd == -1)
			__PANIC("open db error");

		l->db_alloc = n_lseek(l->db_wfd, 0, SEEK_END);
	} else {
		int magic = DB_MAGIC;

		l->db_wfd = n_open(db_name, LSM_CREAT_FLAGS, 0644);
		if (l->db_wfd == -1)
			__PANIC("create db error");

		result = write(l->db_wfd, &magic, sizeof(int));
		if (result == -1) 
			__PANIC("write magic error");

		l->db_alloc = sizeof(int);
	}

	l->buf = buffer_new(LOG_BUFFER_SIZE);
	l->db_buf = buffer_new(LOG_BUFFER_SIZE);

	return l;
}

int _log_read(char *logname, struct skiplist *list)
{
	int rem, count = 0, del_count = 0;
	int fd, size;

	fd = open(logname, O_RDWR, 0644);

	if (fd == -1) {
		__ERROR("open log error when log read, file:<%s>", logname);
		return 0;
	}
	
	size = lseek(fd, 0, SEEK_END);
	if (size == 0) {
		__WARN("log is NULL,file:<%s>", logname);
		return 0;
	}

	rem = size;

	if (lseek(fd, 0, SEEK_SET) == -1) {
		__ERROR("seek begin when log read,file:<%s>", logname);
		return 0;
	}

	while(rem > 0) {
		int isize = 0;
		int klen = 0;
		uint64_t off = 0UL;
		short opt = 0;
		char key[NESSDB_MAX_KEY_SIZE];

		if (read(fd, &klen, sizeof klen) != sizeof klen) {
			__ERROR("read klen error, log#%s", logname);
			return -1;
		}
		isize += sizeof klen;
		
		/* read key */
		memset(key, 0, NESSDB_MAX_KEY_SIZE);
		if (read(fd, &key, klen) != klen) {
			__ERROR("error when read key, log#%s", logname);
			return -1;
		}

		isize += klen;

		/* read data offset */
		if (read(fd, &off, sizeof off) != sizeof off) {
			__ERROR("read error when read data offset, log#%s", logname);
			return -1;
		}

		isize += sizeof off;

		/* read opteration */
		if (read(fd, &opt, sizeof opt) != sizeof opt) {
			__ERROR("read error when read opteration, log#%s", logname);
			return -1;
		}

		isize += sizeof opt;
		if (opt == 1) {
			count++;
			skiplist_insert(list, key, off, ADD);
		} else {
			del_count++;
			skiplist_insert(list, key, off, DEL);
		}

		rem -= isize;
	}

	__DEBUG("recovery count ADD#%d, DEL#%d", count, del_count);
	return 1;
}

int log_recovery(struct log *l, struct skiplist *list)
{
	DIR *dd;
	int ret = 0;
	int flag = 0;
	char new_log[FILE_PATH_SIZE];
	char old_log[FILE_PATH_SIZE];
	struct dirent *de;

	if (!l->islog)
		return 0;

	memset(new_log, 0, FILE_PATH_SIZE);
	memset(old_log, 0, FILE_PATH_SIZE);

	dd = opendir(l->basedir);
	while ((de = readdir(dd))) {
		char *p = strstr(de->d_name, ".log");
		if (p) {
			if (flag == 0) {
				memcpy(new_log, de->d_name, FILE_PATH_SIZE);
				flag |= 0x01;
			} else {
				memcpy(old_log, de->d_name, FILE_PATH_SIZE);
				flag |= 0x10;
			}
		}
	}
	closedir(dd);

	/* 
	 * Get the two log files:new and old 
	 * Read must be sequential,read old then read new
	 */
	if ((flag & 0x01) == 0x01) {
		memset(l->log_new, 0, FILE_PATH_SIZE);
		snprintf(l->log_new, FILE_PATH_SIZE, "%s/%s", l->basedir, new_log);

		__DEBUG("prepare to recovery from new log#%s", l->log_new);

		ret = _log_read(l->log_new, list);
		if (ret == 0)
			return ret;
	}

	if ((flag & 0x10) == 0x10) {
		memset(l->log_old, 0, FILE_PATH_SIZE);
		snprintf(l->log_old, FILE_PATH_SIZE, "%s/%s", l->basedir, old_log);

		__DEBUG("prepare to recovery from old log#%s", l->log_old);

		ret = _log_read(l->log_old, list);
		if (ret == 0)
			return ret;
	}

	return ret;
}

uint64_t log_append(struct log *l, struct compact *cpt, struct slice *sk, struct slice *sv)
{
	int len;
	int db_len;
	int write_len;
	char *line;
	char *db_line;
	struct buffer *buf = l->buf;
	struct buffer *db_buf = l->db_buf;
	uint64_t db_offset = l->db_alloc;
	uint64_t hole_offset = 0UL;

	/* DB write */
	if (sv) {
		buffer_putint(db_buf, sv->len);
		buffer_putshort(db_buf, crc16(sv->data, sv->len));
		buffer_putnstr(db_buf, sv->data, sv->len);
		db_len = db_buf->NUL;
		db_line = buffer_detach(db_buf);

		hole_offset = cpt_get(cpt, sv->len);
		if (hole_offset > 0) {
			db_offset = hole_offset;
			if (lseek(l->db_wfd, db_offset, SEEK_SET) == -1)
				hole_offset = 0;
		} else 
			l->db_alloc += db_len;

		write_len = write(l->db_wfd, db_line, db_len);
		if (write_len != db_len) {
			__ERROR("value aof error when write, length:<%d>, write_len:<%d>", db_len, write_len);
			return db_offset;
		}

		/* reset seek postion to end */
		if (hole_offset > 0) {
			lseek(l->db_wfd, l->db_alloc, SEEK_SET);
		}

	} else
		db_offset = 0UL;

	/* LOG write */
	if (l->islog) {
		buffer_putint(buf, sk->len);
		buffer_putnstr(buf, sk->data, sk->len);
		buffer_putlong(buf, db_offset);

		if(sv)
			buffer_putshort(buf, 1);
		else
			buffer_putshort(buf, 0);

		len = buf->NUL;
		line = buffer_detach(buf);

		write_len = write(l->log_wfd, line, len);
		if (write_len != len)
			__ERROR("log aof error, buffer is:%s,buffer length:<%d>, write_len:<%d>"
					, line
					, len
					, write_len);
	}

	return db_offset;
}

void log_remove(struct log *l, int lsn)
{
	char log_name[FILE_PATH_SIZE];

	memset(log_name, 0 ,FILE_PATH_SIZE);
	snprintf(log_name, FILE_PATH_SIZE, "%s/%d.log", l->basedir, lsn);
	int ret = remove(log_name);
	if (ret != 0) {
		__ERROR("remove log error, log#%s", log_name);
	}
}

void log_next(struct log *l, int lsn)
{
	char log_name[FILE_PATH_SIZE];

	memset(log_name, 0 ,FILE_PATH_SIZE);
	snprintf(log_name, FILE_PATH_SIZE, "%s/%d.log", l->basedir, lsn);
	memcpy(l->name, log_name, FILE_PATH_SIZE);

	buffer_clear(l->buf);
	buffer_clear(l->db_buf);

	if (l->log_wfd > 0)
		close(l->log_wfd);

	l->log_wfd = open(l->name, LSM_CREAT_FLAGS, 0644);

	if (l->log_wfd == -1)
		__ERROR("create new log error, log#%s", l->name);
}

void log_free(struct log *l)
{
	if (l) {
		buffer_free(l->buf);
		buffer_free(l->db_buf);

		if (l->log_wfd > 0)
			close(l->log_wfd);

		free(l);
	}
}
