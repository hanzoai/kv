/*
 * Copyright (c) KV Contributors
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __KV_TLS_H
#define __KV_TLS_H

/* TLS reload functions - only available when TLS is built-in, not as a module */
#if defined(USE_OPENSSL) && USE_OPENSSL == 1 /* BUILD_YES */
void tlsReconfigureIfNeeded(void);
void tlsApplyPendingReload(void);
void tlsConfigureAsync(void);
#endif

#endif /* __KV_TLS_H */
