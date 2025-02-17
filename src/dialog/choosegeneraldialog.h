#ifndef _CHOOSE_GENERAL_DIALOG_H
#define _CHOOSE_GENERAL_DIALOG_H

class General;
class QSanCommandProgressBar;

//#include "timed-progressbar.h"

class OptionButton : public QToolButton
{
    Q_OBJECT

public:
    explicit OptionButton(const QString icon_path, const QString &caption = "", QWidget *parent = 0);
#ifdef Q_WS_X11
    virtual QSize sizeHint() const{ return iconSize(); } // it causes bugs under Windows
#endif

protected:
    virtual void mouseDoubleClickEvent(QMouseEvent *);

signals:
    void double_clicked();
};

class ChooseGeneralDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChooseGeneralDialog(const QStringList &general_names, QWidget *parent, bool view_only = false, const QString &title = QString());

public slots:
    void done(int);

protected:
    QDialog *m_freeChooseDialog;
    QLineEdit *name_edit;

private:
    QSanCommandProgressBar *progress_bar;

private slots:
    void freeChoose();
};

class FreeChooseDialog : public QDialog
{
    Q_OBJECT
        Q_ENUMS(ButtonGroupType)

public:
    enum ButtonGroupType
    {
        Exclusive, Pair, Multi
    };

    explicit FreeChooseDialog(const QString &name, QWidget *parent, ButtonGroupType type = Exclusive);

private:
    QButtonGroup *group;
    ButtonGroupType type;
    QWidget *createTab(const QList<const General *> &generals);

private slots:
    void chooseGeneral();
    void uncheckExtraButton(QAbstractButton *button);

signals:
    void general_chosen(const QString &name);
    void pair_chosen(const QString &first, const QString &second);
};

#endif

