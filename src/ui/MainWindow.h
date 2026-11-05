#pragma once

#include <QMainWindow>

namespace bt {
class TagEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    TagEditor *m_editor;
};

}
