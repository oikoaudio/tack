/*
 * wrapper_info.h
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** The factories clap-wrapper asks for (the VST3 and AU builds of Tack), or NULL for an id that isn't one. */
const void* tack_wrapperFactory(const char* id);

#ifdef __cplusplus
}
#endif
