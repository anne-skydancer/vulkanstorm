// Offline qualification of the production conversation controls. LGPL-2.1.
#ifndef VS_NATIVE_IM_REPLAY_H
#define VS_NATIVE_IM_REPLAY_H
class VSNativeSession;
class LLSD;
class LLMessageSystem;
void vs_native_im_replay_economy_request(LLMessageSystem* message, void** context);
bool vs_native_im_replay_nearby_request(LLMessageSystem* message);
bool vs_native_im_replay_tick(VSNativeSession& session, LLSD& evidence);
bool vs_native_im_replay_expired();
#endif
