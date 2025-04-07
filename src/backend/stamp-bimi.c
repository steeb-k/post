/*
 * Copyright 2024-2026 Jan-Michael Brummer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stamp-bimi.h"

#include <arpa/nameser.h>
#include <resolv.h>
#include <netdb.h>

char *
stamp_query_bimi_logo (const char *domain)
{
  g_autofree char *bimi = g_strdup_printf ("default._bimi.%s", domain);
  unsigned char answer[NS_PACKETSZ];
  ns_msg handle;
  ns_rr rr;
  int len = res_query (bimi, C_IN, T_TXT, answer, sizeof (answer));
  int count;

  g_debug ("%s: Request for %s", G_STRFUNC, domain);

  if (len < 0) {
    if (h_errno == NO_DATA) {
      g_debug ("Domain %s exists, but no BIMI TXT record", domain);
    } else if (h_errno == HOST_NOT_FOUND) {
      g_debug ("%s NXDOMAIN", domain);
    } else {
      g_debug ("%s DNS error: %s", domain, hstrerror (h_errno));
    }

    return NULL;
  }

  if (ns_initparse (answer, len, &handle) < 0) {
    g_debug ("ns_initparse");
    return NULL;
  }

  count = ns_msg_count (handle, ns_s_an);
  for (int i = 0; i < count; i++) {
    if (ns_parserr (&handle, ns_s_an, i, &rr) == 0) {
      const unsigned char *rdata = ns_rr_rdata (rr);
      int txt_len = rdata[0];
      char txt[256];
      char *logo;

      memcpy (txt, rdata + 1, txt_len);
      txt[txt_len] = '\0';

      logo = strstr (txt, "l=");
      if (logo && *logo) {
        char *end = strstr (logo + 1, ";");

        if (end && *end) {
          char *url = g_strndup (logo + 2, end - logo - 2);
          return url;
        }
      }
    }
  }

  return NULL;
}
