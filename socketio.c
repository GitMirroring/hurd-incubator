/* socketio - A translator to allow socket I/O via the filesystem.
   Copyright (C) 2001, 02 Free Software Foundation, Inc.
   Written by Moritz Schulte <moritz@duesseldorf.ccc.de>.
 
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or * (at your option) any later version.
 
   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
   USA */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <error.h>
#include <argp.h>
#include <argz.h>
#include <netdb.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdint.h>
#include <assert.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include <hurd.h>
#include <hurd/socket.h>
#include <hurd/paths.h>
#include <hurd/netfs.h>
#include <hurd/iohelp.h>
#include <maptime.h>

#include "version.h"


/* Netfs initialization.  */
char *netfs_server_name = "socketio";
char *netfs_server_version = HURD_VERSION;
int netfs_maxsymlinks = 0;

/* argp initialization.  */
const char *argp_program_version = STANDARD_HURD_VERSION (SERVER_NAME);
const char *argp_doc = "A translator for socket input/output.";

/* The underlying node.  */
static mach_port_t ul_node;

/* The socket server, PF_INET for us.  */
static pf_t socket_server;

/* Our filesystem id - will be our pid.  */
static int fsid;

/* The default stat information for new nodes.  */
static struct stat stat_default;

/* Used for updating node information.  */
static volatile struct mapped_time_value *maptime;

/* Argp options; none yet.  */
static const struct argp_option main_options[] =
{
  { 0 }
};

/* The supported protocols.  */
#define PROTOCOL_ID_TCP 0x00000001
#define PROTOCOL_ID_UDP 0x00000002
#define PROTOCOL_NAME_TCP "tcp"
#define PROTOCOL_NAME_UDP "udp"

/* One of these items exists for each supported protocol.  */
typedef struct protocol
{
  int id;
  char *name;
} protocol_t;

/* Here we fill PROTOCOLS with information about the available
   protocols.  */

static error_t socket_open (int style, char *hostname, uint16_t netport,
			    mach_port_t *port);

#define PROTOCOL_INIT(p) { PROTOCOL_ID_##p, PROTOCOL_NAME_##p }

static protocol_t protocols[] = { PROTOCOL_INIT (TCP),
				  PROTOCOL_INIT (UDP),
				  { 0, NULL } };

/* The different kinds of nodes managed by this server.  */
#define ROOT_NODE     0x00000001
#define PROTOCOL_NODE 0x00000002
#define HOST_NODE     0x00000004

/* Our private informations associated with netfs node.  */
struct netnode
{
  unsigned short int flags;     /* Either ROOT_NODE, PROTOCOL_NODE or
			           HOST_NODE.  */
  char *hostname;		/* The host to connect to.  */
  uint16_t port;		/* The port to connect to.  */
  struct protocol *protocol;	/* The protocol information.  */
  struct node *entries;		/* The entries in this directory, only
				   useful for the root node.  */
  struct node *dir;		/* The parent directory.  */
};

/* Like the original netfs_attempt_lookup(), except that this version
   supports returning ports directly.  */
error_t netfs_attempt_lookup_improved (struct iouser *user, struct node *dir,
				       char *name, struct node **np,
				       mach_port_t *port,
				       mach_msg_type_name_t *port_type);



/* General macros.  */

/* Deallocate a port.  */
#define port_deallocate(p) mach_port_deallocate (mach_task_self (), p)

/* Initialize the `sockaddr_in' structure ADDR for FAMILY, HOST and
   PORT.  */
#define sockaddr_init(addr, family, hostinfo, port)           \
  do                                                          \
    {                                                         \
      addr.sin_family = (family);                             \
      addr.sin_addr = *(struct in_addr *) (hostinfo)->h_addr; \
      addr.sin_port = htons ((port));                         \
    }                                                         \
  while (0)

/* Returned directory entries are aligned to blocks this many bytes
   long.  Must be a power of two.  For netfs_get_dirents.  */
#define DIRENT_ALIGN 4
#define DIRENT_NAME_OFFS offsetof (struct dirent, d_name)
/* Length is structure before the name + the name + '\0', all
   padded to a four-byte alignment.  */
#define DIRENT_LEN(name_len)						      \
  ((DIRENT_NAME_OFFS + (name_len) + 1 + (DIRENT_ALIGN - 1))		      \
   & ~(DIRENT_ALIGN - 1))


/* Support routines.  */

/* Open a port to the socket server for the protocol family number NO
   and store it in *SOCK.  Return zero on success or an error
   code.  */
static error_t
open_socket_server (int no, pf_t *sock)
{
  char *path;
  error_t err;
  pf_t port;

  err = asprintf (&path, "%s/%i", _SERVERS_SOCKET, no);
  if (err < 0)
    return ENOMEM;
  err = 0;
  port = file_name_lookup (path, 0, 0);
  free (path);
  if (port == MACH_PORT_NULL)
    err = errno;
  else
    *sock = port;
  return err;
}

/* Wrapper function for xgethostbyname_r; get the host address for
   HOSTNAME and store it in *HOSTADDR, the buffer holding the date
   will be stored in *BUF.  Return zero on success or an error
   code.  */
static int
xgethostbyname (char *hostname, struct hostent *hostaddr, char **buf)
{
  struct hostent hostbuf, *hp;
  size_t hostbuf_len;
  char *tmp_hostbuf;
  int herr, err;

  hostbuf_len = 64;
  tmp_hostbuf = malloc (hostbuf_len);
  if (! tmp_hostbuf)
    return ENOMEM;

  while ((err = gethostbyname_r (hostname, &hostbuf,
				 tmp_hostbuf, hostbuf_len,
				 &hp, &herr)) == ERANGE)
    {
      char *p;

      hostbuf_len *= 2;
      p = realloc (tmp_hostbuf, hostbuf_len);
      if (! p)
	{
	  err = ENOMEM;
	  break;
	}
      tmp_hostbuf = p;
    }
  if (! err)
    {
      *hostaddr = *hp;
      *buf = tmp_hostbuf;
    }
  else
    {
      if (tmp_hostbuf)
	free (tmp_hostbuf);
    }
  return err;
}



/* Node management.  */

/* Create a new node in the directory *DIR (if DIR is nonzero) and
   store it in *NODE.  If CONNECT is true (in which case *DIR has to
   be a valid directory node), attach the new node to the list of
   directory entries of *DIR.  Return zero on success or an error
   code.  */
static error_t
node_make_new (struct node **node)
{
  struct netnode *netnode;
  error_t err = 0;

  netnode = malloc (sizeof (struct netnode));
  if (! netnode)
    err = ENOMEM;
  else
    {
      *node = netfs_make_node (netnode);
      if (! *node)
	{
	  free (netnode);
	  err = ENOMEM;
	}
    }
  if (! err)
    {
      (*node)->nn->flags = 0;
      (*node)->nn->hostname = NULL;
      (*node)->nn->protocol = NULL;
      (*node)->nn->entries = NULL;
      (*node)->nn->dir = NULL;
      (*node)->nn_stat = stat_default;
      (*node)->nn_translated = stat_default.st_mode;
      (*node)->next = NULL;
      (*node)->prevp = NULL;
    }
  return err;
}


/* Destroy the node and release a reference to the parent directory.  */
static void
node_destroy (struct node *np)
{
  if (np->nn->flags & HOST_NODE)
    free (np->nn->hostname);
  free (np->nn);
  free (np);
}


/* Install the node NODE with DIR as parent directory node; create one
   reference for NODE.  */
static void
node_install (struct node *node, struct node *dir)
{
  node->next = dir->nn->entries;
  node->prevp = &dir->nn->entries;
  if (node->next)
    node->next->prevp = &node->next;
  dir->nn->entries = node;
  node->nn->dir = dir;
}


/* Prepare the node NODE for destruction and remove it from lists to
   which it is connected.  */
static void
node_uninstall (struct node *node)
{
  *node->prevp = node->next;
  if (node->next)
    node->next->prevp = node->prevp;
}


/* Create a new protocol node for *PROTOCOL in the directory *DIR.
   Return zero on success or an error code.  */
static error_t
node_make_protocol_node (struct node *dir, protocol_t *protocol)
{
  struct node *node;
  error_t err;

  err = node_make_new (&node);
  if (err)
    return err;
  node->nn->flags |= PROTOCOL_NODE;
  node->nn->protocol = protocol;
  node->nn_stat.st_mode = S_IFDIR | S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP
    | S_IROTH | S_IXOTH;
  node->nn_stat.st_ino = protocol->id;
  node->nn_stat.st_nlink = 2;
  fshelp_touch (&node->nn_stat, TOUCH_ATIME | TOUCH_CTIME | TOUCH_MTIME,
		maptime);
  node_install (node, dir);
  netfs_nref (dir);
  return err;
}


/* Create a new host node for HOST in the directory *DIR and store it
   in *NODE.  Return zero on success or an error code.  */
static error_t
node_make_host_node (struct iouser *user, struct node *dir, char *host,
		     struct node **node)
{
  struct node *np;
  error_t err = 0;

  host = strdup (host);
  if (!host)
    return ENOMEM;

  err = node_make_new (&np);
  if (err)
    {
      free (host);
      return err;
    }

  np->nn->hostname = host;
  np->nn->flags |= HOST_NODE;
  np->nn->protocol = dir->nn->protocol;
  np->nn_stat.st_uid = *user->uids->ids;
  np->nn_stat.st_gid = *user->gids->ids;
  np->nn_stat.st_mode = S_IFDIR | S_IRUSR | S_IXUSR ;
  fshelp_touch (&np->nn_stat, TOUCH_ATIME | TOUCH_CTIME | TOUCH_MTIME,
		maptime);
  node_install (np, dir);
  *node = np;

  return err;
}


/* This function creates a new socket port for the user USER, who
   tries to lookup the "netport" NETPORT_S beneath the HOST_NODE *NP.
   Return the created socket port in *PORT and the according port type
   in *PORT_TYPE.  Return zero on success or an error code.  */
static error_t
node_socket_open (struct iouser *user, struct node *np, char *netport_s,
		  mach_port_t *port, mach_msg_type_name_t *port_type)
{
  uint16_t netport = strtol (netport_s, NULL, 10);
  char *hostname = np->nn->hostname;
  socket_t sock = MACH_PORT_NULL;
  error_t err;
  int style;

  switch (np->nn->protocol->id)
    {
    case PROTOCOL_ID_TCP:
      style = SOCK_STREAM;
      break;
    case PROTOCOL_ID_UDP:
      style = SOCK_DGRAM;
      break;
    default:
      return EINVAL;
    }

  err = socket_open (style, hostname, netport, &sock);
  if (! err)
    err = io_restrict_auth (sock, port,
			    user->uids->ids, user->uids->num,
			    user->gids->ids, user->gids->num);
  if (! err)
    *port_type = MACH_MSG_TYPE_MOVE_SEND;
  if (err)
    {
      if (sock)
	port_deallocate (sock);
      if (*port)
	port_deallocate (*port);
    }
  return err;
}


/* Create the root node.  Return zero on success or an error code.  */
static error_t
node_make_root_node (struct node **node)
{
  struct node *np;
  error_t err;
  err = node_make_new (&np);
  if (! err)
    {
      np->nn->flags |= ROOT_NODE;
      *node = np;
    }
  return err;
}

/* Open a socket port for STYLE (either SOCK_STREAM OR SOCK_DGRAM) to
   HOSTNAME and NETPORT.  On success, a new socket is stored in *PORT
   and zero is returned, otherwise an error code is returned.  */
static error_t
socket_open (int style, char *hostname, uint16_t netport,
	     mach_port_t *port)
{
  addr_port_t aport = MACH_PORT_NULL;
  socket_t sock = MACH_PORT_NULL;
  struct sockaddr_in addr;
  struct hostent hostaddr;
  error_t err = 0;
  char *buf;

  err = xgethostbyname (hostname, &hostaddr, &buf);
  if (! err) {
    sockaddr_init (addr, AF_INET, &hostaddr, netport);
    free (buf);
  }
  if (! err)
    err = socket_create (socket_server, style, 0, &sock);
  if (! err)
    err = socket_create_address (sock, addr.sin_family, (char *) &addr,
				 sizeof (addr), &aport);
  if (! err)
    err = socket_connect (sock, aport);

  if (! err)
    *port = (mach_port_t) sock;
  else if (sock != MACH_PORT_NULL)
    port_deallocate (sock);

  if (aport != MACH_PORT_NULL)
    port_deallocate (aport);

  return err;
}


/* Store the protocol node for the protocol specified by NAME in
   *NODE.  Return zero on success or ENOENT if the node could not be
   found.  */
static error_t
protocol_find_node (char *name, struct node **node)
{
  struct node *np;
  error_t err = ENOENT;

  for (np = netfs_root_node->nn->entries;
       np && strcmp (np->nn->protocol->name, name);
       np = np->next);
  if (np)
    {
      *node = np;
      err = 0;
    }
  return err;
}


/* Register the protocols - create according nodes.  Return zero on
   success or an error code.  */
static error_t
protocol_register_default (void)
{
  protocol_t *proto;
  error_t err = 0;

  for (proto = protocols; ! err && proto->name; proto++)
    err = node_make_protocol_node (netfs_root_node, proto);

  return err;
}


/* netfs callbacks.  */

/* We need our own version of netfs_S_dir_lookup, since netfs does not
   yet support "foreign ports" as results from dir_lookup.  */
error_t
netfs_S_dir_lookup (struct protid *diruser,
		    char *filename,
		    int flags,
		    mode_t mode,
		    retry_type *do_retry,
		    char *retry_name,
		    mach_port_t *retry_port,
		    mach_msg_type_name_t *retry_port_type)
{
  int mustbedir = 0;		/* true if the result must be S_IFDIR */
  int lastcomp = 0;		/* true if we are at the last component */
  struct node *dnp, *np;
  char *nextname;
  error_t err = 0;
  struct protid *newpi;
  struct iouser *user;
  mach_msg_type_name_t port_type;
  mach_port_t port = MACH_PORT_NULL;

  if (!diruser)
    return EOPNOTSUPP;

  /* Skip leading slashes */
  while (*filename == '/')
    filename++;

  *retry_port_type = MACH_MSG_TYPE_MAKE_SEND;
  *do_retry = FS_RETRY_NORMAL;
  *retry_name = '\0';

  if (*filename == '\0')
    {
      /* Set things up in the state expected by the code from gotit: on. */
      dnp = 0;
      np = diruser->po->np;
      pthread_mutex_lock (&np->lock);
      netfs_nref (np);
      goto gotit;
    }

  dnp = diruser->po->np;
  pthread_mutex_lock (&dnp->lock);

  netfs_nref (dnp);		/* acquire a reference for later netfs_nput */

  do
    {
      assert (!lastcomp);

      /* Find the name of the next pathname component */
      nextname = index (filename, '/');

      if (nextname)
	{
	  *nextname++ = '\0';
	  while (*nextname == '/')
	    nextname++;
	  if (*nextname == '\0')
	    {
	      /* These are the rules for filenames ending in /. */
	      nextname = 0;
	      lastcomp = 1;
	      mustbedir = 1;
	    }
	  else
	    lastcomp = 0;
	}
      else
	lastcomp = 1;

      np = 0;

      if ((dnp == netfs_root_node || dnp == diruser->po->shadow_root)
	  && filename[0] == '.' && filename[1] == '.' && filename[2] == '\0')
	if (dnp == diruser->po->shadow_root)

	  {
	    *do_retry = FS_RETRY_REAUTH;
	    *retry_port = diruser->po->shadow_root_parent;
	    *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
	    if (! lastcomp)
	      strcpy (retry_name, nextname);
	    err = 0;
	    pthread_mutex_unlock (&dnp->lock);
	    goto out;
	  }
	else if (diruser->po->root_parent != MACH_PORT_NULL)
	  /* We're at a real translator root; even if DIRUSER->po has a
	     shadow root, we can get here if its in a directory that was
	     renamed out from under it...  */
	  {
	    *do_retry = FS_RETRY_REAUTH;
	    *retry_port = diruser->po->root_parent;
	    *retry_port_type = MACH_MSG_TYPE_COPY_SEND;
	    if (!lastcomp)
	      strcpy (retry_name, nextname);
	    err = 0;
	    pthread_mutex_unlock (&dnp->lock);
	    goto out;
	  }
	else
	  /* We are global root */
	  {
	    err = 0;
	    np = dnp;
	    netfs_nref (np);
	  }
      else
	/* Attempt a lookup on the next pathname component. */
	err = netfs_attempt_lookup_improved (diruser->user, dnp,
					     filename, &np,
					     &port, &port_type);
      
      /* At this point, DNP is unlocked */
      
      if (! (err || np || lastcomp))
	{
	  /* Returning ports instead of nodes is only allowed for the
	     last pathname component.  */
	  mach_port_deallocate (mach_task_self (), *retry_port);
	  err = EIEIO;
	}

      if ((! err) && np)
	err = netfs_validate_stat (np, diruser->user);

      /* All remaining errors get returned to the user */
      if (err)
	goto out;

      /* Normal nodes here for next filename component */
      filename = nextname;
      netfs_nrele (dnp);

      if (lastcomp)
	dnp = 0;
      else
	{
	  dnp = np;
	  np = 0;
	}
    }
  while (filename && *filename);

  /* At this point, NP is the node to return.  */
 gotit:

  if (mustbedir && ! np)
    err = ENOTDIR;

  flags &= ~(O_CREAT|O_EXCL|O_NOLINK|O_NOTRANS|O_NONBLOCK);

  if (! err)
    {
      if (np)
	{
	  err = iohelp_dup_iouser (&user, diruser->user);
	  if (! err)
	    {
	      newpi = netfs_make_protid (netfs_make_peropen (np, flags,
							     diruser->po),
					 user);
	      if (! newpi)
		{
		  iohelp_free_iouser (user);
		  err = errno;
		}
	    }
	  if (! err)
	    {
	      *retry_port = ports_get_right (newpi);
	      ports_port_deref (newpi);
	    }
	}
      else
	{
	  *retry_port = port;
	  *retry_port_type = port_type;
	}
    }

 out:
  if (err && port != MACH_PORT_NULL)
    port_deallocate (port);
  if (np)
    netfs_nput (np);
  if (dnp)
    netfs_nrele (dnp);
  return err;
}

/* The user must define this function.  Lookup NAME in DIR (which is
   locked) for USER; set *NODE to the found name upon return.  If the
   name was not found, then return ENOENT.  On any error, clear *NODE.
   (*NODE, if found, should be locked and a reference to it generated.
   This call should unlock DIR no matter what.)  */
error_t
netfs_attempt_lookup (struct iouser *user, struct node *dir,
		      char *name, struct node **node)
{
  /* Not used.  */
  return EIEIO;
}

error_t
netfs_attempt_lookup_improved (struct iouser *user, struct node *dir,
			       char *name, struct node **node,
			       mach_port_t *port,
			       mach_msg_type_name_t *port_type)
{
  error_t err;

  err = fshelp_access (&dir->nn_stat, S_IEXEC, user);
  if (err)
    goto out;
  if (! strcmp (name, "."))
    {
      *node = dir;
      netfs_nref (*node);
    }
  else if (! strcmp (name, ".."))
    {
      *node = dir->nn->dir;
      netfs_nref (*node);
    }
  else
    {
      if (dir->nn->flags & PROTOCOL_NODE)
	err = node_make_host_node (user, dir, name, node);
      else if (dir->nn->flags & HOST_NODE)
	err = node_socket_open (user, dir, name, port, port_type);
      else if (dir->nn->flags & ROOT_NODE)
	{
	  err = protocol_find_node (name, node);
	  if (! err)
	    netfs_nref (*node);
	}
      else
	/* Trying to look something inside beneath a port node.  */
	err = ENOTDIR;
    }

 out:
  fshelp_touch (&dir->nn_stat, TOUCH_ATIME, maptime);
  pthread_mutex_unlock (&dir->lock);
  if (err)
    *node = 0;
  else if (*node)
    pthread_mutex_lock (&(*node)->lock);
  return err;
}


/* Return an argz string describing the current options.  Fill *ARGZ
   with a pointer to newly malloced storage holding the list and *LEN
   to the length of that storage.  */
error_t
netfs_append_args (char **argz, size_t *argz_len)
{
  return 0;
}


/* Make sure that NP->nn_stat is filled with current information.
   CRED identifies the user responsible for the operation. */
error_t
netfs_validate_stat (struct node *np, struct iouser *cred)
{
  return 0;
}


/* This should attempt a chmod call for the user specified by CRED on
   locked node NP, to change the owner to UID and the group to GID.  */
error_t
netfs_attempt_chown (struct iouser *cred, struct node *np,
		     uid_t uid, uid_t gid)
{
  return EOPNOTSUPP;
}


/* This should attempt a chauthor call for the user specified by CRED
   on locked node NP, thereby changing the author to AUTHOR.  */
error_t
netfs_attempt_chauthor (struct iouser *cred, struct node *np,
			uid_t author)
{
  return EOPNOTSUPP;
}


/* This should attempt a chmod call for the user specified by CRED on
   locked node NODE, to change the mode to MODE.  Unlike the normal
   Unix and Hurd meaning of chmod, this function is also used to
   attempt to change files into other types.  If such a transition is
   attempted which is impossible, then return EOPNOTSUPP.  */
error_t
netfs_attempt_chmod (struct iouser *cred, struct node *np,
		     mode_t mode)
{
  error_t err;

  /* We only support permission chmod's on the root node.  */
  if (np->nn->dir ||
      ((mode & S_IFMT) | (np->nn_stat.st_mode & S_IFMT))
      != (np->nn_stat.st_mode & S_IFMT))
    err = EOPNOTSUPP;
  else
    {
      if (! (err = fshelp_isowner (&np->nn_stat, cred)))
	{
	  np->nn_stat.st_mode = mode | (np->nn_stat.st_mode & S_IFMT);
	  fshelp_touch (&np->nn_stat, TOUCH_CTIME, maptime);
	}
    }
  return err;
}


/* Attempt to turn locked node NP (user CRED) into a symlink with
   target NAME.  */
error_t
netfs_attempt_mksymlink (struct iouser *cred, struct node *np,
			 char *name)
{
  return EOPNOTSUPP;
}


/* Attempt to turn NODE (user CRED) into a device.  TYPE is either
   S_IFBLK or S_IFCHR.  NP is locked.  */
error_t
netfs_attempt_mkdev (struct iouser *cred, struct node *np,
		     mode_t type, dev_t indexes)
{
  return EOPNOTSUPP;
}


/* Attempt to set the passive translator record for FILE to ARGZ (of
   length ARGZLEN) for user CRED. NP is locked.  */
error_t
netfs_set_translator (struct iouser *cred, struct node *np,
		      char *argz, size_t argzlen)
{
  return EOPNOTSUPP;
}


/* For locked node NODE with S_IPTRANS set in its mode, look up the
   name of its translator.  Store the name into newly malloced
   storage, and return it in *ARGZ; set *ARGZ_LEN to the total length.  */
error_t
netfs_get_translator (struct node *node, char **argz,
		      size_t *argz_len)
{
  return EOPNOTSUPP;
}


/* This should attempt a chflags call for the user specified by CRED
   on locked node NP, to change the flags to FLAGS.  */
error_t
netfs_attempt_chflags (struct iouser *cred, struct node *np,
		       int flags)
{
  return EOPNOTSUPP;
}


/* This should attempt a utimes call for the user specified by CRED on
   locked node NP, to change the atime to ATIME and the mtime to
   MTIME.  If ATIME or MTIME is null, then set to the current time.  */
error_t
netfs_attempt_utimes (struct iouser *cred, struct node *np,
		      struct timespec *atime, struct timespec *mtime)
{
  return 0;
}


/* This should attempt to set the size of the locked file NP (for user
   CRED) to SIZE bytes long.  */
error_t
netfs_attempt_set_size (struct iouser *cred, struct node *np,
			off_t size)
{
  return 0;
}


/* This should attempt to fetch filesystem status information for the
   remote filesystem, for the user CRED. NP is locked.  */
error_t
netfs_attempt_statfs (struct iouser *cred, struct node *np,
		      struct statfs *st)
{
  return EOPNOTSUPP;
}


/* This should sync the locked file NP completely to disk, for the
   user CRED.  If WAIT is set, return only after the sync is
   completely finished.  */
error_t
netfs_attempt_sync (struct iouser *cred, struct node *np,
		    int wait)
{
  return EOPNOTSUPP;
}


/* This should sync the entire remote filesystem.  If WAIT is set,
   return only after the sync is completely finished.  */
error_t
netfs_attempt_syncfs (struct iouser *cred, int wait)
{
  return 0;
}


/* Delete NAME in DIR (which is locked) for USER.  */
error_t
netfs_attempt_unlink (struct iouser *user, struct node *dir,
		      char *name)
{
  return EOPNOTSUPP;
}


/* Attempt to rename the directory FROMDIR to TODIR. Note that neither
   of the specific nodes are locked.  */
error_t
netfs_attempt_rename (struct iouser *user, struct node *fromdir,
		      char *fromname, struct node *todir, 
		      char *toname, int excl)
{
  return EOPNOTSUPP;
}


/* Attempt to create a new directory named NAME in DIR (which is
   locked) for USER with mode MODE. */
error_t
netfs_attempt_mkdir (struct iouser *user, struct node *dir,
		     char *name, mode_t mode)
{
  return EOPNOTSUPP;
}


/* Attempt to remove directory named NAME in DIR (which is locked) for
   USER.  */
error_t
netfs_attempt_rmdir (struct iouser *user, 
		     struct node *dir, char *name)
{
  return EOPNOTSUPP;
}


/* Create a link in DIR with name NAME to FILE for USER. Note that
   neither DIR nor FILE are locked. If EXCL is set, do not delete the
   target.  Return EEXIST if NAME is already found in DIR.  */
error_t
netfs_attempt_link (struct iouser *user, struct node *dir,
		    struct node *file, char *name, int excl)
{
  return EOPNOTSUPP;
}


/* Attempt to create an anonymous file related to DIR (which is
   locked) for USER with MODE.  Set *NP to the returned file upon
   success. No matter what, unlock DIR.  */
error_t
netfs_attempt_mkfile (struct iouser *user, struct node *dir,
		      mode_t mode, struct node **np)
{
  return EOPNOTSUPP;
}


/* (We don't use this function!)  Attempt to create a file named NAME
   in DIR (which is locked) for USER with MODE.  Set *NP to the new
   node upon return.  On any error, clear *NP.  *NP should be locked
   on success; no matter what, unlock DIR before returning.  */
error_t
netfs_attempt_create_file (struct iouser *user, struct node *dir,
			   char *name, mode_t mode, struct node **np)
{
  *np = 0;
  pthread_mutex_unlock (&dir->lock);
  return EOPNOTSUPP;
}


/* Read the contents of locked node NP (a symlink), for USER, into
   BUF.  */
error_t
netfs_attempt_readlink (struct iouser *user, struct node *np,
			char *buf)
{
  return EOPNOTSUPP;
}


/* libnetfs uses this functions once.  */
error_t
netfs_check_open_permissions (struct iouser *user, struct node *np,
			      int flags, int newnode)
{
  error_t err = 0;

  if (! err && (flags & O_READ))
    err = fshelp_access (&np->nn_stat, S_IREAD, user);
  if (! err && (flags & O_WRITE))
    err = fshelp_access (&np->nn_stat, S_IWRITE, user);
  if (! err && (flags & O_EXEC))
    err = fshelp_access (&np->nn_stat, S_IEXEC, user);
  return err;
}


/* Read from the locked file NP for user CRED starting at OFFSET and
   continuing for up to *LEN bytes.  Put the data at DATA.  Set *LEN
   to the amount successfully read upon return.  */
error_t
netfs_attempt_read (struct iouser *cred, struct node *np,
		    off_t offset, size_t *len, void *data)
{
  *len = 0;
  return 0;
}

/* Write to the locked file NP for user CRED starting at OFFSET and
   continuing for up to *LEN bytes from DATA.  Set *LEN to the amount
   successfully written upon return.  */
error_t
netfs_attempt_write (struct iouser *cred, struct node *np,
		     off_t offset, size_t *len, void *data)
{
  return EOPNOTSUPP;
}


/* Return the valid access types (bitwise OR of O_READ, O_WRITE, and
   O_EXEC) in *TYPES for locked file NP and user CRED.  */
error_t
netfs_report_access (struct iouser *cred, struct node *np,
		     int *types)
{
  *types = 0;
  if (fshelp_access (&np->nn_stat, S_IREAD, cred) == 0)
    *types |= O_READ;
  if (fshelp_access (&np->nn_stat, S_IWRITE, cred) == 0)
    *types |= O_WRITE;
  if (fshelp_access (&np->nn_stat, S_IEXEC, cred) == 0)
    *types |= O_EXEC;
  return 0;
}


/* Node NP has no more references; free all its associated storage.  */
void
netfs_node_norefs (struct node *np)
{
  struct node *dir = np->nn->dir;
  struct references result;

  node_uninstall (np);
  node_destroy (np);
  if (np->nn->flags & PROTOCOL_NODE)
    refcounts_demote (&dir->refcounts, &result);
}


/* Fill the array *DATA of size MAX_DATA_LEN with up to NUM_ENTRIES
   dirents from DIR (which is locked) starting with entry ENTRY for
   user CRED.  The number of entries in the array is stored in
   *DATA_ENTRIES and the number of bytes in *DATA_LEN.  If the
   supplied buffer is not large enough to hold the data, it should be
   grown.  */
error_t
netfs_get_dirents (struct iouser *cred, struct node *dir,
		   int first_entry, int num_entries, char **data,
		   mach_msg_type_number_t *data_len,
		   vm_size_t max_data_len, int *data_entries)
{
  error_t err;
  int count;
  size_t size = 0;		/* Total size of our return block.  */
  struct node *first_name, *nm;

  /* Add the length of a directory entry for NAME to SIZE and return true,
     unless it would overflow MAX_DATA_LEN or NUM_ENTRIES, in which case
     return false.  */
  int bump_size (const char *name)
    {
      if (num_entries == -1 || count < num_entries)
	{
	  size_t new_size = size + DIRENT_LEN (strlen (name));
	  if (max_data_len > 0 && new_size > max_data_len)
	    return 0;
	  size = new_size;
	  count++;
	  return 1;
	}
      else
	return 0;
    }

  /* Find the first entry, taking `.' and `..' into account.  */
  for (first_name = dir->nn->entries, count = 0;
       first_name && first_entry > count;
       first_name = first_name->next)
    count++;

  count = 0;

  /* Make space for the `.' and `..' entries.  */
  if (first_entry == 0)
    bump_size (".");
  if (first_entry <= 1)
    bump_size ("..");

  /* See how much space we need for the result.  */
  for (nm = first_name; nm; nm = nm->next)
    if (!bump_size (nm->nn->protocol->name))
      break;

  /* Allocate it.  */
  *data = mmap (0, size, PROT_READ|PROT_WRITE, MAP_ANON, 0, 0);
  err = ((void *) *data == (void *) -1) ? errno : 0;

  if (! err)
    /* Copy out the result.  */
    {
      char *p = *data;

      int add_dir_entry (const char *name, ino_t fileno, int type)
	{
	  if (num_entries == -1 || count < num_entries)
	    {
	      struct dirent hdr;
	      size_t name_len = strlen (name);
	      size_t sz = DIRENT_LEN (name_len);

	      if (sz > size)
		return 0;
	      else
		size -= sz;

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

      *data_len = size;
      *data_entries = count;

      count = 0;

      /* Add `.' and `..' entries.  */
      if (first_entry == 0)
	add_dir_entry (".", 2, DT_DIR);
      if (first_entry <= 1)
	add_dir_entry ("..", 2, DT_DIR);

      /* Fill in the real directory entries.  */
      for (nm = first_name; nm; nm = nm->next)
	if (!add_dir_entry (nm->nn->protocol->name, nm->nn->protocol->id,
			    DT_DIR))
	  break;
    }

  fshelp_touch (&dir->nn_stat, TOUCH_ATIME, maptime);
  return err;
}


/* argp option parser.  */
static error_t
parse_opts (int key, char *arg, struct argp_state *sate)
{
  return 0;
}

/* Main entry point.  */
int
main (int argc, char **argv)
{
  struct argp main_argp = { main_options, parse_opts,
			    NULL, argp_doc, NULL };
  mach_port_t bootstrap_port;
  error_t err;
  extern struct stat stat_default;

  /* Start the server.  */
  argp_parse (&main_argp, argc, argv, 0, 0, 0);
  task_get_bootstrap_port (mach_task_self (), &bootstrap_port);
  netfs_init ();
  ul_node = netfs_startup (bootstrap_port, 0);

  /* Initialize socketio.  */
  err = node_make_root_node (&netfs_root_node);
  if (err)
    error (EXIT_FAILURE, err, "cannot create root node");
  fsid = getpid ();
  
  {
    /* Here we adjust the root node permissions.  */
    struct stat ul_node_stat;
    err = io_stat (ul_node, &ul_node_stat);
    if (err)
      error (EXIT_FAILURE, err, "cannot stat underlying node");
    netfs_root_node->nn_stat = ul_node_stat;
    netfs_root_node->nn_stat.st_fsid = fsid;
    netfs_root_node->nn_stat.st_mode = S_IFDIR | (ul_node_stat.st_mode
						  & ~S_IFMT & ~S_ITRANS);

    /* If the underlying node isn't a directory, enhance the stat
       information, if needed.  */
    if (! S_ISDIR (ul_node_stat.st_mode))
      {
	if (ul_node_stat.st_mode & S_IRUSR)
	  netfs_root_node->nn_stat.st_mode |= S_IXUSR;
	if (ul_node_stat.st_mode & S_IRGRP)
	  netfs_root_node->nn_stat.st_mode |= S_IXGRP;
	if (ul_node_stat.st_mode & S_IROTH)
	  netfs_root_node->nn_stat.st_mode |= S_IXOTH;
      }
  }

  err = maptime_map (0, 0, &maptime);
  if (err)
    error (EXIT_FAILURE, err, "cannot map time");

  fshelp_touch (&netfs_root_node->nn_stat,
		TOUCH_ATIME|TOUCH_MTIME|TOUCH_CTIME,
		maptime);

  /* Here we initialize the default stat information for new
     nodes.  */
  stat_default.st_fstype = FSTYPE_MISC;
  stat_default.st_fsid = fsid;
  stat_default.st_ino = 1;	/* FIXME? */
  stat_default.st_gen = 0;
  stat_default.st_rdev = 0;
  stat_default.st_mode = 0;
  stat_default.st_nlink = 0;
  stat_default.st_uid = netfs_root_node->nn_stat.st_uid;
  stat_default.st_gid = netfs_root_node->nn_stat.st_gid;
  stat_default.st_size = 0;
  stat_default.st_blksize = 0;
  stat_default.st_blocks = 0;
  stat_default.st_author = netfs_root_node->nn_stat.st_author;

  err = open_socket_server (PF_INET, &socket_server);
  if (err)
    error (EXIT_FAILURE, err, "open_socket_server");

  err = protocol_register_default ();
  if (err)
    error (EXIT_FAILURE, err, "protocol_register_default");

  for (;;)
    netfs_server_loop ();

  /* Never reached.  */
  exit (0);
}
