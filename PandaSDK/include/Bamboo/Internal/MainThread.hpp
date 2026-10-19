#pragma once

namespace Bamboo::Internal {

// Records the calling thread as the script-module main thread. Called once when the
// plugin is loaded (pandaLoadScriptBundle runs on Panda's main thread).
void registerMainThread();

bool isMainThread();

// Guards a main-thread-only Bamboo API. Returns true if it is safe to proceed.
//
// Most of the Bamboo API touches World/Entity/components/assets/renderer/UI through
// the host and must only be called on the main thread. A background job's work()
// runs on a PandaAsync worker; such results must be applied in the job's complete()
// callback, which runs on the main thread.
//
// On a violation this never touches a main-thread-only host API for logging (it may
// run on a worker). In debug it is fail-fast with a diagnostic naming the API and the
// offending async job (or "external thread"). In release it returns false so the
// caller skips the host call instead of entering engine state from the wrong thread.
bool requireMainThreadApi(const char *apiName);

} // namespace Bamboo::Internal
