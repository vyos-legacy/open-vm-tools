/* **********************************************************
 * Copyright 2007 VMware, Inc.  All rights reserved.
 * **********************************************************/

/*
 * subr.c --
 *
 *	Subroutines for the VMBlock filesystem on FreeBSD.
 */


/*-
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)null_subr.c	8.7 (Berkeley) 5/14/95
 *
 * $FreeBSD: src/sys/fs/nullfs/null_subr.c,v 1.48.2.1 2006/03/13 03:05:17 jeff Exp $
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/file.h>

#include "vmblock_k.h"
#include "block.h"

#define LOG2_SIZEVNODE  8		/* log2(sizeof struct vnode) */
#define	NVMBLOCKCACHE   16              /* Number of hash buckets/chains */


/*
 * Local data
 */

/*
 * VMBlock layer cache:
 *    Each cache entry holds a reference to the lower vnode along with a
 *    pointer to the alias vnode.  When an entry is added the lower vnode
 *    is VREF'd.  When the alias is removed the lower vnode is vrele'd.
 */

#define	VMBLOCK_NHASH(vp) \
	(&nodeHashTable[(((uintptr_t)vp)>>LOG2_SIZEVNODE) & nodeHashMask])

/*
 * See hashinit(9).
 */
static LIST_HEAD(nodeHashHead, VMBlockNode) *nodeHashTable;
static u_long nodeHashMask;
static struct mtx hashMutex;

static MALLOC_DEFINE(M_VMBLOCKFSHASH, "VMBlockFS hash", "VMBlockFS hash table");
MALLOC_DEFINE(M_VMBLOCKFSNODE, "VMBlockFS node", "VMBlockFS vnode private part");

/* Defined for quick access to temporary pathname buffers. */
uma_zone_t VMBlockPathnameZone;


/*
 * Local functions
 */

static struct vnode * VMBlockHashGet(struct mount *, struct vnode *);
static struct vnode * VMBlockHashInsert(struct mount *, struct VMBlockNode *);


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockInit --
 *
 *      Initialize VMBlock file system.  Called when module first loaded into
 *      the kernel.
 *
 * Results:
 *      Zero.
 *
 * Side effects:
 *      None.
 *
 * Original comments:
 *      Initialise cache headers
 *
 *-----------------------------------------------------------------------------
 */

int
VMBlockInit(struct vfsconf *vfsp)       // ignored
{
   VMBLOCKDEBUG("VMBlockInit\n");      /* printed during system boot */
   nodeHashTable = hashinit(NVMBLOCKCACHE, M_VMBLOCKFSHASH, &nodeHashMask);
   mtx_init(&hashMutex, "vmblock-hs", NULL, MTX_DEF);
   VMBlockPathnameZone = uma_zcreate("VMBlock", MAXPATHLEN, NULL, NULL, NULL,
                                     NULL, UMA_ALIGN_PTR, 0);

   /*
    * See block describing VMBlockFileOps in vnops.c.
    */
   VMBlockFileOps.fo_stat = vnops.fo_stat;
   VMBlockFileOps.fo_flags = vnops.fo_flags;

   BlockInit();
   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockUninit --
 *
 *      Clean up when module is unloaded.
 *
 * Results:
 *      Zero always.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

int
VMBlockUninit(struct vfsconf *vfsp)     // ignored
{
   mtx_destroy(&hashMutex);
   free(nodeHashTable, M_VMBLOCKFSHASH);
   BlockCleanup();
   uma_zdestroy(VMBlockPathnameZone);
   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockHashGet --
 *
 *      "Return a VREF'ed alias for lower vnode if already exists, else 0.
 *      Lower vnode should be locked on entry and will be left locked on exit."
 *
 * Results:
 *      Pointer to upper layer/alias vnode if lowervp found, otherwise NULL.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static struct vnode *
VMBlockHashGet(struct mount *mp,        // IN: vmblock file system information
               struct vnode *lowervp)   // IN: lower vnode to search for
{
   struct thread *td = curthread;   /* XXX */
   struct nodeHashHead *hd;
   struct VMBlockNode *a;
   struct vnode *vp;
   int error;

   ASSERT_VOP_LOCKED(lowervp, "hashEntryget");

   /*
    * Find hash base, and then search the (two-way) linked list looking
    * for a VMBlockNode structure which is referencing the lower vnode.
    * If found, the increment the VMBlockNode reference count (but NOT the
    * lower vnode's VREF counter).
    */
   hd = VMBLOCK_NHASH(lowervp);
   mtx_lock(&hashMutex);
   LIST_FOREACH(a, hd, hashEntry) {
      if (a->lowerVnode == lowervp && VMBTOVP(a)->v_mount == mp) {
         vp = VMBTOVP(a);
         VI_LOCK(vp);
         mtx_unlock(&hashMutex);
         /*
          * We need to clear the OWEINACT flag here as this may lead vget()
          * to try to lock our vnode which is already locked via lowervp.
          */
         vp->v_iflag &= ~VI_OWEINACT;
         error = vget(vp, LK_INTERLOCK, td);
         /*
          * Since we have the lower node locked the nullfs node can not be
          * in the process of recycling.  If it had been recycled before we
          * grabed the lower lock it would not have been found on the hash.
          */
         if (error) {
            panic("hashEntryget: vget error %d", error);
         }
         return vp;
      }
   }
   mtx_unlock(&hashMutex);
   return NULLVP;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockHashInsert --
 *
 *      "Act like VMBlockHashGet, but add passed VMBlockNode to hash if no
 *      existing node found."
 *
 * Results:
 *      Referenced, locked alias vnode if entry already in hash.  Otherwise
 *      NULLVP.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

static struct vnode *
VMBlockHashInsert(struct mount *mp,             // IN: VMBlock file system info
                  struct VMBlockNode *xp)       // IN: node to insert into hash
{
   struct thread *td = curthread;   /* XXX */
   struct nodeHashHead *hd;
   struct VMBlockNode *oxp;
   struct vnode *ovp;
   int error;

   hd = VMBLOCK_NHASH(xp->lowerVnode);
   mtx_lock(&hashMutex);
   LIST_FOREACH(oxp, hd, hashEntry) {
      if (oxp->lowerVnode == xp->lowerVnode && VMBTOVP(oxp)->v_mount == mp) {
         /*
          * See hashEntryget for a description of this
          * operation.
          */
         ovp = VMBTOVP(oxp);
         VI_LOCK(ovp);
         mtx_unlock(&hashMutex);
         ovp->v_iflag &= ~VI_OWEINACT;
         error = vget(ovp, LK_INTERLOCK, td);
         if (error) {
            panic("hashEntryins: vget error %d", error);
         }
         return ovp;
      }
   }
   LIST_INSERT_HEAD(hd, xp, hashEntry);
   mtx_unlock(&hashMutex);
   return NULLVP;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockHashRem --
 *
 *      Remove a VMBlockNode from the hash.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBlockHashRem(struct VMBlockNode *xp)  // IN: node to remove
{
   mtx_lock(&hashMutex);
   LIST_REMOVE(xp, hashEntry);
   mtx_unlock(&hashMutex);
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockNodeGet --
 *
 *      Return a VMBlockNode mapped to the given lower layer vnode.
 *
 * Results:
 *      Zero on success, an appropriate system error otherwise.
 *
 * Side effects:
 *      None.
 *
 * Original function comment:
 *
 *	Make a new or get existing nullfs node.  Vp is the alias vnode,
 *	lowervp is the lower vnode.
 *
 *	The lowervp assumed to be locked and having "spare" reference. This
 *	routine vrele lowervp if nullfs node was taken from hash. Otherwise it
 *	"transfers" the caller's "spare" reference to created nullfs vnode.
 *
 *-----------------------------------------------------------------------------
 */

int
VMBlockNodeGet(struct mount *mp,        // IN: VMBlock fs info
               struct vnode *lowervp,   // IN: lower layer vnode
               struct vnode **vpp,      // OUT: upper layer/alias vnode
               struct vnode *dvp)       // IN: Pointer to vmblock layer parent,
                                        //     if any
{
   struct VMBlockNode *xp;
   struct vnode *vp;
   int error;

   /* Lookup the hash firstly */
   *vpp = VMBlockHashGet(mp, lowervp);
   if (*vpp != NULL) {
      vrele(lowervp);
      return 0;
   }

   /*
    * We do not serialize vnode creation, instead we will check for duplicates
    * later, when adding new vnode to hash.
    *
    * Note that duplicate can only appear in hash if the lowervp is locked
    * LK_SHARED.
    */

   /*
    * Do the malloc before the getnewvnode since doing so afterward might
    * cause a bogus v_data pointer to get dereferenced elsewhere if malloc
    * should block.
    */
   xp = malloc(sizeof *xp, M_VMBLOCKFSNODE, M_WAITOK|M_ZERO);

   error = getnewvnode("vmblock", mp, &VMBlockVnodeOps, &vp);
   if (error) {
      free(xp, M_VMBLOCKFSNODE);
      return error;
   }

   xp->backVnode = vp;
   xp->lowerVnode = lowervp;
   vp->v_type = lowervp->v_type;
   vp->v_data = xp;
   vp->v_vnlock = lowervp->v_vnlock;
   if (vp->v_vnlock == NULL) {
      panic("VMBlockNodeGet: Passed a NULL vnlock.\n");
   }

   /*
    * Atomically insert our new node into the hash or vget existing if
    * someone else has beaten us to it.
    *
    * ETA:  If a hash entry already exists, we'll be stuck with an orphaned
    * vnode and associated VMBlockNode.  By vrele'ng this vp, it'll be reclaimed
    * by the OS later.  That same process will take care of freeing the
    * VMBlockNode, too.
    */
   *vpp = VMBlockHashInsert(mp, xp);
   if (*vpp != NULL) {
      vrele(lowervp);
      vp->v_vnlock = &vp->v_lock;
      xp->lowerVnode = NULL;
      vrele(vp);
      return 0;
   } else {
      xp->parentVnode = dvp;
   }
   *vpp = vp;

   return 0;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockSetNodeName --
 *
 *      If not already set, assign a VMBlockNode its lower layer vnode's
 *      path name.  (E.g., if /tmp/VMwareDND is remounted to /var/run/vmblock,
 *      the root VMBlockNode will have a path name of "/tmp/VMwareDND".)
 *
 *      In the interest of saving a little memory, for all vnodes except the
 *      mountpoint, we only copy the first pathname component (i.e., up to the
 *      first slash).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      This function may not be called with mutexes held.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBlockSetNodeName(struct vnode *vp,    // IN: assignee vnode
                   const char *name)    // IN: name assigned
{
   VMBlockNode *blockNode;
   char *slash;

   KASSERT(vp != NULL, ("vp is NULL"));
   KASSERT(name != NULL, ("name is NULL"));

   blockNode = VPTOVMB(vp);
   
   /*
    * We assume that blocks are only placed on directories, not files,
    * and by doing so define the relationship between VMBlockNodes and pathnames
    * is 1:1.  (One cannot hardlink directories.)  As such, renaming a node is
    * pointless, so we just return if this node is already named.
    */
   if (blockNode->componentName != NULL) {
      return;
   }

   /*
    * If given an absolute path or a lookup's final pathname component,
    * copy until the end of the string.  Otherwise, copy only until
    * the first slash is found. 
    */
   slash = index(name, '/');

   if ((name[0] == '/') || (slash == NULL)) {
      blockNode->componentSize = strlen(name) + 1;
   } else { 
      blockNode->componentSize = (slash - name) + 1;
   }

   /*
    * Finally allocate & copy buffer.
    */
   blockNode->componentName = malloc(blockNode->componentSize,
                                     M_VMBLOCKFSNODE, M_WAITOK);
   strlcpy(blockNode->componentName, name, blockNode->componentSize);
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockBuildBlockName --
 *
 *      Given a leaf vnode, compile and return a string representing its
 *      entire pathname.
 *
 *      After grabbing a buffer from a pathname zone/slab, we copy the
 *      leaf's component name to the end of the buffer.  Then we look up
 *      its parent and prepend the parent's name in the buffer, and so on
 *      until we reach the filesystem mountpoint.
 *
 * Results:
 *      Pointer to C-string pathname on success, NULL otherwise.
 *
 * Side effects:
 *      The returned string must be destroyed with VMBlockDestroyBlockName.
 *      Also, the caller must not be holding any mutexes.  (uma_zalloc is
 *      allowed to sleep.)
 *
 *-----------------------------------------------------------------------------
 */

char *
VMBlockBuildBlockName(struct vnode *vp) // IN: leaf vnode
{
   VMBlockNode *xp;
   unsigned int written = 0;
   char *buf, *tstart;

   buf = uma_zalloc(VMBlockPathnameZone, M_WAITOK);
   tstart = &buf[MAXPATHLEN];

   /*
    * Stopping after processing the root vnode (no parent):
    *   - Decrement copy destination pointer by the size of the current
    *     componentName's buffer.
    *   - Memcpy componentName to destination pointer.  (Memcpy will include
    *     terminating NUL.)
    *   - Replace each component's terminator with a pathname delimiter ('/').
    *     (For simplicity, even the leaf component is terminated with a '/', 
    *     but the final built string is manually terminated after this loop.)
    *
    * This function and its dependent VMBlockVnode::parentVnode member assume
    * the following:
    *   - We expect only to be called in VMBlockVopLookup with vp locked.
    *     As such, vp is locked, and all its parent directories up to the
    *     VMBlock filesystem root are also locked.  This means we don't have to
    *     worry about VMBlock vnodes disappearing out from under us.
    */
   do {
      xp = VPTOVMB(vp);
      vp = xp->parentVnode;

      /* Note: componentSize includes terminator. */
      tstart -= xp->componentSize;
      written += xp->componentSize;
      if (written > MAXPATHLEN) {
         Warning("%s: name too long (%u bytes)\n", __func__, written);
         uma_zfree(VMBlockPathnameZone, buf);
         return NULL;
      }

      /* Copy & reterminate. */
      memcpy(tstart, xp->componentName, xp->componentSize);
      tstart[xp->componentSize - 1] = '/';
   } while (vp != NULL);

   /* Reterminate string. */
   buf[MAXPATHLEN - 1] = '\0';

   /*
    * Since we copied components from leaf to root, the start of the pathname
    * string could be anywhere in the buffer.  To keep things simple for
    * callers, just move it to the beginning of the buffer.
    *
    * Don't worry!  Bcopy() works with overlapping buffers, and "written"
    * includes the NUL terminator.
    */
   bcopy(tstart, buf, written);
   return buf;
}


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockDestroyBlockName --
 *
 *      Free a name compiled by VMBlockBuildBlockName.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

void
VMBlockDestroyBlockName(char *blockName)        // IN: name to free
{
   uma_zfree(VMBlockPathnameZone, blockName);
}


#ifdef DIAGNOSTIC                               /* if (DIAGNOSTIC) { */

#ifdef KDB                                      /* if (KDB) { */
# define        VMBlockCheckVp_barrier	1
#else
# define        VMBlockCheckVp_barrier	0
#endif                                          /* }    */


/*
 *-----------------------------------------------------------------------------
 *
 * VMBlockCheckVp --
 *
 *      Sanity-checking intermediary used for debugging.  When module is
 *      compiled with FreeBSD macro "DIAGNOSTIC", every instance of
 *      VMBVPTOLOWERVP() calls this function to test vnodes' and VMBlockNodes'
 *      values, printing diagnostic information before panicing.  If the kernel
 *      debugger (KDB) is enabled, then this function will break to the debugger
 *      before a panic.
 *
 * Results:
 *      Valid pointer to a VMBlockNode's lower vnode.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */

struct vnode *
VMBlockCheckVp(vp, fil, lno)
	struct vnode *vp;
	char *fil;
	int lno;
{
   struct VMBlockNode *a = VPTOVMB(vp);
#ifdef notyet
   /*
    * Can't do this check because vop_reclaim runs
    * with a funny vop vector.
    */
   if (vp->v_op != null_vnodeop_p) {
      printf ("VMBlockCheckVp: on non-null-node\n");
      while (VMBlockCheckVp_barrier) /*WAIT*/ ;
      panic("VMBlockCheckVp");
   };
#endif
   if (a->lowerVnode == NULLVP) {
      /* Should never happen */
      int i; u_long *p;
      printf("vp = %p, ZERO ptr\n", (void *)vp);
      for (p = (u_long *) a, i = 0; i < 8; i++) {
         printf(" %lx", p[i]);
      }
      printf("\n");
      /* wait for debugger */
      while (VMBlockCheckVp_barrier) /*WAIT*/ ;
      panic("VMBlockCheckVp");
   }
   if (vrefcnt(a->lowerVnode) < 1) {
      int i; u_long *p;
      printf("vp = %p, unref'ed lowervp\n", (void *)vp);
      for (p = (u_long *) a, i = 0; i < 8; i++) {
         printf(" %lx", p[i]);
      }
      printf("\n");
      /* wait for debugger */
      while (VMBlockCheckVp_barrier) /*WAIT*/ ;
      panic ("null with unref'ed lowervp");
   };
#ifdef notyet
   printf("null %x/%d -> %x/%d [%s, %d]\n", VMBTOVP(a), vrefcnt(VMBTOVP(a)),
      a->lowerVnode, vrefcnt(a->lowerVnode), fil, lno);
#endif
   return a->lowerVnode;
}
#endif                                          /* } [DIAGNOSTIC] */
