#include "tagging/TagChanges.h"
#include <QtTest>

using namespace bt;

class TagTests : public QObject {
    Q_OBJECT
  private slots:
    void edits()
    {
        TagChanges c{tagSet({"solo", "tagme"}), {}, {}};
        QVERIFY(c.add("smile"));
        QVERIFY(!c.add("smile"));
        QVERIFY(!c.add("solo"));
        QVERIFY(c.remove("tagme"));
        QCOMPARE(c.original.size(), 2);
        QCOMPARE(c.result(), tagSet({"solo", "smile"}));
        QVERIFY(c.add("tagme"));
        QVERIFY(c.remove("smile"));
        QVERIFY(!c.dirty());
    }
    void normalization()
    {
        QCOMPARE(splitTags(" Solo  SOLO\ncustom_tag ", ProviderKind::Danbooru), QStringList({"solo", "custom_tag"}));
        QCOMPARE(splitTags("Custom:Tag custom:tag", ProviderKind::Shimmie), QStringList({"Custom:Tag", "custom:tag"}));
        QCOMPARE(tagsFromJson(tagsJson({"solo", "Custom:Tag"})), tagSet({"solo", "Custom:Tag"}));
        QVERIFY_EXCEPTION_THROWN(tagsFromJson("broken"), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(tagsFromJson("[42]"), std::runtime_error);
    }
    void merging()
    {
        TagChanges c{tagSet({"solo", "tagme"}), {"smile"}, {"tagme"}};
        auto merge = mergeTags(c, tagSet({"solo", "tagme", "blue_eyes"}));
        QVERIFY(!merge.needsReview);
        QCOMPARE(merge.tags, tagSet({"solo", "smile", "blue_eyes"}));
        merge = mergeTags(c, tagSet({"tagme", "blue_eyes"}));
        QVERIFY(merge.needsReview);
        QVERIFY(!merge.tags.contains("solo"));
        merge = mergeTags(c, tagSet({"solo"}));
        QVERIFY(!merge.needsReview);
    }
    void ranges()
    {
        QCOMPARE(idRange(3, 6), QList<qint64>({3, 4, 5, 6}));
        QCOMPARE(idRange(6, 3), QList<qint64>({6, 5, 4, 3}));
        QVERIFY(idRange(0, 5).isEmpty());
        QVERIFY(idRange(1, 100001).isEmpty());
        QCOMPARE(uniqueIds({3, 3, -1, 2, 1, 2}), QList<qint64>({3, 2, 1}));
    }
    void instanceUrls()
    {
        QCOMPARE(endpoint(QUrl("https://example.org/booru/"), "graphql").toString(),
                 QString("https://example.org/booru/graphql"));
        QVERIFY(!sameOrigin(QUrl("https://a.test"), QUrl("https://b.test")));
        Instance i{"id", "test", ProviderKind::Shimmie, QUrl("http://localhost/booru"), "user", false};
        QVERIFY(!validateInstance(i).isEmpty());
        i.allowHttp = true;
        QVERIFY(validateInstance(i).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TagTests)
#include "test_tags.moc"
