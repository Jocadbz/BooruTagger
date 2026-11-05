#include "ui/TagEditor.h"
#include "media/ImageView.h"
#include <QLineEdit>
#include <QListWidget>
#include <QtTest>

using namespace bt;

class WidgetTests : public QObject {
    Q_OBJECT
private slots:
    void keyboardEditing() {
        TagEditor editor;
        editor.load({tagSet({"tagme", "solo"}), {}, {}}, ProviderKind::Danbooru);
        editor.show();
        editor.focusInput();
        auto *input = editor.findChild<QLineEdit *>("tagInput");
        QTest::keyClicks(input, "smile smile long_hair");
        QTest::keyClick(input, Qt::Key_Return);
        QCOMPARE(editor.changes().added.size(), 2);
        QVERIFY(input->text().isEmpty());
        editor.undo();
        QVERIFY(!editor.changes().dirty());
        editor.redo();
        auto *list = editor.findChild<QListWidget *>("currentTags");
        for (int i = 0; i < list->count(); ++i) {
            if (list->item(i)->text() == "tagme")
                list->setCurrentRow(i);
        }
        list->setFocus();
        QTest::qWait(10);
        QTest::keyClick(list, Qt::Key_Delete);
        QVERIFY(editor.changes().removed.contains("tagme"));
        editor.undo();
        QVERIFY(!editor.changes().removed.contains("tagme"));
    }
    void suggestionMetadata() {
        SuggestionModel model;
        model.setSuggestions({{"solo", 0, 100, false, {}}, {"custom", {}, {}, false, {}},
                              {"smile", 0, 42, false, "smiling"}}, {"solo"});
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.data(model.index(0), Qt::DisplayRole).toString(), QString("custom"));
        QVERIFY(model.data(model.index(1), Qt::DisplayRole).toString().contains("smiling"));
        QCOMPARE(model.data(model.index(1), Qt::EditRole).toString(), QString("smile"));
    }
    void imageFit() {
        ImageView view;
        view.resize(400, 300);
        view.show();
        QImage image(1000, 500, QImage::Format_RGB32);
        image.fill(Qt::red);
        view.setImage(image);
        QVERIFY(view.transform().m11() < 1.0);
        view.actualSize();
        QCOMPARE(view.transform().m11(), 1.0);
        view.clearImage();
        QVERIFY(view.scene()->items().isEmpty());
    }
};

QTEST_MAIN(WidgetTests)
#include "test_widgets.moc"
