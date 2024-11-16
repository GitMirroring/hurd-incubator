/* GNU tar files parsing.
   Copyright (C) 1995, 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
   2006, 2007 Free Software Foundation, Inc.
   
   Written by: 1995 Jakub Jelinek
   Rewritten by: 1998 Pavel Machek
   Modified by: 2002 Ludovic Courtes (for the Hurd tarfs)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License
   as published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <error.h>
#include <time.h>
#include <string.h>
#include <hurd/netfs.h>
#include <hurd/store.h>

#include "tar.h"
#include "names.h"

#include "debug.h"

/* A hook which is called each time a header has been parsed. */
int (*tar_header_hook) (tar_record_t *, struct archive *) = NULL;


#define	isodigit(c)	( ((c) >= '0') && ((c) <= '7') )

#ifndef isspace
# define isspace(c)      ( (c) == ' ' )
#endif

/* Taken from glibc. */
char *
strconcat (const char *string1, ...)
{
  size_t l;
  va_list args;
  char *s;
  char *concat;
  char *ptr;

  if (!string1)
    return NULL;

  l = 1 + strlen (string1);
  va_start (args, string1);
  s = va_arg (args, char *);
  while (s)
    {
      l += strlen (s);
      s = va_arg (args, char *);
    }
  va_end (args);

  concat = malloc (l);
  ptr = concat;

  ptr = stpcpy (ptr, string1);
  va_start (args, string1);
  s = va_arg (args, char *);
  while (s)
    {
      ptr = stpcpy (ptr, s);
      s = va_arg (args, char *);
    }
  va_end (args);

  return concat;
}

/*
 * Quick and dirty octal conversion.
 *
 * Result is -1 if the field is invalid (all blank, or nonoctal).
 */
static long tar_from_oct (int digs, char *where)
{
    register long value;

    while (isspace ((unsigned char) *where)) {	/* Skip spaces */
	where++;
	if (--digs <= 0)
	    return -1;		/* All blank field */
    }
    value = 0;
    while (digs > 0 && isodigit (*where)) {	/* Scan till nonoctal */
	value = (value << 3) | (*where++ - '0');
	--digs;
    }

    if (digs > 0 && *where && !isspace ((unsigned char) *where))
	return -1;		/* Ended on non-space/nul */

    return value;
}


static union record *
tar_get_next_record (struct archive *archive)
{
    error_t err;
    void *buf = mmap(NULL, RECORDSIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    size_t n = RECORDSIZE;

    err = store_read (archive->tar_file, archive->current_tar_position, RECORDSIZE, &buf, &n);
    if (err)
      error (1, err, "Read error (offset=%lli)", archive->current_tar_position);
    if (n != RECORDSIZE)
      return NULL;		/* An error has occurred */

    if (buf != archive->rec_buf.charptr)
      {
	memcpy (archive->rec_buf.charptr, buf, n);
	munmap (buf, n);
      }

    archive->current_tar_position += RECORDSIZE;

    return &(archive->rec_buf);
}

static void tar_skip_n_records (struct archive *archive, int n)
{
    (void) archive;
    archive->current_tar_position += n * RECORDSIZE;
}

typedef enum
{
  STATUS_BADCHECKSUM,
  STATUS_SUCCESS,
  STATUS_EOFMARK,
  STATUS_EOF,
}
ReadStatus;
/*
 * Return 1 for success, 0 if the checksum is bad, EOF on eof,
 * 2 for a record full of zeros (EOF marker).
 *
 */
static ReadStatus
tar_read_header (struct archive *archive, size_t *h_size)
{
    register int i;
    register long sum, signed_sum, recsum;
    register char *p;
    register union record *header;
    static char *next_long_name = NULL, *next_long_link = NULL;

  recurse:

    header = tar_get_next_record (archive);
    if (NULL == header)
	return STATUS_EOF;

    recsum = tar_from_oct (8, header->header.chksum);

    sum = 0;
    signed_sum = 0;
    p = header->charptr;
    for (i = sizeof (*header); --i >= 0;) {
	/*
	 * We can't use unsigned char here because of old compilers,
	 * e.g. V7.
	 */
	signed_sum += *p;
	sum += 0xFF & *p++;
    }

    /* Adjust checksum to count the "chksum" field as blanks. */
    for (i = sizeof (header->header.chksum); --i >= 0;) {
	sum -= 0xFF & header->header.chksum[i];
	signed_sum -= (char) header->header.chksum[i];
    }
    sum += ' ' * sizeof header->header.chksum;
    signed_sum += ' ' * sizeof header->header.chksum;

    /*
     * This is a zeroed record...whole record is 0's except
     * for the 8 blanks we faked for the checksum field.
     */
    if (sum == 8 * ' ')
	return STATUS_EOFMARK;

    if (sum != recsum && signed_sum != recsum)
	return STATUS_BADCHECKSUM;

    /*
     * Try to determine the archive format.
     */
    if (archive->type == TAR_UNKNOWN) {
	if (!strcmp (header->header.magic, TMAGIC)) {
	    if (header->header.linkflag == LF_GLOBAL_EXTHDR)
		archive->type = TAR_POSIX;
	    else
		archive->type = TAR_USTAR;
	} else if (!strcmp (header->header.magic, OLDGNU_MAGIC)) {
	    archive->type = TAR_GNU;
	}
    }

    /*
     * linkflag on BSDI tar (pax) always '\000'
     */
    if (header->header.linkflag == '\000') {
	if (header->header.arch_name[NAMSIZ - 1] != '\0')
	    i = NAMSIZ;
	else
	    i = strlen (header->header.arch_name);

	if (i && header->header.arch_name[i - 1] == '/')
	    header->header.linkflag = LF_DIR;
    }

    /*
     * Good record.  Decode file size and return.
     */
    if (header->header.linkflag == LF_LINK
	|| header->header.linkflag == LF_DIR)
	*h_size = 0;		/* Links 0 size on tape */
    else
	*h_size = tar_from_oct (1 + 12, header->header.size);

    /*
     * Skip over directory snapshot info records that
     * are stored in incremental tar archives.
     */
    if (header->header.linkflag == LF_DUMPDIR) {
	if (archive->type == TAR_UNKNOWN)
	    archive->type = TAR_GNU;
	return STATUS_SUCCESS;
    }

    /*
     * Skip over pax extended header and global extended
     * header records.
     */
    if (header->header.linkflag == LF_EXTHDR ||
	header->header.linkflag == LF_GLOBAL_EXTHDR) {
	if (archive->type == TAR_UNKNOWN)
	    archive->type = TAR_POSIX;
	return STATUS_SUCCESS;
    }

    if (header->header.linkflag == LF_LONGNAME
	|| header->header.linkflag == LF_LONGLINK) {
	char **longp;
	char *bp, *data;
	int size, written;

	if (archive->type == TAR_UNKNOWN)
	    archive->type = TAR_GNU;

	longp = ((header->header.linkflag == LF_LONGNAME)
		 ? &next_long_name : &next_long_link);

	free (*longp);
	bp = *longp = malloc (*h_size + 1);

	for (size = *h_size; size > 0; size -= written) {
	    data = tar_get_next_record (archive)->charptr;
	    if (data == NULL) {
		free (*longp);
		*longp = NULL;
		error (0, 0, "Unexpected EOF on archive file");
		return STATUS_BADCHECKSUM;
	    }
	    written = RECORDSIZE;
	    if (written > size)
		written = size;

	    memcpy (bp, data, written);
	    bp += written;
	}

	*bp = 0;
	goto recurse;
    } else {
	long data_position;
	char *q;
	int len;
	char *current_file_name, *current_link_name;

	current_link_name =
	    (next_long_link ? next_long_link :
	     strndup (header->header.arch_linkname, NAMSIZ));
	len = strlen (current_link_name);
	if (len > 1 && current_link_name[len - 1] == '/')
	    current_link_name[len - 1] = 0;

	current_file_name = NULL;
	switch (archive->type) {
	case TAR_USTAR:
	case TAR_POSIX:
	    /* The ustar archive format supports pathnames of upto 256
	     * characters in length. This is achieved by concatenating
	     * the contents of the `prefix' and `arch_name' fields like
	     * this:
	     *
	     *   prefix + path_separator + arch_name
	     *
	     * If the `prefix' field contains an empty string i.e. its
	     * first characters is '\0' the prefix field is ignored.
	     */
	    if (header->header.unused.prefix[0] != '\0') {
		char *temp_name, *temp_prefix;

		temp_name = strndup (header->header.arch_name, NAMSIZ);
		temp_prefix  = strndup (header->header.unused.prefix,
					PREFIX_SIZE);
		current_file_name = strconcat (temp_prefix, "/",
					       temp_name, (char *) NULL);
		free (temp_name);
		free (temp_prefix);
	    }
	    break;
	case TAR_GNU:
	    if (next_long_name != NULL)
		current_file_name = next_long_name;
	    break;
	default:
	    break;
	}

	if (current_file_name == NULL)
	    current_file_name = strndup (header->header.arch_name, NAMSIZ);

	len = strlen (current_file_name);

	data_position = archive->current_tar_position;

	p = strrchr (current_file_name, '/');
	if (p == NULL) {
	    p = current_file_name;
	    q = current_file_name + len;	/* "" */
	} else {
	    *(p++) = 0;
	    q = current_file_name;
	}

	if (tar_header_hook)
	  tar_header_hook (header, archive);

	free (current_file_name);

	/*    done:*/
	next_long_link = next_long_name = NULL;

	if (archive->type == TAR_GNU &&
	    header->header.unused.oldgnu.isextended) {
	    while (tar_get_next_record (archive)->ext_hdr.
		   isextended);
	}
	return STATUS_SUCCESS;
    }
}

void
tar_fill_stat (struct archive *archive, struct stat *st, tar_record_t *header)
{
    st->st_mode = tar_from_oct (8, header->header.mode);

    /* Adjust st->st_mode because there are tar-files with
     * linkflag==LF_SYMLINK and S_ISLNK(mod)==0. I don't 
     * know about the other modes but I think I cause no new
     * problem when I adjust them, too. -- Norbert.
     */
    if (header->header.linkflag == LF_DIR) {
	st->st_mode |= S_IFDIR;
    } else if (header->header.linkflag == LF_SYMLINK) {
	st->st_mode |= S_IFLNK;
    } else if (header->header.linkflag == LF_CHR) {
	st->st_mode |= S_IFCHR;
    } else if (header->header.linkflag == LF_BLK) {
	st->st_mode |= S_IFBLK;
    } else if (header->header.linkflag == LF_FIFO) {
	st->st_mode |= S_IFIFO;
    } else
	st->st_mode |= S_IFREG;

    st->st_rdev = 0;
    switch (archive->type) {
    case TAR_USTAR:
    case TAR_POSIX:
    case TAR_GNU:
	st->st_uid =
	    *header->header.uname ? finduid (header->header.
						 uname) : tar_from_oct (8,
									header->
									header.
									uid);
	st->st_gid =
	    *header->header.gname ? findgid (header->header.
						 gname) : tar_from_oct (8,
									header->
									header.
									gid);
	switch (header->header.linkflag) {
	case LF_BLK:
	case LF_CHR:
	    st->st_rdev =
		(tar_from_oct (8, header->header.devmajor) << 8) |
		tar_from_oct (8, header->header.devminor);
	}
    default:
	st->st_uid = tar_from_oct (8, header->header.uid);
	st->st_gid = tar_from_oct (8, header->header.gid);
    }
    st->st_size = tar_from_oct (1 + 12, header->header.size);
    st->st_mtime = tar_from_oct (1 + 12, header->header.mtime);
    st->st_atime = 0;
    st->st_ctime = 0;
    if (archive->type == TAR_GNU) {
	st->st_atime = tar_from_oct (1 + 12,
				     header->header.unused.oldgnu.atime);
	st->st_ctime = tar_from_oct (1 + 12,
				     header->header.unused.oldgnu.ctime);
    }
}

/*
 * Main loop for reading an archive.
 * Returns 0 on success, -1 on error.
 */
int
tar_open_archive (struct store *tar_file)
{
    /* Initial status at start of archive */
    ReadStatus status = STATUS_EOFMARK;
    ReadStatus prev_status;

    struct archive archive;
    archive.tar_file = tar_file;
    archive.current_tar_position = 0;
    archive.type = TAR_UNKNOWN;

    for (;;) {
	size_t h_size;

	prev_status = status;
	status = tar_read_header (&archive, &h_size);

	switch (status) {

	case STATUS_SUCCESS:
	    tar_skip_n_records (&archive,
				(h_size + RECORDSIZE -
				 1) / RECORDSIZE);
	    continue;

	    /*
	     * Invalid header:
	     *
	     * If the previous header was good, tell them
	     * that we are skipping bad ones.
	     */
	case STATUS_BADCHECKSUM:
	    switch (prev_status) {

		/* Error on first record */
	    case STATUS_EOFMARK:
		/* FALL THRU */

		/* Error after header rec */
	    case STATUS_SUCCESS:
		/* Error after error */

	    case STATUS_BADCHECKSUM:
		return -1;

	    case STATUS_EOF:
		return 0;
	    }

	    /* Record of zeroes */
	case STATUS_EOFMARK:
	    status = prev_status;	/* If error after 0's */
	    /* FALL THRU */

	case STATUS_EOF:	/* End of archive */
	    break;
	}
	break;
    };

    return 0;
}


/* Create a tar header based on ST and NAME where NAME is a path.
   If NAME is a hard link (resp. symlink), HARDLINK (resp.
   SYMLINK) is the path of NAME's target.
   Also see GNU tar's create.c:start_header() (Ludovic).  */
void
tar_make_header (tar_record_t *header, io_statbuf_t *st, char *name,
    		 char *symlink, char *hardlink)
{
  int i;
  long sum = 0;
  char *p;

  /* NAME must be at most NAMSIZ long.  */
  assert (strlen (name) <= NAMSIZ);

  bzero (header, sizeof (* header));
  strcpy (header->header.arch_name, name);

  /* If it's a dir, add a trailing '/' */
  if (S_ISDIR (st->st_mode))
  {
    size_t s = strlen (name);
    if (s + 1 <= NAMSIZ + 1)
    {
      header->header.arch_name[s] = '/';
      header->header.arch_name[s+1] = '\0';
    }
  }

#define TO_OCT(what, where) \
  sprintf (where, "%07o", what);
#define LONG_TO_OCT(what, where) \
  sprintf (where, "%011llo", what);
#define TIME_TO_OCT(what, where) \
  sprintf (where, "%011lo", what);

  TO_OCT (st->st_mode, header->header.mode);
  TO_OCT (st->st_uid, header->header.uid);
  TO_OCT (st->st_gid, header->header.gid);
  LONG_TO_OCT (st->st_size, header->header.size);
  TIME_TO_OCT (st->st_mtime, header->header.mtime);

  /* Set the correct file type.  */
  if (S_ISREG (st->st_mode))
    header->header.linkflag = LF_NORMAL;
  else if (S_ISDIR (st->st_mode))
    header->header.linkflag = LF_DIR;
  else if (S_ISLNK (st->st_mode))
  {
    assert (symlink);
    assert (strlen (symlink) <= NAMSIZ);
    header->header.linkflag = LF_SYMLINK;
    memcpy (header->header.arch_linkname, symlink, strlen (symlink));
  }
  else if (S_ISCHR (st->st_mode))
    header->header.linkflag = LF_CHR;
  else if (S_ISBLK (st->st_mode))
    header->header.linkflag = LF_BLK;
  else if (S_ISFIFO (st->st_mode))
    header->header.linkflag = LF_FIFO;
  else if (hardlink)
  {
    assert (strlen (hardlink) <= NAMSIZ);
    header->header.linkflag = LF_LINK;
    memcpy (header->header.arch_linkname, hardlink, strlen (hardlink));
  }
  else
    header->header.linkflag = LF_NORMAL;

  strncpy (header->header.magic, TMAGIC, strlen(TMAGIC));

  uid_to_uname (st->st_uid, header->header.uname);
  gid_to_gname (st->st_gid, header->header.gname);

  /* Compute a checksum for this header.  */
  strncpy (header->header.chksum, CHKBLANKS, sizeof (header->header.chksum));
  p = header->charptr;
  for (sum = 0, i = sizeof (*header); --i >= 0;)
      sum += 0xFF & *p++;

  sprintf (header->header.chksum, "%06lo", sum);
  header->header.chksum[6] = '\0';
}
