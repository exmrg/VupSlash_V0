#ifndef _WINRATE_VIEW_DIALOG_H
#define _WINRATE_VIEW_DIALOG_H

class ClientPlayer;

class WinrateViewDialogUI;

class WinrateViewDialog : public QDialog
{
    Q_OBJECT

public:
    WinrateViewDialog(QWidget *parent = 0);
    ~WinrateViewDialog();
    void move_to(QString name);

private:
    WinrateViewDialogUI *ui;

private slots:
    void refresh();
    void refresh_skill_text();
};

#endif

