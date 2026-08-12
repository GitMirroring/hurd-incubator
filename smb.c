/*
  Copyright (C) 2004, 2007, 2009 Free Software Foundation, Inc.
  Copyright (C) 2004, 2007, 2009 Giuseppe Scrivano.
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
#include "smb.h"

void
auth_data_fn (SMBCCTX *c, const char *server, const char *share, char *workgroup,
              int wgmaxlen, char *username, int unmaxlen, char *password,
              int pwmaxlen)
{
  if (strcmp (server, opts.server))
    {
      fprintf(stderr, "ERROR: server %s does not match what we wanted %s\n", server, opts.server);
      return;
    }

  strncpy (workgroup, opts.workgroup, wgmaxlen);
  strncpy (username, opts.username, unmaxlen);
  strncpy (password, opts.password, pwmaxlen);
}

void
init_smb (const char *min_proto)
{
  int ret;

  ctx = smbc_new_context();
  if (!ctx)
    error(EXIT_FAILURE, errno, "Failed to get new smbc context");

  smbc_setOptionDebugToStderr(ctx, 1);
  smbc_setDebug(ctx, 1);
  smbc_setOptionNoAutoAnonymousLogin(ctx, true);
  smbc_setOptionUseKerberos(ctx, 0);
  ret = smbc_setOptionProtocols(ctx, min_proto, "SMB3_11");
  if (!ret)
    error(EXIT_FAILURE, 1, "Cannot set minimum protocol version to %s", min_proto);

  smbc_setOptionPosixExtensions(ctx, true);
  ret = smbc_getOptionPosixExtensions(ctx);
  if (ret == false)
    fprintf(stderr, "Could not enable posix extensions, continuing\n");

  smbc_setUser(ctx, opts.username);
  smbc_setFunctionAuthDataWithContext(ctx, auth_data_fn);
  if (!smbc_getFunctionAuthDataWithContext(ctx))
    error(EXIT_FAILURE, 1, "Cannot set auth data function\n");

  ctx = smbc_init_context(ctx);
  if (!ctx)
    error(EXIT_FAILURE, errno, "Failed to init smbc context");

  smbc_set_context(ctx);
}
