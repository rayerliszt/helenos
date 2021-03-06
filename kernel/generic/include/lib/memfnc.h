/*
 * Copyright (c) 2011 Martin Decky
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup generic
 * @{
 */
/** @file
 */

#ifndef KERN_LIB_MEMFNC_H_
#define KERN_LIB_MEMFNC_H_

#include <stddef.h>
#include <cc.h>

#ifdef CONFIG_LTO
#define DO_NOT_DISCARD ATTRIBUTE_USED
#else
#define DO_NOT_DISCARD
#endif

extern void *memset(void *, int, size_t)
    __attribute__((nonnull(1)))
    ATTRIBUTE_OPTIMIZE("-fno-tree-loop-distribute-patterns") DO_NOT_DISCARD;
extern void *memcpy(void *, const void *, size_t)
    __attribute__((nonnull(1, 2)))
    ATTRIBUTE_OPTIMIZE("-fno-tree-loop-distribute-patterns") DO_NOT_DISCARD;
extern int memcmp(const void *, const void *, size_t len)
    __attribute__((nonnull(1, 2)))
    ATTRIBUTE_OPTIMIZE("-fno-tree-loop-distribute-patterns") DO_NOT_DISCARD;

#define alloca(size) __builtin_alloca((size))

#endif

/** @}
 */
