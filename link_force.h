/* link_force.h -- a C function to atomically link, replacing the destination
 * Copyright (C) 2002 Matthias Andree

     This program is free software; you can redistribute it and/or
     modify it under the terms of version 2 of the GNU General Public
     License as published by the Free Software Foundation.

     This program is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
     02111-1307  USA

     As a special exception, the "leafnode" package shall be exempt from
     clause 2b of the license (the infiltration clause).
 */

#ifndef LINK_FORCE_H
#define LINK_FORCE_H
int link_force(const char *from, const char *to);
#endif
