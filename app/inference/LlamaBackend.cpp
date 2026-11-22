#include "LlamaBackend.h"

#include "ChatTemplate.h"
#include "ContextManager.h"

#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
#include <llama.h>
#endif

#include <QThread>
#include <QDebug>
#include <QElapsedTimer>
#include <atomic>
#include <vector>
#include <functional>

namespace ModelFit {

struct LlamaBackend::Engine {
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    llama_model * model = nullptr;
    llama_context * ctx = nullptr;
    llama_sampler * sampler = nullptr;
#endif
    std::atomic<bool> cancelRequested{false};
};

LlamaBackend::LlamaBackend(QObject *parent) : QObject(parent) {
    m_engine = std::make_unique<Engine>();
    qRegisterMetaType<InferenceResult>("ModelFit::InferenceResult");
}

LlamaBackend::~LlamaBackend() { unloadModel(); }

bool LlamaBackend::invokeWorker(std::function<void()> fn, bool blocking) const {
    Q_UNUSED(blocking);
    fn();
    return true;
}

bool LlamaBackend::loadModel(const QString &path, const InferenceConfig &cfg) {
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    unloadModel();
    ensureBackendInitPlaceholder();
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = cfg.gpuLayers;
    Engine &e = *m_engine;
    e.model = llama_model_load_from_file(path.toUtf8().constData(), mparams);
    if (!e.model) { emit generationError("Failed to load model: " + path); return false; }

    llama_context_params cparams = llama_context_default_params();
    cparams.n_ctx = cfg.contextLength;
    cparams.n_threads = cfg.threads;
    cparams.n_threads_batch = cfg.threads;
    e.ctx = llama_init_from_model(e.model, cparams);
    if (!e.ctx) { llama_model_free(e.model); e.model = nullptr; emit generationError("Failed to create context"); return false; }

    auto sparams = llama_sampler_chain_default_params();
    llama_sampler * smpl = llama_sampler_chain_init(sparams);
    if (cfg.repeatPenalty != 1.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_penalties(cfg.penaltyLastN, cfg.repeatPenalty, 0.0f, 0.0f));
    }
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(cfg.topK));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(cfg.topP, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_min_p(cfg.minP, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(cfg.temperature));
    int seed = cfg.seed < 0 ? LLAMA_DEFAULT_SEED : cfg.seed;
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(seed));
    e.sampler = smpl;

    m_context.setCapacity(cfg.contextLength);
    m_context.reset();
    m_loaded.store(true, std::memory_order_release);
    m_cancelRequested.store(false, std::memory_order_release);
    emit modelLoaded();
    return true;
#else
    Q_UNUSED(path); Q_UNUSED(cfg);
    qWarning() << "llama.cpp not available in this build";
    return false;
#endif
}

void LlamaBackend::unloadModel() {
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    Engine &e = *m_engine;
    if (e.sampler) { llama_sampler_free(e.sampler); e.sampler = nullptr; }
    if (e.ctx) { llama_free(e.ctx); e.ctx = nullptr; }
    if (e.model) { llama_model_free(e.model); e.model = nullptr; }
#endif
    if (m_loaded.load()) {
        m_loaded.store(false, std::memory_order_release);
        emit modelUnloaded();
    }
}

void LlamaBackend::generate(const QString &prompt, const InferenceConfig &cfg) {
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    if (!m_loaded.load()) { emit generationError("No model loaded"); return; }
    Engine &e = *m_engine;
    if (!e.ctx || !e.model) { emit generationError("No model loaded"); return; }
    m_cancelRequested.store(false, std::memory_order_release);
    m_generating.store(true, std::memory_order_release);

    const llama_vocab * vocab = llama_model_get_vocab(e.model);

    // tokenize
    std::vector<llama_token> prompt_tokens;
    int n_prompt = -llama_tokenize(vocab, prompt.toUtf8().constData(), (int)prompt.toUtf8().size(), nullptr, 0, cfg.addBos, true);
    if (n_prompt < 0) n_prompt = 0;
    prompt_tokens.resize(n_prompt);
    llama_tokenize(vocab, prompt.toUtf8().constData(), (int)prompt.toUtf8().size(), prompt_tokens.data(), n_prompt, cfg.addBos, true);

    // batch
    llama_batch_ext * batch = llama_batch_ext_init(e.ctx);
    auto batch_set = [&](const std::vector<llama_token> & toks, llama_pos pos0) {
        llama_batch_ext_clear(batch);
        for (size_t i = 0; i < toks.size(); ++i) {
            int idx = llama_batch_ext_add_token(batch, 0, toks[i]);
            llama_pos p = pos0 + (llama_pos)i;
            llama_batch_ext_set_pos(batch, idx, &p);
        }
        if (!toks.empty()) llama_batch_ext_set_output_logits(batch, (int)toks.size() - 1, true);
    };

    InferenceResult result;
    QElapsedTimer timer; timer.start();
    double ttft = 0;
    bool first = true;

    int n_pos = 0;
    int n_decode = 0;

    batch_set(prompt_tokens, 0);
    if (llama_process(e.ctx, LLAMA_PROCESS_TYPE_DECODE, batch)) {
        emit generationError("Failed to process prompt");
        llama_batch_ext_free(batch);
        m_generating.store(false);
        return;
    }
    n_pos += (int)prompt_tokens.size();
    result.promptTokens = (int)prompt_tokens.size();
    m_context.setUsed(n_pos);

    for (int i = 0; i < cfg.maxTokens && !m_cancelRequested.load(); ++i) {
        llama_token new_token = llama_sampler_sample(e.sampler, e.ctx, -1);
        if (llama_vocab_is_eog(vocab, new_token)) break;

        char buf[256];
        int n = llama_token_to_piece(vocab, new_token, buf, sizeof(buf), 0, true);
        if (n < 0) { emit generationError("token_to_piece failed"); break; }
        QString piece = QString::fromUtf8(buf, n);
        result.text += piece;
        if (first) { ttft = timer.elapsed(); first = false; }
        emit tokenGenerated(piece);

        std::vector<llama_token> one = { new_token };
        batch_set(one, n_pos);
        if (llama_process(e.ctx, LLAMA_PROCESS_TYPE_DECODE, batch)) {
            emit generationError("decode failed");
            break;
        }
        n_pos += 1;
        n_decode += 1;
        m_context.setUsed(n_pos);
    }

    llama_batch_ext_free(batch);

    result.completionTokens = n_decode;
    result.cancelled = m_cancelRequested.load();
    double secs = timer.elapsed() / 1000.0;
    result.tokensPerSecond = secs > 0 ? n_decode / secs : 0;
    result.timeToFirstTokenMs = ttft;
    result.contextTokensUsed = m_context.used();
    result.contextCapacity = m_context.capacity();
    m_generating.store(false, std::memory_order_release);
    emit generationFinished(result);
#else
    Q_UNUSED(prompt); Q_UNUSED(cfg);
    emit generationError("llama.cpp not available in this build");
#endif
}

void LlamaBackend::cancel() { m_cancelRequested.store(true, std::memory_order_release); }

QString LlamaBackend::formatChat(const QList<ChatTurn> &messages, const QString &tmplOverride, bool addAssistant) const {
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    Engine &e = *m_engine;
    if (e.model) {
        return ChatTemplate::apply(e.model, messages, addAssistant, tmplOverride);
    }
#endif
    return ChatTemplate::apply(nullptr, messages, addAssistant, tmplOverride);
}

QString LlamaBackend::chatTemplate(const QString &name) const {
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
    Engine &e = *m_engine;
    if (e.model) return ChatTemplate::modelTemplate(e.model, name);
#endif
    return {};
}

// ensureBackendInit helper
#if defined(HAVE_LLAMA_CPP) && HAVE_LLAMA_CPP
namespace {
bool s_backendInit = false;
}
static void ensureBackendInitPlaceholder() {
    if (!s_backendInit) { llama_backend_init(); s_backendInit = true; }
}
#endif

InferenceManager::InferenceManager(QObject *parent) : QObject(parent) {
    connect(&m_backend, &LlamaBackend::tokenGenerated, this, &InferenceManager::tokenGenerated);
    connect(&m_backend, &LlamaBackend::generationFinished, this, &InferenceManager::generationFinished);
    connect(&m_backend, &LlamaBackend::generationError, this, &InferenceManager::generationError);
    connect(&m_backend, &LlamaBackend::modelLoaded, this, &InferenceManager::modelLoaded);
    connect(&m_backend, &LlamaBackend::modelUnloaded, this, &InferenceManager::modelUnloaded);
    connect(&m_backend, &LlamaBackend::contextUsageChanged, this, &InferenceManager::contextUsageChanged);
}

bool InferenceManager::loadModel(const QString &path, const InferenceConfig &cfg) { return m_backend.loadModel(path, cfg); }
void InferenceManager::unloadModel() { m_backend.unloadModel(); }
bool InferenceManager::isModelLoaded() const { return m_backend.isLoaded(); }
void InferenceManager::generate(const QString &prompt, const InferenceConfig &cfg) { m_backend.generate(prompt, cfg); }
void InferenceManager::cancel() { m_backend.cancel(); }
QString InferenceManager::formatChat(const QList<ChatTurn> &messages, const QString &tmplOverride, bool addAssistant) const { return m_backend.formatChat(messages, tmplOverride, addAssistant); }
QString InferenceManager::chatTemplate(const QString &name) const { return m_backend.chatTemplate(name); }

} // namespace ModelFit
