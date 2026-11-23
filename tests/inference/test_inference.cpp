#include <QtTest>

#include <QFileInfo>
#include <QSignalSpy>
#include <QThread>

#include "app/inference/ChatTemplate.h"
#include "app/inference/ContextManager.h"
#include "app/inference/LlamaBackend.h"

using namespace ModelBench;

class TestInference : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void chatTemplateFallback();
    void contextManagerMath();

    void loadGenerateUnload();
    void cancellation();

private:
    bool hasTestModel() const;
    InferenceConfig testConfig(int maxTokens) const;

    QString m_modelPath;
};

void TestInference::initTestCase()
{
    qRegisterMetaType<InferenceResult>("ModelBench::InferenceResult");
    m_modelPath = qEnvironmentVariable("MODELFIT_TEST_MODEL");
    if (m_modelPath.isEmpty()) {
        qInfo() << "MODELFIT_TEST_MODEL is not set; model-backed inference tests will be skipped";
    }
}

bool TestInference::hasTestModel() const
{
    return !m_modelPath.isEmpty() && QFileInfo::exists(m_modelPath);
}

InferenceConfig TestInference::testConfig(int maxTokens) const
{
    InferenceConfig cfg;
    cfg.modelPath = m_modelPath;
    cfg.contextLength = 512;
    cfg.threads = qMax(1, QThread::idealThreadCount() / 2);
    cfg.batchSize = 128;
    cfg.gpuLayers = 0;
    cfg.temperature = 0.0f;
    cfg.topP = 1.0f;
    cfg.topK = 0;
    cfg.minP = 0.0f;
    cfg.repeatPenalty = 1.0f;
    cfg.penaltyLastN = 0;
    cfg.maxTokens = maxTokens;
    cfg.seed = 42;
    return cfg;
}

void TestInference::chatTemplateFallback()
{
    const QList<ChatTurn> messages = {
        {QStringLiteral("system"), QStringLiteral("You are concise.")},
        {QStringLiteral("user"), QStringLiteral("Hi")},
    };

    const QString prompt = ChatTemplate::renderPrompt(messages, true);
    QVERIFY(prompt.contains(QStringLiteral("<|im_start|>system")));
    QVERIFY(prompt.contains(QStringLiteral("You are concise.")));
    QVERIFY(prompt.contains(QStringLiteral("Hi")));
    QVERIFY(prompt.endsWith(QStringLiteral("<|im_start|>assistant\n")));

    const QString fromNullModel = ChatTemplate::apply(nullptr, messages, true);
    QVERIFY(fromNullModel.contains(QStringLiteral("<|im_start|>assistant")));
}

void TestInference::contextManagerMath()
{
    ContextManager context;
    context.setCapacity(100);
    QCOMPARE(context.capacity(), 100);
    QCOMPARE(context.used(), 0);

    context.setUsed(30);
    QCOMPARE(context.available(), 70);
    QVERIFY(context.fits(70));
    QVERIFY(!context.fits(71));

    context.addTokens(80);
    QCOMPARE(context.used(), 100);
    QCOMPARE(context.available(), 0);

    QCOMPARE(context.maxPromptTokens(10), 90);
    context.reset();
    QCOMPARE(context.used(), 0);
}

void TestInference::loadGenerateUnload()
{
#ifndef HAVE_LLAMA_CPP
    QSKIP("Built without llama.cpp");
#else
    if (!hasTestModel()) {
        QSKIP("MODELFIT_TEST_MODEL is not set or does not exist");
    }

    LlamaBackend backend;
    QSignalSpy errors(&backend, &LlamaBackend::generationError);
    QSignalSpy loaded(&backend, &LlamaBackend::modelLoaded);

    const InferenceConfig cfg = testConfig(24);
    const bool ok = backend.loadModel(m_modelPath, cfg);
    const QString loadError = errors.isEmpty()
        ? QStringLiteral("load failed without an error signal")
        : errors.at(0).at(0).toString();
    QVERIFY2(ok, qPrintable(loadError));
    QVERIFY(backend.isLoaded());
    QCOMPARE(loaded.count(), 1);
    QVERIFY(backend.contextManager().capacity() > 0);

    QSignalSpy tokens(&backend, &LlamaBackend::tokenGenerated);
    QSignalSpy finished(&backend, &LlamaBackend::generationFinished);

    backend.generate(QStringLiteral("Hello"), cfg);
    QVERIFY(finished.wait(180000));
    QCOMPARE(finished.count(), 1);

    const InferenceResult result = finished.at(0).at(0).value<InferenceResult>();
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.promptTokens > 0);
    QVERIFY(result.completionTokens > 0);
    QVERIFY(!result.text.isEmpty());
    QVERIFY(tokens.count() > 0);
    QVERIFY(result.timeToFirstTokenMs >= 0.0);
    QVERIFY(result.tokensPerSecond >= 0.0);
    QVERIFY(result.contextCapacity > 0);
    QVERIFY(result.contextTokensUsed > 0);
    QCOMPARE(result.contextCapacity, backend.contextManager().capacity());

    QSignalSpy unloaded(&backend, &LlamaBackend::modelUnloaded);
    backend.unloadModel();
    QVERIFY(!backend.isLoaded());
    QCOMPARE(unloaded.count(), 1);
#endif
}

void TestInference::cancellation()
{
#ifndef HAVE_LLAMA_CPP
    QSKIP("Built without llama.cpp");
#else
    if (!hasTestModel()) {
        QSKIP("MODELFIT_TEST_MODEL is not set or does not exist");
    }

    LlamaBackend backend;
    QSignalSpy errors(&backend, &LlamaBackend::generationError);

    InferenceConfig cfg = testConfig(1000000);
    const bool ok = backend.loadModel(m_modelPath, cfg);
    const QString loadError = errors.isEmpty()
        ? QStringLiteral("load failed without an error signal")
        : errors.at(0).at(0).toString();
    QVERIFY2(ok, qPrintable(loadError));

    QSignalSpy finished(&backend, &LlamaBackend::generationFinished);
    backend.generate(QStringLiteral("Write an extremely long story about the ocean."), cfg);
    // Cancel before the worker gets far; the flag is checked at the start and
    // between every decoded token.
    backend.cancel();

    QVERIFY(finished.wait(60000));
    QCOMPARE(finished.count(), 1);

    const InferenceResult result = finished.at(0).at(0).value<InferenceResult>();
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.cancelled);
    QVERIFY(result.completionTokens < cfg.maxTokens);

    backend.unloadModel();
    QVERIFY(!backend.isLoaded());
#endif
}

QTEST_MAIN(TestInference)
#include "test_inference.moc"
