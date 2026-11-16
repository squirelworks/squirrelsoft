/*
 * Replay stream for Flight Lab / HIL bench UI.
 * See docs/replay-hook.md — Soft emits; consumers ingest line-oriented REPLAY v1.
 */
#ifndef BM_REPLAY_H
#define BM_REPLAY_H

#include "bm_frame.h"

/* Write REPLAY v1 for every frame in cap into a malloc'd string (*out). */
int bm_replay_build(const BmCapture *cap, char **out);

#endif
