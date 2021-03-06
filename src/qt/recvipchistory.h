#ifndef RECVIPCHISTORY_H
#define RECVIPCHISTORY_H

#include <QWidget>
QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE
namespace Ui {
class recvipchistory;
}

class recvipchistory : public QWidget
{
    Q_OBJECT

public:
    explicit recvipchistory(QWidget *parent = 0);
    ~recvipchistory();
    void showVisual(bool visual);
    void updateInfo(QString status);
    void setinfo(const QModelIndex &idx);
private:
    Ui::recvipchistory *ui;
};

#endif // RECVIPCHISTORY_H
