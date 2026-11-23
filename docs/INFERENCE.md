# Inference

ModelBench runs GGUF models in-process through llama.cpp. The implementation lives
in `app/inference/` and is compiled in only when `MODELFIT_ENABLE_LLAMA_CPP=ON`
and the `third_party/llama.cpp` submodule is present. Without it, the same API
is still available and reports that inference is unavailable.

## Building

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0 \
  -DMODELFIT_ENABLE_LLAMA_CPP=ON
cmake --build build
```

`HAVE_LLAMA_CPP` is defined for targets that link `llama`. The top-level
`CMakeLists.txt` only defines it when the submodule exists, so a missing
submodule produces a warning instead of a broken build.

## Public API

`IInferenceBackend` is the stable boundary. `LlamaBackend` implements it;
`InferenceManager` owns one backend and forwards its signals.

```cpp
#include "app/inference/LlamaBackend.h"

using namespace ModelBench;

InferenceConfig cfg;
cfg.contextLength = 4096;
cfg.threads = 8;
cfg.batchSize = 512;
cfg.gpuLayers = 0;        // >0 offloads layers to the GPU
cfg.temperature = 0.7f;
cfg.topP = 0.9f;
cfg.topK = 40;
cfg.minP = 0.05f;
cfg.repeatPenalty = 1.1f;
cfg.penaltyLastN = 64;
cfg.maxTokens = 512;
cfg.seed = -1;            // random seed; >=0 is deterministic
cfg.stopSequences = {QStringLiteral("</s>")};

LlamaBackend backend;
QObject::connect(&backend, &LlamaBackend::tokenGenerated,
                 [](const QString &piece) { /* append to UI */ });
QObject::connect(&backend, &LlamaBackend::generationFinished,
                 [](const InferenceResult &result) { /* final text + stats */ });
QObject::connect(&backend, &LlamaBackend::generationError,
                 [](const QString &error) { /* show error */ });

if (!backend.loadModel(QStringLiteral("/models/tiny.gguf"), cfg)) {
    return; // generationError already emitted
}

backend.generate(QStringLiteral("Hello"), cfg);
// ... later, if needed:
backend.cancel();
```

`InferenceResult` contains the complete text, prompt/completion token counts,
tokens per second, time to first token, prompt evaluation time, context usage
and the cancellation/error flags. `generationFinished` is emitted for every
completed request, including cancelled and failed ones. `generationError` is
emitted in addition when `result.error` is non-empty, and directly for load
failures or rejected requests.

### Chat templates

`generate()` takes a single prompt string. Format conversations with
`ChatTemplate` before calling it:

```cpp
#include "app/inference/ChatTemplate.h"

QList<ChatMessage> messages = {
    {QStringLiteral("system"), QStringLiteral("You are concise.")},
    {QStringLiteral("user"), QStringLiteral("Explain GGUF.")},
};
const QString prompt = backend.formatChat(messages); // uses model template
```

`LlamaBackend::formatChat()` uses the template embedded in the GGUF metadata
through `llama_chat_apply_template`. If the model has no template, or the
template is not one of the templates supported by llama.cpp, it falls back to
ChatML. `backend.chatTemplate()` returns the raw template for inspection.
`ChatTemplate::renderPrompt()` is a model-free ChatML fallback and works in
builds without llama.cpp.

## Threading

`LlamaBackend` creates one `QThread` and moves a small context object to it. All
llama.cpp calls (model load, context creation, tokenization, decode, sampling,
unload) run on that worker thread. The GUI thread never calls libllama directly.

- `loadModel()` and `unloadModel()` use a blocking invocation and return once
  the worker is done.
- `generate()` posts an asynchronous invocation and returns immediately.
- `tokenGenerated`, `generationFinished`, `generationError` and
  `contextUsageChanged` are emitted from the worker. Qt's automatic connection
  type queues them to the receiver's thread, so UI slots always run on the GUI
  thread.
- `formatChat()` and `chatTemplate()` use a blocking invocation because they
  need the model pointer.
- Only one generation may run at a time. A second call while a generation is
  active emits `generationError("Generation already in progress")`.
- `loadModel()` is rejected while a generation is active.

The destructor cancels any running generation, quits the worker thread and
waits before freeing the model, context and sampler.

## Cancellation

`cancel()` sets an atomic flag. The flag is checked:

- before tokenization,
- between prompt chunks,
- before every sampled token,
- by the llama.cpp abort callback during `llama_process()` (CPU backend; GPU
  backends may take longer to notice).

`unloadModel()` also requests cancellation first, so it does not wait for a full
generation to finish. A cancelled request still produces `generationFinished`
with `InferenceResult::cancelled == true` and the text generated so far.

## Context tracking

`ContextManager` records the actual context size reported by
`llama_n_ctx()` and the number of positions used by the current generation.
`backend.contextManager()` exposes it; `contextUsageChanged(used, capacity)` is
emitted after each request and after load/unload. `available()`, `fits()` and
`maxPromptTokens()` can be used to trim chat history before calling
`generate()`.

Prompts that do not fit are rejected with an error instead of being silently
truncated. The maximum completion is clamped to the remaining context.

## Backend and hardware detection

`app/hardware/BackendDetector` reports available compute backends:

- CPU is always available.
- When built with `HAVE_LLAMA_CPP`, the ggml runtime registry and device list
  are queried after `ggml_backend_load_all()`.
- Independently, the detector probes for `ggml-cuda`, `ggml-vulkan` and
  `ggml-metal` shared libraries with `QLibrary`. It looks next to the
  executable, in `ggml/` and `backends/` subdirectories, in `../lib`, and in the
  platform library search path.
- Metal is also reported on macOS because it ships with the OS.

`HardwareDetector::detect()` combines CPU, RAM, GPU and backend information:

- Windows: CPU name/cores from the registry, physical RAM from
  `GlobalMemoryStatusEx`, GPU name/driver/VRAM from the display adapter registry
  key (`HardwareInformation.qwMemorySize`, which avoids the 32-bit `AdapterRAM`
  limit), with `nvidia-smi` as the authoritative source for NVIDIA cards and
  `wmic` as a last-resort fallback.
- macOS: `sysctl` for CPU/RAM and `system_profiler SPDisplaysDataType` for GPU
  and VRAM.
- Linux: `/proc/cpuinfo`, `/proc/meminfo`, `nvidia-smi` and `lspci`.

The resulting `supportedBackends` list is what the UI should offer when choosing
`gpuLayers`.

## Sampling

Each request builds a fresh sampler chain:

1. penalties (`repeatPenalty`, last `penaltyLastN` tokens)
2. top-k
3. top-p
4. min-p
5. temperature
6. distribution with the configured seed (or greedy when temperature is 0)

The chain is owned by the worker and freed on the next request or on unload.

## Tests

`tests/inference/test_inference.cpp` covers ChatML fallback, context math,
load/generate/unload and cancellation. Model-backed tests need a small GGUF:

```sh
set MODELFIT_TEST_MODEL=C:\models\tiny.gguf
ctest --test-dir build -R modelfit_inference_tests --output-on-failure
```

If `MODELFIT_TEST_MODEL` is unset, or the build has no llama.cpp, the
model-backed tests skip and the pure logic tests still run.

## Limitations

- Encoder-decoder architectures are rejected; the backend targets decoder-only
  chat models.
- The chat template helper relies on llama.cpp's supported template list, not a
  full Jinja engine. Unsupported templates fall back to ChatML.
- A model is loaded with one context and reused for all requests; the KV cache
  is cleared before every generation.
- Only one request runs at a time (no continuous batching or parallel slots).
- GPU cancellation depends on the backend honoring llama.cpp's abort callback.
