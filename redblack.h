/*
   Redblack balanced tree algorithm
   Copyright (C) Damian Ivereigh 2000

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version. See the file COPYING for details.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Header file for redblack.c, should be included by any code that 
** uses redblack.c since it defines the functions 
*/ 
 
/* Stop multiple includes */
#ifndef _REDBLACK_H

#ifndef RB_CUSTOMIZE
/*
 * Without customization, the data member in the tree nodes is a void
 * pointer, and you need to pass in a comparison function to be
 * applied at runtime.  With customization, you specify the data type
 * as the macro RB_ENTRY(data_t) (has to be a macro because compilers
 * gag on typdef void) and the name of the compare function as the
 * value of the macro RB_CMP. Because the comparison function is
 * compiled in, RB_CMP only needs to take two arguments.  If your
 * content type is not a pointer, define INLINE to get direct access.
 */
#define rbdata_t	void
#define RB_CMP(s, t, e)	(*rbinfo->rb_cmp)(s, t, e)
#undef RB_INLINE
#define RB_ENTRY(name)	rb##name
#endif /* RB_CUSTOMIZE */

#ifndef RB_STATIC
#define RB_STATIC
#endif

/* Modes for rblookup */
#define RB_NONE -1	    /* None of those below */
#define RB_LUEQUAL 0	/* Only exact match */
#define RB_LUGTEQ 1		/* Exact match or greater */
#define RB_LULTEQ 2		/* Exact match or less */
#define RB_LULESS 3		/* Less than key (not equal to) */
#define RB_LUGREAT 4	/* Greater than key (not equal to) */
#define RB_LUNEXT 5		/* Next key after current */
#define RB_LUPREV 6		/* Prev key before current */
#define RB_LUFIRST 7	/* First key in index */
#define RB_LULAST 8		/* Last key in index */

/* For rbwalk - pinched from search.h */
typedef enum
{
  preorder,
  postorder,
  endorder,
  leaf
}
VISIT;

struct RB_ENTRY(lists) { 
const struct RB_ENTRY(node) *rootp; 
const struct RB_ENTRY(node) *nextp; 
}; 
 
#define RBLIST struct RB_ENTRY(lists) 

struct RB_ENTRY(tree) {
#ifndef RB_CUSTOMIZE
		/* comparison routine */
int (*rb_cmp)(const void *, const void *, const void *);
		/* config data to be passed to rb_cmp */
const void *rb_config;
		/* root of tree */
#endif /* RB_CUSTOMIZE */
struct RB_ENTRY(node) *rb_root;
};

#ifndef RB_CUSTOMIZE
RB_STATIC struct RB_ENTRY(tree) *rbinit(int (*)(const void *, const void *, const void *),
		 const void *);
#else
RB_STATIC struct RB_ENTRY(tree) *RB_ENTRY(init)(void);
#endif /* RB_CUSTOMIZE */

#ifndef no_delete
RB_STATIC const RB_ENTRY(data_t) *RB_ENTRY(delete)(const RB_ENTRY(data_t) *, struct RB_ENTRY(tree) *);
#endif

#ifndef no_find
RB_STATIC const RB_ENTRY(data_t) *RB_ENTRY(find)(const RB_ENTRY(data_t) *, struct RB_ENTRY(tree) *);
#endif

#ifndef no_lookup
RB_STATIC const RB_ENTRY(data_t) *RB_ENTRY(lookup)(int, const RB_ENTRY(data_t) *, struct RB_ENTRY(tree) *);
#endif

#ifndef no_search
RB_STATIC const RB_ENTRY(data_t) *RB_ENTRY(search)(const RB_ENTRY(data_t) *, struct RB_ENTRY(tree) *);
#endif

#ifndef no_destroy
RB_STATIC void RB_ENTRY(destroy)(struct RB_ENTRY(tree) *);
#endif

#ifndef no_walk
RB_STATIC void RB_ENTRY(walk)(const struct RB_ENTRY(tree) *,
		void (*)(const RB_ENTRY(data_t) *, const VISIT, const int, void *),
		void *); 
#endif

#ifndef no_readlist
RB_STATIC RBLIST *RB_ENTRY(openlist)(const struct RB_ENTRY(tree) *); 
RB_STATIC const RB_ENTRY(data_t) *RB_ENTRY(readlist)(RBLIST *); 
RB_STATIC void RB_ENTRY(closelist)(RBLIST *); 
#endif

/* Some useful macros */
#define rbmin(rbinfo) RB_ENTRY(lookup)(RB_LUFIRST, NULL, (rbinfo))
#define rbmax(rbinfo) RB_ENTRY(lookup)(RB_LULAST, NULL, (rbinfo))

#define _REDBLACK_H
#endif /* _REDBLACK_H */
