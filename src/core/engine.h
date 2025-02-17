﻿#ifndef _ENGINE_H
#define _ENGINE_H

#include "card.h"
#include "skill.h"
#include "structs.h"
#include "util.h"

class AI;
class Scenario;
class LuaBasicCard;
class LuaTrickCard;
class LuaWeapon;
class LuaArmor;
class LuaOffensiveHorse;
class LuaDefensiveHorse;
class LuaTreasure;
class CardPattern;
class RoomState;

struct lua_State;

class Engine : public QObject
{
    Q_OBJECT

public:
	Engine(bool isManualMode = false);
    ~Engine();

    void addTranslationEntry(const char *key, const char *value);
    QString translate(const QString &to_translate) const;
    lua_State *getLuaState() const;

    int getMiniSceneCounts();

    void addPackage(Package *package);
    void addBanPackage(const QString &package_name);
    QList<const Package *> getPackages() const;
    QStringList getBanPackages() const;
    Card *cloneCard(const Card *card, int new_id = -1);
    Card *cloneCard(const QString &name, Card::Suit suit = Card::SuitToBeDecided, int number = -1, const QStringList &flags = QStringList()) const;
    SkillCard *cloneSkillCard(const QString &name) const;
    QString getVersionNumber() const;
    QString getVersion() const;
    QString getVersionName() const;
    QString getMODName() const;
    QStringList getExtensions() const;
    QStringList getKingdoms() const;
    QStringList getConfigStringListFromLua(const char *key) const;
    QColor getKingdomColor(const QString &kingdom) const;
    QMap<QString, QColor> getSkillTypeColorMap() const;
    QStringList getChattingEasyTexts() const;
    QString getSetupString() const;

    QMap<QString, QString> getAvailableModes() const;
    QString getModeName(const QString &mode) const;
    int getPlayerCount(const QString &mode) const;
    QString getRoles(const QString &mode) const;
    QStringList getRoleList(const QString &mode) const;
    int getRoleIndex() const;

    const CardPattern *getPattern(const QString &name) const;
    bool matchExpPattern(const QString &pattern, const Player *player, const Card *card) const;
    Card::HandlingMethod getCardHandlingMethod(const QString &method_name) const;
    QList<const Skill *> getRelatedSkills(const QString &skill_name) const;
    const Skill *getMainSkill(const QString &skill_name) const;

    QStringList getModScenarioNames() const;
    void addScenario(Scenario *scenario);
    const Scenario *getScenario(const QString &name) const;
    void addPackage(const QString &name);

    const General *getGeneral(const QString &name) const;
    int getGeneralCount(bool include_banned = false, const QString &kingdom = QString()) const;
    const Skill *getSkill(const QString &skill_name) const;
    const Skill *getSkill(const EquipCard *card) const;
    QStringList getSkillNames() const;
    const TriggerSkill *getTriggerSkill(const QString &skill_name) const;
    const ViewAsSkill *getViewAsSkill(const QString &skill_name) const;
    QList<const DistanceSkill *> getDistanceSkills() const;
    QList<const MaxCardsSkill *> getMaxCardsSkills() const;
    QList<const TargetModSkill *> getTargetModSkills() const;
    QList<const InvaliditySkill *> getInvaliditySkills() const;
    QList<const TriggerSkill *> getGlobalTriggerSkills() const;
    QList<const AttackRangeSkill *> getAttackRangeSkills() const;
    void addSkills(const QList<const Skill *> &skills);

    int getCardCount() const;
    const Card *getEngineCard(int cardId) const;
    // @todo: consider making this const Card *
    Card *getCard(int cardId);
    WrappedCard *getWrappedCard(int cardId);

    QStringList getLords(bool contain_banned = false) const;
    QStringList getRandomLords(bool is_robot = false) const;
    QStringList getRandomGenerals(int count, const QSet<QString> &ban_set = QSet<QString>(), const QString &kingdom = QString(), bool is_robot = false) const;
    QList<int> getRandomCards() const;
    QString getRandomGeneralName() const;
    QStringList getLimitedGeneralNames(const QString &kingdom = QString()) const;
    QStringList getAllGeneralNames(const QString &kingdom = QString()) const;
    inline QList<const General *> getAllGenerals() const
    {
        return findChildren<const General *>();
    }

    void playSystemAudioEffect(const QString &name, bool superpose = true) const;
    void playAudioEffect(const QString &filename, bool superpose = true) const;
    void playSkillAudioEffect(const QString &skill_name, int index, bool superpose = true) const;

    const ProhibitSkill *isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &others = QList<const Player *>()) const;
    const ProhibitPindianSkill *isPindianProhibited(const Player *from, const Player *to) const;
    int correctDistance(const Player *from, const Player *to) const;
    int correctMaxCards(const Player *target, bool fixed = false) const;
    int correctCardTarget(const TargetModSkill::ModType type, const Player *from, const Card *card, const Player *to = NULL) const;
    bool correctSkillValidity(const Player *player, const Skill *skill) const;
    int correctAttackRange(const Player *target, bool include_weapon = true, bool fixed = false) const;

    void registerRoom(QObject *room);
    void unregisterRoom();
    QObject *currentRoomObject();
    Room *currentRoom();
    RoomState *currentRoomState();

    QString getCurrentCardUsePattern();
    CardUseStruct::CardUseReason getCurrentCardUseReason();

    QString findConvertFrom(const QString &general_name) const;
    bool isGeneralHidden(const QString &general_name) const;

private:
    void _loadMiniScenarios();
    void _loadModScenarios();
    void godLottery(QStringList &) const;
	void godLottery(QSet<QString> &) const;

    QMutex m_mutex;
    QHash<QString, QString> translations;
    QHash<QString, const General *> generals;
    QHash<QString, const QMetaObject *> metaobjects;
    QHash<QString, QString> className2objectName;
    QHash<QString, const Skill *> skills;
    QHash<QThread *, QObject *> m_rooms;
    QMap<QString, QString> modes;
    QMultiMap<QString, QString> related_skills;
    mutable QMap<QString, const CardPattern *> patterns;

    // special skills
    QList<const ProhibitSkill *> prohibit_skills;
    QList<const ProhibitPindianSkill *> prohibitpindian_skills;
    QList<const DistanceSkill *> distance_skills;
    QList<const MaxCardsSkill *> maxcards_skills;
    QList<const TargetModSkill *> targetmod_skills;
    QList<const InvaliditySkill *> invalidity_skills;
    QList<const TriggerSkill *> global_trigger_skills;
    QList<const AttackRangeSkill *> attack_range_skills;

    QList<Card *> cards;
    QStringList lord_list;
    QSet<QString> ban_package;
    QHash<QString, Scenario *> m_scenarios;
    QHash<QString, Scenario *> m_miniScenes;
    Scenario *m_customScene;

    lua_State *lua;

    QHash<QString, QString> luaBasicCard_className2objectName;
    QHash<QString, const LuaBasicCard *> luaBasicCards;
    QHash<QString, QString> luaTrickCard_className2objectName;
    QHash<QString, const LuaTrickCard *> luaTrickCards;
    QHash<QString, QString> luaWeapon_className2objectName;
    QHash<QString, const LuaWeapon*> luaWeapons;
    QHash<QString, QString> luaArmor_className2objectName;
    QHash<QString, const LuaArmor *> luaArmors;
    QHash<QString, QString> luaOffensiveHorse_className2objectName;
    QHash<QString, const LuaOffensiveHorse*> luaOffensiveHorses;
    QHash<QString, QString> luaDefensiveHorse_className2objectName;
    QHash<QString, const LuaDefensiveHorse*> luaDefensiveHorses;
    QHash<QString, QString> luaTreasure_className2objectName;
    QHash<QString, const LuaTreasure *> luaTreasures;

    QMultiMap<QString, QString> sp_convert_pairs;
    QStringList extra_hidden_generals;
    QStringList removed_hidden_generals;
    QStringList extra_default_lords;
    QStringList removed_default_lords;
#ifdef LOGNETWORK
signals:
	void logNetworkMessage(QString);
public slots:
	void handleNetworkMessage(QString);
private:
	QFile logFile;
#endif // LOGNETWORK

};

static inline QVariant GetConfigFromLuaState(lua_State *L, const char *key)
{
    return GetValueFromLuaState(L, "config", key);
}

extern Engine *Sanguosha;

#endif

