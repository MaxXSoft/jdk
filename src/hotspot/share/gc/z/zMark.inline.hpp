/*
 * Copyright (c) 2015, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#ifndef SHARE_GC_Z_ZMARK_INLINE_HPP
#define SHARE_GC_Z_ZMARK_INLINE_HPP

#include "gc/z/zMark.hpp"

#include "gc/z/zAddress.inline.hpp"
#include "gc/z/zGeneration.inline.hpp"
#include "gc/z/zMarkStack.inline.hpp"
#include "gc/z/zMarkTerminate.inline.hpp"
#include "gc/z/zPage.inline.hpp"
#include "gc/z/zPageTable.inline.hpp"
#include "gc/z/zThreadLocalData.hpp"
#include "runtime/javaThread.hpp"
#include "utilities/debug.hpp"

// Marking before pushing helps reduce mark stack memory usage. However,
// we only mark before pushing in GC threads to avoid burdening Java threads
// with writing to, and potentially first having to clear, mark bitmaps.
//
// It's also worth noting that while marking an object can be done at any
// time in the marking phase, following an object can only be done after
// root processing has called ClassLoaderDataGraph::clear_claimed_marks(),
// since it otherwise would interact badly with claiming of CLDs.

template <bool resurrect, bool gc_thread, bool follow, bool finalizable>
inline void ZMark::mark_object(zaddress addr) {
  assert_is_oop(addr);

  ZPage* const page = _page_table->get(addr);
  if (page->is_allocating()) {
    // Already implicitly marked
    return;
  }

  const bool mark_before_push = gc_thread;
  bool inc_live = false;

  if (mark_before_push) {
    // Try mark object
    if (!page->mark_object(addr, finalizable, inc_live)) {
      // Already marked
      return;
    }
  } else {
    // Don't push if already marked
    if (page->is_object_marked(addr, finalizable)) {
      // Already marked
      return;
    }
  }

  if (resurrect) {
    _terminate.set_resurrected(true);
  }

  // Push
  ZMarkThreadLocalStacks* const stacks = ZThreadLocalData::mark_stacks(Thread::current(), _generation->id());
  ZMarkStripe* const stripe = _stripes.stripe_for_addr(untype(addr));
  ZMarkStackEntry entry(untype(ZAddress::offset(addr)), !mark_before_push, inc_live, follow, finalizable);

  assert(ZHeap::heap()->is_young(addr) == _generation->is_young(), "Phase/object mismatch");

  const bool publish = !gc_thread;
  stacks->push(&_stripes, stripe, &_terminate, entry, publish);
}

#endif // SHARE_GC_Z_ZMARK_INLINE_HPP
