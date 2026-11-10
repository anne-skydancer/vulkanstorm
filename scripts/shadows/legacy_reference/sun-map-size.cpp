//! Reference excerpt "sun-map-size" for scripts/shadows/check_legacy_shadows.py.
//! Source: indra/newview/pipeline.cpp (LLPipeline::allocateShadowBuffer) of Firestorm 7.2.5, upstream ref
//! upstream/Firestorm_7.2.5 at 4ac201fdc5 (identical at e3cd40d016), lines 1090-1091.
//! Linden Lab / Firestorm code, LGPL-2.1 (see LICENSE-LGPL-2.1 and doc/licensing.md); stored unchanged
//! as the oracle for the Legacy shadow method. Lines starting with //! are not part of the excerpt.
    U32 sun_shadow_map_width = BlurHappySize(resX, scale);
    U32 sun_shadow_map_height = BlurHappySize(resY, scale);
