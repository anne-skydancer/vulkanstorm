// Native rendering peer for the existing snapshot controller. LGPL-2.1.
#pragma once
#include "llsnapshotmodel.h"
class LLViewerWindow;
class LLImageRaw;
bool vs_native_snapshot(LLViewerWindow&, LLImageRaw&, int width, int height, bool keepAspect,
                        bool showUI, bool showHUD, bool noPost, bool showBalance,
                        LLSnapshotModel::ESnapshotLayerType, int maximumSize);

// Close snapshot controllers before their tool and character owners retire.
void vs_retire_snapshot_floaters();

bool vs_native_simple_snapshot(LLViewerWindow&,LLImageRaw&,int width,int height,int passes);
