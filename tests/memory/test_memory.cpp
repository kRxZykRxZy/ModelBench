#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "app/storage/VectorStore.h"
#include "app/agent/AgentMemory.h"
#include "app/storage/CodeIndexer.h"

using namespace ModelBench;

namespace {

QSqlDatabase makeDb(const QString &name, const QString &path) {
    if (QSqlDatabase::contains(name))
        QSqlDatabase::removeDatabase(name);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(path);
    return db;
}

void closeDb(QSqlDatabase &db, const QString &name) {
    const QString dbName = db.databaseName();
    db.close();
    db = QSqlDatabase();
    if (QSqlDatabase::contains(name))
        QSqlDatabase::removeDatabase(name);
}

QVector<float> fakeEmbed(const QString &text) {
    // Deterministic tiny embedding for tests: 4 dims based on keyword presence.
    const QString t = text.toLower();
    return QVector<float>{
        t.contains(QStringLiteral("cat")) ? 1.0f : 0.0f,
        t.contains(QStringLiteral("dog")) ? 1.0f : 0.0f,
        t.contains(QStringLiteral("code")) || t.contains(QStringLiteral("function")) ? 1.0f : 0.0f,
        t.contains(QStringLiteral("memory")) || t.contains(QStringLiteral("remember")) ? 1.0f : 0.0f,
    };
}

} // namespace

class TestMemory : public QObject {
    Q_OBJECT

private slots:
    void vectorStore_upsertAndSearch();
    void vectorStore_hybridSearch();
    void agentMemory_scratchpadAndProjectContext();
    void agentMemory_rememberRecallAndSearchTool();
    void codeIndexer_indexAndSearch();
};

void TestMemory::vectorStore_upsertAndSearch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conn = QStringLiteral("test_vs_1");
    QSqlDatabase db = makeDb(conn, dir.filePath(QStringLiteral("test.db")));
    QVERIFY(db.open());

    VectorStore store(db);
    QVERIFY(store.ensureSchema());

    VectorEntry a;
    a.id = QStringLiteral("a");
    a.namespaceName = QStringLiteral("ns");
    a.content = QStringLiteral("cat likes milk");
    a.embedding = fakeEmbed(a.content);
    QVERIFY(store.upsert(a));

    VectorEntry b;
    b.id = QStringLiteral("b");
    b.namespaceName = QStringLiteral("ns");
    b.content = QStringLiteral("dog likes bones");
    b.embedding = fakeEmbed(b.content);
    QVERIFY(store.upsert(b));

    const auto sem = store.semanticSearch(fakeEmbed(QStringLiteral("cat")), QStringLiteral("ns"), 5);
    QVERIFY(!sem.isEmpty());
    QCOMPARE(sem.first().id, QStringLiteral("a"));

    const auto kw = store.keywordSearch(QStringLiteral("bones"), QStringLiteral("ns"), 5);
    QVERIFY(!kw.isEmpty());
    QCOMPARE(kw.first().id, QStringLiteral("b"));

    closeDb(db, conn);
}

void TestMemory::vectorStore_hybridSearch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conn = QStringLiteral("test_vs_2");
    QSqlDatabase db = makeDb(conn, dir.filePath(QStringLiteral("test.db")));
    QVERIFY(db.open());

    VectorStore store(db);
    QVERIFY(store.ensureSchema());

    VectorEntry a{QStringLiteral("a"), QStringLiteral("ns"), QStringLiteral("cat memory note"), {}, fakeEmbed(QStringLiteral("cat memory")), QDateTime::currentMSecsSinceEpoch()};
    VectorEntry b{QStringLiteral("b"), QStringLiteral("ns"), QStringLiteral("dog walk note"), {}, fakeEmbed(QStringLiteral("dog")), QDateTime::currentMSecsSinceEpoch()};
    QVERIFY(store.upsert(a));
    QVERIFY(store.upsert(b));

    const auto hybrid = store.hybridSearch(QStringLiteral("cat"), fakeEmbed(QStringLiteral("cat")), QStringLiteral("ns"), 5, 0.7f);
    QVERIFY(!hybrid.isEmpty());
    QCOMPARE(hybrid.first().id, QStringLiteral("a"));
    QVERIFY(hybrid.first().score > 0.0f);

    closeDb(db, conn);
}

void TestMemory::agentMemory_scratchpadAndProjectContext() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conn = QStringLiteral("test_am_1");
    QSqlDatabase db = makeDb(conn, dir.filePath(QStringLiteral("test.db")));
    QVERIFY(db.open());

    AgentMemory mem(db);
    QVERIFY(mem.ensureSchema());

    QVERIFY(mem.setScratchpad(QStringLiteral("conv1"), QStringLiteral("plan"), QStringLiteral("step1")));
    auto val = mem.getScratchpad(QStringLiteral("conv1"), QStringLiteral("plan"));
    QVERIFY(val.has_value());
    QCOMPARE(val.value(), QStringLiteral("step1"));

    const auto list = mem.listScratchpad(QStringLiteral("conv1"));
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().key, QStringLiteral("plan"));

    QVERIFY(mem.setProjectContext(dir.path(), QStringLiteral("style"), QStringLiteral("Qt")));
    auto p = mem.getProjectContext(dir.path(), QStringLiteral("style"));
    QVERIFY(p.has_value());
    QCOMPARE(p.value(), QStringLiteral("Qt"));

    QVERIFY(mem.clearScratchpad(QStringLiteral("conv1")));
    QVERIFY(mem.listScratchpad(QStringLiteral("conv1")).isEmpty());

    closeDb(db, conn);
}

void TestMemory::agentMemory_rememberRecallAndSearchTool() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString conn = QStringLiteral("test_am_2");
    QSqlDatabase db = makeDb(conn, dir.filePath(QStringLiteral("test.db")));
    QVERIFY(db.open());

    VectorStore store(db);
    QVERIFY(store.ensureSchema());
    store.setEmbeddingProvider(fakeEmbed);

    AgentMemory mem(db);
    mem.setVectorStore(&store);
    QVERIFY(mem.ensureSchema());

    const QString id = mem.remember(QStringLiteral("conv1"), QStringLiteral("remember cat fact"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("fact")}});
    QVERIFY(!id.isEmpty());
    mem.remember(QStringLiteral("conv1"), QStringLiteral("dog fact"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("fact")}});

    MemorySearchQuery q;
    q.query = QStringLiteral("cat");
    q.conversationId = QStringLiteral("conv1");
    q.topK = 5;
    const auto recalled = mem.recall(q);
    QVERIFY(!recalled.isEmpty());
    QCOMPARE(recalled.first().conversationId, QStringLiteral("conv1"));
    QVERIFY(recalled.first().content.contains(QStringLiteral("cat")));

    QJsonObject args;
    args.insert(QStringLiteral("query"), QStringLiteral("cat"));
    args.insert(QStringLiteral("conversation_id"), QStringLiteral("conv1"));
    const QJsonObject toolResult = mem.searchTool(args);
    QVERIFY(toolResult.value(QStringLiteral("ok")).toBool());
    QVERIFY(toolResult.value(QStringLiteral("results")).toArray().size() >= 1);

    closeDb(db, conn);
}

void TestMemory::codeIndexer_indexAndSearch() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString srcDir = dir.filePath(QStringLiteral("src"));
    QVERIFY(QDir().mkpath(srcDir));
    QFile f(dir.filePath(QStringLiteral("src/main.cpp")));
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    f.write("int add(int a, int b) { return a + b; }\n\nint main() { return add(1,2); }\n");
    f.close();

    QFile f2(dir.filePath(QStringLiteral("README.md")));
    QVERIFY(f2.open(QIODevice::WriteOnly | QIODevice::Text));
    f2.write("# Project\nThis project has cat memory notes.\n");
    f2.close();

    const QString conn = QStringLiteral("test_ci_1");
    QSqlDatabase db = makeDb(conn, dir.filePath(QStringLiteral("test.db")));
    QVERIFY(db.open());

    VectorStore store(db);
    QVERIFY(store.ensureSchema());
    store.setEmbeddingProvider(fakeEmbed);

    CodeIndexer indexer(&store);
    indexer.setEmbeddingProvider(fakeEmbed);

    IndexOptions opt;
    opt.chunkLines = 20;
    opt.chunkOverlapLines = 0;
    const IndexStats stats = indexer.indexProject(dir.path(), opt);
    QVERIFY(stats.filesScanned >= 2);
    QVERIFY(stats.chunksStored >= 2);

    const QJsonArray tree = indexer.fileTree(dir.path(), opt);
    QVERIFY(!tree.isEmpty());

    const auto results = indexer.search(QStringLiteral("cat memory"), dir.path(), 5);
    QVERIFY(!results.isEmpty());
    QVERIFY(results.first().filePath.contains(QStringLiteral("README.md")));

    closeDb(db, conn);
}

QTEST_MAIN(TestMemory)
#include "test_memory.moc"
