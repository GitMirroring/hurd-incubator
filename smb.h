/*
  Copyright (C) 2004, 2007 Free Software Foundation, Inc.
  Copyright (C) 2004, 2007 Giuseppe Scrivano.
  Written by Giuseppe Scrivano <gscrivano@gnu.org>
   
  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 3, or (at
  your option) any later version.
  
  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define _GNU_SOURCE 1
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <maptime.h>
#include <errno.h>
#include <error.h>
#include <argp.h>
#include <hurd/netfs.h>
#include <libsmbclient.h>

struct smb_opts
{
  char *server;
  char *share;
  char *workgroup;
  char *username;
  char *password;
  char *min_proto;
};
extern struct smb_opts opts;
extern SMBCCTX *ctx;

extern void init_smb (const char *min_proto);
