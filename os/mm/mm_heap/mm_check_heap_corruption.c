/****************************************************************************
 *
 * Copyright 2021 Samsung Electronics All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND,
 * either express or implied. See the License for the specific
 * language governing permissions and limitations under the License.
 *
 ****************************************************************************/
/****************************************************************************
 * mm/mm_heap/mm_check_heap_corruption.c
 *
 *   Copyright (C) 2007, 2009, 2013-2014 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include <tinyara/config.h>
#include <tinyara/arch.h>
#include <tinyara/mm/mm.h>
#if defined(CONFIG_DEBUG_MM_HEAPINFO)  && (CONFIG_TASK_NAME_SIZE > 0)
#include <sys/prctl.h>
#endif
#include <debug.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/
#define MM_PREV_NODE_SIZE(x)		((x)->preceding & ~MM_ALLOC_BIT)
#define IS_ALLOCATED_NODE(x)		((x)->preceding & MM_ALLOC_BIT)
#define IS_FREE_NODE(x)			(!IS_ALLOCATED_NODE(x))

/****************************************************************************
 * Public data
 ****************************************************************************/
enum node_type_e {
	TYPE_CORRUPTED,
	TYPE_OVERFLOWED,
};

typedef enum node_type_e node_type_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void dump_node(struct mm_allocnode_s *node, node_type_t type)
{
#if defined(CONFIG_DEBUG_MM_HEAPINFO)  && (CONFIG_TASK_NAME_SIZE > 0)
	char myname[CONFIG_TASK_NAME_SIZE + 1];
#endif

	if (type == TYPE_CORRUPTED) {
		dbg("CORRUPTED NODE: addr = 0x%08x size = %u preceding size = %u\n", node, node->size, MM_PREV_NODE_SIZE(node));
	} else if (type == TYPE_OVERFLOWED) {
		dbg("OVERFLOWED NODE: addr = 0x%08x size = %u type = %c\n", node, node->size, IS_ALLOCATED_NODE(node) ? 'A' : 'F');
	}
#ifdef CONFIG_DEBUG_MM_HEAPINFO
#if CONFIG_TASK_NAME_SIZE > 0
	if (prctl(PR_GET_NAME, myname, node->pid) == OK) {
		dbg("Node owner pid = %u (%s), allocated by code at addr = 0x%08x\n", node->pid, myname, node->alloc_call_addr);
	} else {
		dbg("Node owner pid = %u (Exited Task), allocated by code at addr = 0x%08x\n", node->pid, node->alloc_call_addr);
	}
#else
	dbg("Node owner pid = %u, allocated by code at addr = 0x%08x\n", node->pid, node->alloc_call_addr);
#endif
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mm_check_heap_corruption
 *
 * Description:
 *   This function walk through heap and prints information about corrupt node.
 ****************************************************************************/
int mm_check_heap_corruption(struct mm_heap_s *heap)
{
	struct mm_allocnode_s *node;
	struct mm_allocnode_s *prev;
	struct mm_allocnode_s *next;
#if CONFIG_KMM_REGIONS > 1
	int region;
#else
#define region 0
#endif

	DEBUGASSERT(heap);

	/* Visit each region */

#if CONFIG_KMM_REGIONS > 1
	for (region = 0; region < heap->mm_nregions; region++)
#endif
	{
		/* Visit each node in the region
		 * Retake the semaphore for each region to reduce latencies
		 */

#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
		if (!up_interrupt_context())
#endif
		{
			mm_takesemaphore(heap);
		}

		prev = NULL;
		node = heap->mm_heapstart[region];
		next = (struct mm_allocnode_s *)((char *)node + node->size);

		for (; node < heap->mm_heapend[region]; prev = node, node = next, next = (struct mm_allocnode_s *)((char *)next + next->size)) {
			if (prev && prev->size != MM_PREV_NODE_SIZE(node)) {
				dbg("#########################################################################################\n");
				dbg("ERROR: Heap node corruption detected\n");
				dump_node(prev, TYPE_OVERFLOWED);
				dump_node(node, TYPE_CORRUPTED);
				dbg("#########################################################################################\n");
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
				if (!up_interrupt_context())
#endif
				{
					mm_givesemaphore(heap);
				}
				return -1;
			} else if (node->size != MM_PREV_NODE_SIZE(next)) {
				dbg("#########################################################################################\n");
				dbg("ERROR: Heap node corruption detected.\n");
				dbg("=========================================================================================\n");
				dbg("Possible corruption scenario 1:\n");
				dbg("=========================================================================================\n");
				dump_node(prev, TYPE_OVERFLOWED);
				dump_node(node, TYPE_CORRUPTED);
				dbg("=========================================================================================\n");
				dbg("Possible corruption scenario 2:\n");
				dbg("=========================================================================================\n");
				dump_node(node, TYPE_OVERFLOWED);
				dump_node(next, TYPE_CORRUPTED);
				dbg("#########################################################################################\n");
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
				if (!up_interrupt_context())
#endif
				{
					mm_givesemaphore(heap);
				}
				return -1;
			} else if (IS_FREE_NODE(node)) {
				if ((((struct mm_freenode_s *)node)->blink && ((struct mm_freenode_s *)node)->blink->flink != ((struct mm_freenode_s *)node))) {
					dbg("#########################################################################################\n");
					dbg("ERROR: Heap node corruption detected in free node list\n");
					dump_node(prev, TYPE_OVERFLOWED);
					dump_node(node, TYPE_CORRUPTED);
					dbg("Corrupted node blink(0x%08x) and prev node flink(0x%08x) do not match\n", ((struct mm_freenode_s *)node)->blink,
							((struct mm_freenode_s *)node)->blink->flink);
					dbg("#########################################################################################\n");
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
					if (!up_interrupt_context())
#endif
					{
						mm_givesemaphore(heap);
					}
					return -1;
				} else if (((struct mm_freenode_s *)node)->flink && ((struct mm_freenode_s *)node)->flink->blink != ((struct mm_freenode_s *)node)) {
					dbg("#########################################################################################\n");
					dbg("ERROR: Heap node corruption detected in free node list\n");
					dump_node(prev, TYPE_OVERFLOWED);
					dump_node(node, TYPE_CORRUPTED);
					dbg("Corrupted node flink(0x%08x) and next node blink(0x%08x) do not match\n", ((struct mm_freenode_s *)node)->flink,
							((struct mm_freenode_s *)node)->flink->blink);
					dbg("#########################################################################################\n");
#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
					if (!up_interrupt_context())
#endif
					{
						mm_givesemaphore(heap);
					}
					return -1;
				}
			}
		}

#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
		if (!up_interrupt_context())
#endif
		{
			mm_givesemaphore(heap);
		}
	}

	return 0;
}
