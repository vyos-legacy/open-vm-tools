/*********************************************************
 * Copyright (C) 2000 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation version 2.1 and no later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the Lesser GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA.
 *
 *********************************************************/

/*********************************************************
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of VMware Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission of VMware Inc.
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
 *********************************************************/

/*********************************************************
 * The contents of this file are subject to the terms of the Common
 * Development and Distribution License (the "License") version 1.0
 * and no later version.  You may not use this file except in
 * compliance with the License.
 *
 * You can obtain a copy of the License at
 *         http://www.opensource.org/licenses/cddl1.php
 *
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 *********************************************************/

/* 
 * vmballoon.h: Definitions and macros for vmballoon driver.
 */

#ifndef	VMBALLOON_H
#define	VMBALLOON_H

#include "vm_basic_types.h"

#define BALLOON_NAME                    "vmmemctl"
#define BALLOON_NAME_VERBOSE            "VMware memory control driver"

#define BALLOON_POLL_PERIOD             1 /* sec */

/*
 * Page allocation flags
 */
typedef enum BalloonPageAllocType {
   BALLOON_PAGE_ALLOC_NOSLEEP = 0,
   BALLOON_PAGE_ALLOC_CANSLEEP = 1,
   BALLOON_PAGE_ALLOC_TYPES_NR,	// total number of alloc types
} BalloonPageAllocType;

/*
 * Types
 */

typedef struct {
   /* current status */
   uint32 nPages;
   uint32 nPagesTarget;

   /* adjustment rates */
   uint32 rateNoSleepAlloc;
   uint32 rateAlloc;
   uint32 rateFree;

   /* high-level operations */
   uint32 timer;

   /* primitives */
   uint32 primAlloc[BALLOON_PAGE_ALLOC_TYPES_NR];
   uint32 primAllocFail[BALLOON_PAGE_ALLOC_TYPES_NR];
   uint32 primFree;
   uint32 primErrorPageAlloc;
   uint32 primErrorPageFree;

   /* monitor operations */
   uint32 lock;
   uint32 lockFail;
   uint32 unlock;
   uint32 unlockFail;
   uint32 target;
   uint32 targetFail;
   uint32 start;
   uint32 startFail;
   uint32 guestType;
   uint32 guestTypeFail;
} BalloonStats;

/*
 * Operations
 */

Bool Balloon_Init(BalloonGuest guestType);
void Balloon_Cleanup(void);

void Balloon_QueryAndExecute(void);

const BalloonStats *Balloon_GetStats(void);

#endif	/* VMBALLOON_H */