#include <QtTest>
#include "app/skills/SkillsManager.h"

using namespace ModelBench;

class TestSkillsParser : public QObject {
    Q_OBJECT
private slots:
    void parse_frontmatter() {
        QString md = "---\nname: foo\ndescription: bar\n---\n# body";
        auto o = SkillsManager::parseSkillMd(md);
        QCOMPARE(o.value("name").toString(), QString("foo"));
        QCOMPARE(o.value("description").toString(), QString("bar"));
    }
    void parse_no_frontmatter() {
        auto o = SkillsManager::parseSkillMd("# just body");
        QVERIFY(o.isEmpty());
    }
};

QTEST_MAIN(TestSkillsParser)
#include "test_skills_parser.moc"
