#include "scenario-overview.h"
#include "engine.h"
#include "scenario.h"

ScenarioOverview::ScenarioOverview(QWidget *parent)
    : QDialog(parent)
{
    //setWindowTitle(tr("Scenario overview"));
    setWindowTitle(Sanguosha->translate("GAME_INSTRUCTION"));
    resize(800, 600);

    list = new QListWidget;
    list->setMaximumWidth(100);

    content_box = new QTextEdit;
    content_box->setReadOnly(true);
    content_box->setProperty("description", true);

    QHBoxLayout *layout = new QHBoxLayout;

    layout->addWidget(content_box);
    layout->addWidget(list);

    setLayout(layout);

    /*QStringList names = Sanguosha->getModScenarioNames();
    names << "BossMode" << "Hulaopass" << "Basara" << "Hegemony" << "MiniScene";*/
    QStringList names;
    names << "UpdateInfo" << "NewGameRules" << "TietieBattle" << "Couple" << "IceFire";
    foreach (QString name, names) {
        QString text = Sanguosha->translate(name);
        QListWidgetItem *item = new QListWidgetItem(text, list);
        item->setData(Qt::UserRole, name);
    }

    connect(list, SIGNAL(currentRowChanged(int)), this, SLOT(loadContent(int)));

    if (!names.isEmpty())
        loadContent(0);
}

void ScenarioOverview::loadContent(int row)
{
    QString name = list->item(row)->data(Qt::UserRole).toString();
    QString filename = QString("scenarios/%1.html").arg(name.toLower());
    QFile file(filename);
    if (file.open(QIODevice::ReadOnly)) {
        QTextStream stream(&file);
        stream.setCodec("UTF-8");
        QString content = stream.readAll();
        content_box->setHtml(content);
    }
}

