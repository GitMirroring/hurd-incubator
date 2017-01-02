/**********************************************************
 * netfs.c
 *
 * Copyright (C) 2004, 2005, 2007 by Stefan Siegl <stesie@brokenpipe.de>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Publice License,
 * version 2 or any later. The license is contained in the COPYING
 * file that comes with the cvsfs distribution.
 *
 * callback functions for libnetfs
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stddef.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include <hurd/netfs.h>

#include <stdio.h>

#include "cvsfs.h"
#include "cvs_files.h"
#include "node.h"
#include "cvs_connect.h"



/* Make sure that NP->nn_stat is filled with current information.  CRED
   identifies the user responsible for the operation.  */
error_t
netfs_validate_stat (struct node *node, struct iouser *cred)
{
  (void) cred;

  FUNC_PROLOGUE_NODE("netfs_validate_stat", node);

  if(! config.nostats
     && node->nn->revision && node->nn->parent)
    {
      if(! node->nn->revision->contents)
	{
	  /* head revision not available locally yet, retrieve it ... */
	  pthread_rwlock_wrlock(&node->nn->revision->lock);
	  cvs_files_cache(node->nn, node->nn->revision);
	  pthread_rwlock_unlock(&node->nn->revision->lock);
	}

      if(node->nn->revision->contents)
	{
	  node->nn_stat.st_mode = (node->nn->revision->perm | S_IFREG)
	    &~(S_IWUSR | S_IWGRP | S_IWOTH);
	  node->nn_stat.st_size = node->nn->revision->length;
	  node->nn_stat.st_blocks = (node->nn_stat.st_size >> 9) + 1;

	  node->nn_stat.st_mtim.tv_sec = node->nn->revision->time;
	  node->nn_stat.st_ctim.tv_sec = node->nn->revision->time;

	  node->nn_stat.st_mtim.tv_nsec = 0;
	  node->nn_stat.st_ctim.tv_nsec = 0;
	}
    }

  FUNC_EPILOGUE(0);
}



/* Read the contents of NODE (a symlink), for USER, into BUF. */
error_t netfs_attempt_readlink (struct iouser *user, struct node *node,
				char *buf)
{
  (void) user;
  (void) buf;

  FUNC_PROLOGUE_NODE("netfs_attempt_readlink", node);

  /* actually we don't have no symlinks in cvsfs, at least not for
   * the time being
   */

  FUNC_EPILOGUE(EINVAL);
}



/* Attempt to create a file named NAME in DIR for USER with MODE.  Set *NODE
   to the new node upon return.  On any error, clear *NODE.  *NODE should be
   locked on success; no matter what, unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **node)
{
  (void) user;
  (void) mode;

  FUNC_PROLOGUE_FMT("netfs_attempt_create_file", "name=%s", name);

  *node = 0;
  pthread_mutex_unlock (&dir->lock);

  FUNC_EPILOGUE(EROFS);
}



/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the owner to UID and the group to GID. */
error_t netfs_attempt_chown (struct iouser *cred, struct node *node,
			     uid_t uid, uid_t gid)
{
  (void) cred;
  (void) uid;
  (void) gid;

  FUNC_PROLOGUE_NODE("netfs_attempt_chown", node);
  FUNC_EPILOGUE(EROFS);
}



/* This should attempt to fetch filesystem status information for the remote
   filesystem, for the user CRED. */
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *node,
		      fsys_statfsbuf_t *st)
{
  (void) cred;
  (void) st;

  FUNC_PROLOGUE_NODE("netfs_attempt_statfs", node);
  FUNC_EPILOGUE(EOPNOTSUPP);
}



/* Attempt to create a new directory named NAME in DIR for USER with mode
   MODE.  */
error_t netfs_attempt_mkdir (struct iouser *user, struct node *dir,
			     char *name, mode_t mode)
{
  (void) user;
  (void) dir;
  (void) mode;

  FUNC_PROLOGUE_FMT("netfs_attempt_mkdir", "name=%s", name);
  FUNC_EPILOGUE(EROFS);
}



/* This should attempt a chflags call for the user specified by CRED on node
   NODE, to change the flags to FLAGS. */
error_t netfs_attempt_chflags (struct iouser *cred, struct node *node,
			       int flags)
{
  (void) cred;
  (void) flags;

  FUNC_PROLOGUE_NODE("netfs_attempt_chflags", node);
  FUNC_EPILOGUE(EROFS);
}



/* Node NODE is being opened by USER, with FLAGS.  NEWNODE is nonzero if we
   just created this node.  Return an error if we should not permit the open
   to complete because of a permission restriction. */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *node,
			      int flags, int newnode)
{
  (void) newnode;

  FUNC_PROLOGUE_NODE("netfs_check_open_permissions", node);
  error_t err = 0;

  if (flags & O_READ)
    err = fshelp_access (&node->nn_stat, S_IREAD, user);

  if (!err && (flags & O_WRITE))
    err = fshelp_access (&node->nn_stat, S_IWRITE, user);

  if (!err && (flags & O_EXEC))
    err = fshelp_access (&node->nn_stat, S_IEXEC, user);

  FUNC_EPILOGUE(err);
}



/* This should attempt a chmod call for the user specified by CRED on node
   NODE, to change the mode to MODE.  Unlike the normal Unix and Hurd meaning
   of chmod, this function is also used to attempt to change files into other
   types.  If such a transition is attempted which is impossible, then return
   EOPNOTSUPP.  */
error_t netfs_attempt_chmod (struct iouser *cred, struct node *node,
			     mode_t mode)
{
  (void) cred;
  (void) mode;

  FUNC_PROLOGUE_NODE("netfs_attempt_chmod", node);
  FUNC_EPILOGUE(EROFS);
}



/* Attempt to create an anonymous file related to DIR for USER with MODE.
   Set *NODE to the returned file upon success.  No matter what, unlock DIR. */
error_t netfs_attempt_mkfile (struct iouser *user, struct node *dir,
			      mode_t mode, struct node **node)
{
  (void) user;
  (void) mode;

  FUNC_PROLOGUE("netfs_attempt_mkfile");

  *node = 0;
  pthread_mutex_unlock (&dir->lock);

  FUNC_EPILOGUE(EROFS);
}



/* This should sync the entire remote filesystem.  If WAIT is set, return
   only after sync is completely finished.  */
error_t netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  (void) cred;
  (void) wait;

  FUNC_PROLOGUE("netfs_attempt_syncfs");

  /* we don't support writing */
  FUNC_EPILOGUE(0);
}



/* This should sync the file NODE completely to disk, for the user CRED.  If
   WAIT is set, return only after sync is completely finished.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *node, int wait)
{
  (void) cred;
  (void) wait;

  FUNC_PROLOGUE_NODE("netfs_attempt_sync", node);

  /* we don't support writing to files, therefore syncing isn't really
   * much to worry about ...
   */
  FUNC_EPILOGUE(0);
}



/* Delete NAME in DIR for USER. */
error_t netfs_attempt_unlink (struct iouser *user, struct node *dir,
			      char *name)
{
  (void) user;

  FUNC_PROLOGUE_FMT("netfs_attempt_unlink", "dir=%s, name=%s",
		    dir->nn->name, name);
  FUNC_EPILOGUE(EROFS);
}



/* This should attempt to set the size of the file NODE (for user CRED) to
   SIZE bytes long. */
error_t netfs_attempt_set_size (struct iouser *cred, struct node *node,
				loff_t size)
{
  (void) cred;
  (void) size;

  FUNC_PROLOGUE_NODE("netfs_attempt_set_size", node);
  FUNC_EPILOGUE(EOPNOTSUPP);
}



/* Attempt to turn NODE (user CRED) into a device.  TYPE is either S_IFBLK or
   S_IFCHR. */
error_t netfs_attempt_mkdev (struct iouser *cred, struct node *node,
			     mode_t type, dev_t indexes)
{
  (void) cred;
  (void) type;
  (void) indexes;

  FUNC_PROLOGUE_NODE("netfs_attempt_mkdev", node);
  FUNC_EPILOGUE(EROFS);
}



/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and O_EXEC)
   in *TYPES for file NODE and user CRED.  */
error_t
netfs_report_access (struct iouser *cred, struct node *node, int *types)
{
  FUNC_PROLOGUE_NODE("netfs_report_access", node);
  *types = 0;

  if (fshelp_access (&node->nn_stat, S_IREAD, cred) == 0)
    *types |= O_READ;
  
  /* we don't support writing to files, therefore don't even think of
   * returning writable ...
   *
   * if (fshelp_access (&node->nn_stat, S_IWRITE, cred) == 0)
   *   *types |= O_WRITE;
   */

  if (fshelp_access (&node->nn_stat, S_IEXEC, cred) == 0)
    *types |= O_EXEC;
  
  FUNC_EPILOGUE_FMT(0, "types=%d", *types);
}



/* Lookup NAME in DIR for USER; set *NODE to the found name upon return.  If
   the name was not found, then return ENOENT.  On any error, clear *NODE.
   (*NODE, if found, should be locked, this call should unlock DIR no matter
   what.) */
error_t netfs_attempt_lookup (struct iouser *user, struct node *dir,
			      char *name, struct node **node)
{
  (void) user;

  FUNC_PROLOGUE_FMT("netfs_attempt_lookup", "dir=%s, name=%s",
		    dir->nn->name, name);
  error_t err = ENOENT;
  struct netnode *nn;

  if(! strcmp(name, "."))
    {
      /* lookup for a directory, just return another reference to
       * our cwd 'dir'
       */
      netfs_nref(dir);

      *node = dir;
      err = 0;
    }
  else if(! strcmp(name, ".."))
    {
      if(dir->nn->parent)
	{
	  /* return a reference to our parent */
	  *node = cvsfs_make_node(dir->nn->parent);
	  err = 0;
	}
      else
	/* this is the root directory of cvsfs, but the user 
	 * requests to go up by one. we can't tell, where to go, so ...
	 */
	err = EAGAIN;
    }
  else if(dir->nn->revision)
    {
      struct revision *rev;
      struct netnode *nn = dir->nn->parent ? dir->nn : dir->nn->child;

      /* read-lock the real netnode - not the virtual one - what wouldn't
       * make much sense.
       */
      pthread_rwlock_rdlock(&nn->lock);
      rev = dir->nn->revision;

      for(; rev; rev = rev->next)
	if(! strcmp(rev->id, name))
	  break;

      pthread_rwlock_unlock(&nn->lock);

      if(! rev && (rev = malloc(sizeof(*rev))))
	{
	  /* okay, we don't have this particular revision available;
	   * create a new revision structure and try retrieving it
	   */
	  rev->id = strdup(name);
	  rev->contents = NULL;
	  rev->next = NULL;
	  pthread_rwlock_init(&rev->lock, NULL);

	  if(cvs_files_hit(nn, rev))
	    {
	      /* unable to hit wanted revision. */
	      free(rev->id);
	      free(rev);
	      rev = NULL;
	    }
	  else
	    {
	      /* okay, went well, enqueue into revisions chain */
	      pthread_rwlock_wrlock(&nn->lock);

	      rev->next = nn->revision->next;
	      nn->revision->next = rev;

	      pthread_rwlock_unlock(&nn->lock);
	    }
	}

      if(rev)
	{
	  /* cool, we've got that revision! */
	  *node = cvsfs_make_virtual_node(nn, rev);
	  err = 0;
	}
    }
  else
    {
      for(nn = dir->nn->child; nn; nn = nn->sibling)
	if(! strcmp(nn->name, name)) 
	  {
	    err = 0; /* hey, we got it! */

	    /* pthread_rwlock_rdlock(&nn->lock);
	     * we don't have to lock nn->lock since it's ref cannot become
	     * invalid
	     */

	    if((*node = nn->node))
	      netfs_nref (*node);

	    /* pthread_rwlock_unlock(&nn->lock); */

	    if(! *node)
	      *node = cvsfs_make_node(nn);

	    break;
	  }
    }

  if(! err)
    fshelp_touch(&(*node)->nn_stat, TOUCH_ATIME, cvsfs_maptime);

  pthread_mutex_unlock(&dir->lock);

  if(err)
    *node = NULL;
  else
    pthread_mutex_lock(&(*node)->lock);

  FUNC_EPILOGUE(err);
}



/* Create a link in DIR with name NAME to FILE for USER.  Note that neither
   DIR nor FILE are locked.  If EXCL is set, do not delete the target, but
   return EEXIST if NAME is already found in DIR.  */
error_t netfs_attempt_link (struct iouser *user, struct node *dir,
			    struct node *file, char *name, int excl)
{
  (void) user;
  (void) dir;
  (void) name;
  (void) excl;

  FUNC_PROLOGUE_NODE("netfs_attempt_link", file);
  FUNC_EPILOGUE(EROFS);
}



/* Attempt to remove directory named NAME in DIR for USER. */
error_t netfs_attempt_rmdir (struct iouser *user,
			     struct node *dir, char *name)
{
  (void) user;

  FUNC_PROLOGUE_FMT("netfs_attempt_rmdir", "dir=%s, name=%s",
			  dir->nn->name, name);
  FUNC_EPILOGUE(EROFS);
}



/* This should attempt a chauthor call for the user specified by CRED on node
   NODE, to change the author to AUTHOR. */
error_t netfs_attempt_chauthor (struct iouser *cred, struct node *node,
				uid_t author)
{
  (void) cred;
  (void) author;

  FUNC_PROLOGUE_NODE("netfs_attempt_chauthor", node);
  FUNC_EPILOGUE(EROFS);
}



/* Attempt to turn NODE (user CRED) into a symlink with target NAME. */
error_t netfs_attempt_mksymlink (struct iouser *cred, struct node *node,
				 char *name)
{
  (void) cred;
  (void) name;

  FUNC_PROLOGUE_NODE("netfs_attempt_mksymlink", node);
  FUNC_EPILOGUE(EROFS);
}



/* Note that in this one call, neither of the specific nodes are locked. */
error_t netfs_attempt_rename (struct iouser *user, struct node *fromdir,
			      char *fromname, struct node *todir,
			      char *toname, int excl)
{
  (void) user;
  (void) fromdir;
  (void) fromname;
  (void) todir;
  (void) toname;
  (void) excl;

  FUNC_PROLOGUE("netfs_attempt_rename");
  FUNC_EPILOGUE(EROFS);
}



/* Write to the file NODE for user CRED starting at OFSET and continuing for up
   to *LEN bytes from DATA.  Set *LEN to the amount seccessfully written upon
   return. */
error_t netfs_attempt_write (struct iouser *cred, struct node *node,
			     loff_t offset, size_t *len, void *data)
{
  (void) cred;
  (void) offset;
  (void) len;
  (void) data;

  FUNC_PROLOGUE_NODE("netfs_attempt_write", node);
  FUNC_EPILOGUE(EROFS);
}



/* This should attempt a utimes call for the user specified by CRED on node
   NODE, to change the atime to ATIME and the mtime to MTIME. */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *node,
		      struct timespec *atime, struct timespec *mtime)
{
  (void) cred;

  FUNC_PROLOGUE_NODE("netfs_attempt_utimes", node);
  error_t err = fshelp_isowner (&node->nn_stat, cred);
  int flags = TOUCH_CTIME;
  
  if (! err)
    {
      if (mtime)
	{
	  node->nn_stat.st_mtim.tv_sec = mtime->tv_sec;
	  node->nn_stat.st_mtim.tv_nsec = mtime->tv_nsec;
	}
      else
	flags |= TOUCH_MTIME;
      
      if (atime)
	{
	  node->nn_stat.st_atim.tv_sec = atime->tv_sec;
	  node->nn_stat.st_atim.tv_nsec = atime->tv_nsec;
	}
      else
	flags |= TOUCH_ATIME;
      
      fshelp_touch (&node->nn_stat, flags, cvsfs_maptime);
    }

  FUNC_EPILOGUE(err);
}



/* Read from the file NODE for user CRED starting at OFFSET and continuing for
   up to *LEN bytes.  Put the data at DATA.  Set *LEN to the amount
   successfully read upon return.  */
error_t netfs_attempt_read (struct iouser *cred, struct node *node,
			    loff_t offset, size_t *len, void *data)
{
  (void) cred;

  FUNC_PROLOGUE_NODE("netfs_attempt_read", node);
  int maxlen;

  if(! node->nn->revision)
    {
      fprintf(stderr, "netfs_attempt_read entered, for something not "
	      "being a CVS revision controlled file. getting outta here.\n");
      return EISDIR;
    }

  pthread_rwlock_rdlock(&node->nn->lock);
  pthread_rwlock_rdlock(&node->nn->revision->lock);

  if(! node->nn->revision->contents) 
    {
      /* we don't have the content of this revision cached locally,
       * therefore try to fetch it.
       *
       * TODO: consider whether it's possible (if using non-blocking I/O)
       * to fork a retrieval task, and return 0 bytes for the time being ..
       */

      /* oops, we need a writer lock ... */
      pthread_rwlock_unlock(&node->nn->revision->lock);
      pthread_rwlock_wrlock(&node->nn->revision->lock);

      if(cvs_files_cache(node->nn->parent ? node->nn : node->nn->child,
			 node->nn->revision))
	{
	  pthread_rwlock_unlock(&node->nn->revision->lock);
	  pthread_rwlock_unlock(&node->nn->lock);
	  *len = 0;
	  return EIO;
	}

      /* TODO consider whether there's a nicer way, so that we don't have
       * to relock two times 
       */
      pthread_rwlock_unlock(&node->nn->revision->lock);
      pthread_rwlock_rdlock(&node->nn->revision->lock);
    }

  maxlen = node->nn->revision->length;
  
  if(offset >= maxlen)
    {
      /* trying to read beyond of file, cowardly refuse to do so ... */
      *len = 0;
      pthread_rwlock_unlock(&node->nn->revision->lock);
      pthread_rwlock_unlock(&node->nn->lock);
      return 0;
    }

  if(*len + offset >= maxlen)
    *len = maxlen - offset;

  memcpy(data, node->nn->revision->contents + offset, *len);
  pthread_rwlock_unlock(&node->nn->revision->lock);
  pthread_rwlock_unlock(&node->nn->lock);

  FUNC_EPILOGUE(0);
}



/* Returned directory entries are aligned to blocks this many bytes long.
   Must be a power of two.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)

/* Length is structure before the name + the name + '\0', all
   padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len)						      \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1))		      \
   & ~(DIRENT_ALIGN - 1))

error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		   int first_entry, int num_entries, char **data,
		   mach_msg_type_number_t *data_len,
		   vm_size_t max_data_len, int *data_entries)
{
  (void) cred;
  (void) max_data_len;

  FUNC_PROLOGUE_NODE("netfs_get_dirents", dir);
  error_t err = 0;
  int count = 0;
  struct netnode *first_nn, *nn;

  if(dir->nn->revision)
    return ENOTDIR; /* it's a file ... */

  /* find the first entry, we shall write out to the user ... */
  for(first_nn = dir->nn->child, count = 2; 
      first_nn && first_entry > count;
      first_nn = first_nn->sibling, count ++);

  size_t size = 0;
  char *p = *data;
  count = 0;

  int add_dir_entry (const char *name, ino_t fileno, int type)
    {
      if (num_entries == -1 || count < num_entries)
	{
	  struct dirent hdr;
	  size_t name_len = strlen (name);
	  size_t sz = DIRENT_LEN (name_len);

	  if (sz + size > *data_len)
	    return 0;
	  else
	    size += sz;

	  hdr.d_fileno = fileno;
	  hdr.d_reclen = sz;
	  hdr.d_type = type;
	  hdr.d_namlen = name_len;

	  memcpy (p, &hdr, DIRENT_NAME_OFFS);
	  strcpy (p + DIRENT_NAME_OFFS, name);
	  p += sz;

	  count++;

	  return 1;
	}
      else
	return 0;
    }

  /* Add `.' and `..' entries.  */
  if (first_entry == 0)
    add_dir_entry (".", 2, DT_DIR);
  if (first_entry <= 1)
    add_dir_entry ("..", 2, DT_DIR);

  /* okay, now tell about the real entries ... */
  for(nn = first_nn; nn; nn = nn->sibling)
    if(! add_dir_entry(nn->name, nn->fileno,
		       nn->revision ? DT_REG : DT_DIR))
      break;

  *data_len = size;
  *data_entries = count;

  fshelp_touch (&dir->nn_stat, TOUCH_ATIME, cvsfs_maptime);
  FUNC_EPILOGUE_FMT(err, "wrote %d entries to %d bytes.", count, size);
}



/* Node NP is all done; free all its associated storage. */
void
netfs_node_norefs (struct node *node)
{
  FUNC_PROLOGUE_NODE("netfs_node_norefs", node);

  /* the node will be freed, therefore our nn->node pointer will not
   * be valid any longer, therefore reset it 
   */
  pthread_rwlock_wrlock(&node->nn->lock);
  node->nn->node = NULL;
  pthread_rwlock_unlock(&node->nn->lock);

  if(node->nn->revision && !node->nn->parent)
    /* node is a virtual node, therefore we need to free the netnode */
    free(node->nn);

  FUNC_EPILOGUE_NORET();
}
