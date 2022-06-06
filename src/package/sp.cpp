#include "sp.h"
#include "client.h"
#include "general.h"
#include "skill.h"
#include "standard-skillcards.h"
#include "engine.h"
#include "maneuvering.h"
#include "json.h"
#include "settings.h"
#include "clientplayer.h"
#include "util.h"
#include "wrapped-card.h"
#include "room.h"
#include "roomthread.h"
#include "yjcm2013.h"
#include "wind.h"

class SPMoonSpearSkill : public WeaponSkill
{
public:
    SPMoonSpearSkill() : WeaponSkill("sp_moonspear")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::NotActive)
            return false;

        const Card *card = NULL;
        if (triggerEvent == CardUsed) {
            CardUseStruct card_use = data.value<CardUseStruct>();
            card = card_use.card;
        } else if (triggerEvent == CardResponded) {
            card = data.value<CardResponseStruct>().m_card;
        }

        if (card == NULL || !card->isBlack()
            || (card->getHandlingMethod() != Card::MethodUse && card->getHandlingMethod() != Card::MethodResponse))
            return false;

        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *tmp, room->getAlivePlayers()) {
            if (player->inMyAttackRange(tmp))
                targets << tmp;
        }
        if (targets.isEmpty()) return false;

        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@sp_moonspear", true, true);
        if (!target) return false;
        room->setEmotion(player, "weapon/moonspear");
        room->getThread()->delay(250);
        room->broadcastSkillInvoke("audio_effect", 29);
        if (!room->askForCard(target, "jink", "@moon-spear-jink", QVariant(), Card::MethodResponse, player)){
            DamageStruct dmg = DamageStruct(objectName(), player, target);
            if (player->getWeapon() && player->getWeapon()->isKindOf("SPMoonSpear"))
                dmg.card = player->getWeapon();
            room->damage(dmg);
        } else {
            CardEffectStruct effect;
            effect.from = player;
            effect.to = target;
            if (player->getWeapon() && player->getWeapon()->isKindOf("SPMoonSpear"))
                effect.card = player->getWeapon();
            QVariant data = QVariant::fromValue(effect);
            room->getThread()->trigger(EffectResponded, room, player, data);
            room->getThread()->trigger(EffectOffsetted, room, player, data);
        }
        return false;
    }
};

SPMoonSpear::SPMoonSpear(Suit suit, int number)
    : Weapon(suit, number, 3)
{
    setObjectName("sp_moonspear");
}

SPCardPackage::SPCardPackage()
    : Package("sp_cards")
{
    (new SPMoonSpear)->setParent(this);
    skills << new SPMoonSpearSkill;

    type = CardPack;
}

ADD_PACKAGE(SPCard)

HegemonySPPackage::HegemonySPPackage()
: Package("hegemony_sp")
{
    General *sp_heg_zhouyu = new General(this, "sp_heg_zhouyu", "wu", 3, true, true); // GSP 001
    sp_heg_zhouyu->addSkill("nosyingzi");
    sp_heg_zhouyu->addSkill("nosfanjian");

    General *sp_heg_xiaoqiao = new General(this, "sp_heg_xiaoqiao", "wu", 3, false, true); // GSP 002
    sp_heg_xiaoqiao->addSkill("tianxiang");
    sp_heg_xiaoqiao->addSkill("hongyan");
}

ADD_PACKAGE(HegemonySP)

class Jilei : public TriggerSkill
{
public:
    Jilei() : TriggerSkill("jilei")
    {
        events << Damaged;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *yangxiu, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *current = room->getCurrent();
        if (!current || current->getPhase() == Player::NotActive || current->isDead() || !damage.from)
            return false;

        if (room->askForSkillInvoke(yangxiu, objectName(), data)) {
            QString choice = room->askForChoice(yangxiu, objectName(), "BasicCard+EquipCard+TrickCard");
            room->broadcastSkillInvoke(objectName());

            LogMessage log;
            log.type = "#Jilei";
            log.from = damage.from;
            log.arg = choice;
            room->sendLog(log);

            QStringList jilei_list = damage.from->tag[objectName()].toStringList();
            if (jilei_list.contains(choice)) return false;
            jilei_list.append(choice);
            damage.from->tag[objectName()] = QVariant::fromValue(jilei_list);
            QString _type = choice + "|.|.|hand"; // Handcards only
            room->setPlayerCardLimitation(damage.from, "use,response,discard", _type, true);

            QString type_name = choice.replace("Card", "").toLower();
            if (damage.from->getMark("@jilei_" + type_name) == 0)
                room->addPlayerMark(damage.from, "@jilei_" + type_name);
        }

        return false;
    }
};

class JileiClear : public TriggerSkill
{
public:
    JileiClear() : TriggerSkill("#jilei-clear")
    {
        events << EventPhaseChanging << Death;
    }

    int getPriority(TriggerEvent) const
    {
        return 5;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *target, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive)
                return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != target || target != room->getCurrent())
                return false;
        }
        QList<ServerPlayer *> players = room->getAllPlayers();
        foreach (ServerPlayer *player, players) {
            QStringList jilei_list = player->tag["jilei"].toStringList();
            if (!jilei_list.isEmpty()) {
                LogMessage log;
                log.type = "#JileiClear";
                log.from = player;
                room->sendLog(log);

                foreach (QString jilei_type, jilei_list) {
                    room->removePlayerCardLimitation(player, "use,response,discard", jilei_type + "|.|.|hand$1");
                    QString type_name = jilei_type.replace("Card", "").toLower();
                    room->setPlayerMark(player, "@jilei_" + type_name, 0);
                }
                player->tag.remove("jilei");
            }
        }

        return false;
    }
};

class Danlao : public TriggerSkill
{
public:
    Danlao() : TriggerSkill("danlao")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.to.length() <= 1 || !use.to.contains(player)
            || !use.card->isKindOf("TrickCard")
            || !room->askForSkillInvoke(player, objectName(), data))
            return false;

        room->broadcastSkillInvoke(objectName());
        player->setFlags("-DanlaoTarget");
        player->setFlags("DanlaoTarget");
        player->drawCards(1, objectName());
        if (player->isAlive() && player->hasFlag("DanlaoTarget")) {
            player->setFlags("-DanlaoTarget");
            use.nullified_list << player->objectName();
            data = QVariant::fromValue(use);
        }
        return false;
    }
};

Yongsi::Yongsi() : TriggerSkill("yongsi")
{
    events << DrawNCards << EventPhaseStart;
    frequency = Compulsory;
}

int Yongsi::getKingdoms(ServerPlayer *yuanshu) const
{
    QSet<QString> kingdom_set;
    Room *room = yuanshu->getRoom();
    foreach(ServerPlayer *p, room->getAlivePlayers())
        kingdom_set << p->getKingdom();

    return kingdom_set.size();
}

bool Yongsi::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *yuanshu, QVariant &data) const
{
    if (triggerEvent == DrawNCards) {
        int x = getKingdoms(yuanshu);
        data = data.toInt() + x;

        Room *room = yuanshu->getRoom();
        LogMessage log;
        log.type = "#YongsiGood";
        log.from = yuanshu;
        log.arg = QString::number(x);
        log.arg2 = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(yuanshu, objectName());

        room->broadcastSkillInvoke("yongsi", x % 2 + 1);
    } else if (triggerEvent == EventPhaseStart && yuanshu->getPhase() == Player::Discard) {
        int x = getKingdoms(yuanshu);
        LogMessage log;
        log.type = yuanshu->getCardCount() > x ? "#YongsiBad" : "#YongsiWorst";
        log.from = yuanshu;
        log.arg = QString::number(log.type == "#YongsiBad" ? x : yuanshu->getCardCount());
        log.arg2 = objectName();
        room->sendLog(log);
        room->notifySkillInvoked(yuanshu, objectName());
        if (x > 0)
            room->askForDiscard(yuanshu, "yongsi", x, x, false, true);
    }

    return false;
}

class WeidiViewAsSkill : public ViewAsSkill
{
public:
    WeidiViewAsSkill() : ViewAsSkill("weidi")
    {
    }

    static QList<const ViewAsSkill *> getLordViewAsSkills(const Player *player)
    {
        const Player *lord = NULL;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->isLord()) {
                lord = p;
                break;
            }
        }
        if (!lord) return QList<const ViewAsSkill *>();

        QList<const ViewAsSkill *> vs_skills;
        foreach (const Skill *skill, lord->getVisibleSkillList()) {
            if (skill->isLordSkill() && player->hasLordSkill(skill->objectName())) {
                const ViewAsSkill *vs = ViewAsSkill::parseViewAsSkill(skill);
                if (vs)
                    vs_skills << vs;
            }
        }
        return vs_skills;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        QList<const ViewAsSkill *> vs_skills = getLordViewAsSkills(player);
        foreach (const ViewAsSkill *skill, vs_skills) {
            if (skill->isEnabledAtPlay(player))
                return true;
        }
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        QList<const ViewAsSkill *> vs_skills = getLordViewAsSkills(player);
        foreach (const ViewAsSkill *skill, vs_skills) {
            if (skill->isEnabledAtResponse(player, pattern))
                return true;
        }
        return false;
    }

    bool isEnabledAtNullification(const ServerPlayer *player) const
    {
        QList<const ViewAsSkill *> vs_skills = getLordViewAsSkills(player);
        foreach (const ViewAsSkill *skill, vs_skills) {
            if (skill->isEnabledAtNullification(player))
                return true;
        }
        return false;
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        QString skill_name = Self->tag["weidi"].toString();
        if (skill_name.isEmpty()) return false;
        const ViewAsSkill *vs_skill = Sanguosha->getViewAsSkill(skill_name);
        if (vs_skill) return vs_skill->viewFilter(selected, to_select);
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        QString skill_name = Self->tag["weidi"].toString();
        if (skill_name.isEmpty()) return NULL;
        const ViewAsSkill *vs_skill = Sanguosha->getViewAsSkill(skill_name);
        if (vs_skill) return vs_skill->viewAs(cards);
        return NULL;
    }
};

WeidiDialog *WeidiDialog::getInstance()
{
    static WeidiDialog *instance;
    if (instance == NULL)
        instance = new WeidiDialog();

    return instance;
}

WeidiDialog::WeidiDialog()
{
    setObjectName("weidi");
    setWindowTitle(Sanguosha->translate("weidi"));
    group = new QButtonGroup(this);

    button_layout = new QVBoxLayout;
    setLayout(button_layout);
    connect(group, SIGNAL(buttonClicked(QAbstractButton *)), this, SLOT(selectSkill(QAbstractButton *)));
}

void WeidiDialog::popup()
{
    Self->tag.remove(objectName());
    foreach (QAbstractButton *button, group->buttons()) {
        button_layout->removeWidget(button);
        group->removeButton(button);
        delete button;
    }

    QList<const ViewAsSkill *> vs_skills = WeidiViewAsSkill::getLordViewAsSkills(Self);
    int count = 0;
    QString name;
    foreach (const ViewAsSkill *skill, vs_skills) {
        QAbstractButton *button = createSkillButton(skill->objectName());
        button->setEnabled(skill->isAvailable(Self, Sanguosha->currentRoomState()->getCurrentCardUseReason(),
            Sanguosha->currentRoomState()->getCurrentCardUsePattern()));
        if (button->isEnabled()) {
            count++;
            name = skill->objectName();
        }
        button_layout->addWidget(button);
    }

    if (count == 0) {
        emit onButtonClick();
        return;
    } else if (count == 1) {
        Self->tag[objectName()] = name;
        emit onButtonClick();
        return;
    }

    exec();
}

void WeidiDialog::selectSkill(QAbstractButton *button)
{
    Self->tag[objectName()] = button->objectName();
    emit onButtonClick();
    accept();
}

QAbstractButton *WeidiDialog::createSkillButton(const QString &skill_name)
{
    const Skill *skill = Sanguosha->getSkill(skill_name);
    if (!skill) return NULL;

    QCommandLinkButton *button = new QCommandLinkButton(Sanguosha->translate(skill_name));
    button->setObjectName(skill_name);
    button->setToolTip(skill->getDescription());

    group->addButton(button);
    return button;
}

class Weidi : public GameStartSkill
{
public:
    Weidi() : GameStartSkill("weidi")
    {
        frequency = Compulsory;
        view_as_skill = new WeidiViewAsSkill;
    }

    void onGameStart(ServerPlayer *) const
    {
        return;
    }

    QDialog *getDialog() const
    {
        return WeidiDialog::getInstance();
    }
};

class Yicong : public DistanceSkill
{
public:
    Yicong() : DistanceSkill("yicong")
    {
    }

    int getCorrect(const Player *from, const Player *to) const
    {
        int correct = 0;
        if (from->hasSkill(this) && from->getHp() > 2)
            correct--;
        if (to->hasSkill(this) && to->getHp() <= 2)
            correct++;

        return correct;
    }
};

class YicongEffect : public TriggerSkill
{
public:
    YicongEffect() : TriggerSkill("#yicong-effect")
    {
        events << HpChanged;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        int hp = player->getHp();
        int index = 0;
        int reduce = 0;
        if (data.canConvert<RecoverStruct>()) {
            int rec = data.value<RecoverStruct>().recover;
            if (hp > 2 && hp - rec <= 2)
                index = 1;
        } else {
            if (data.canConvert<DamageStruct>()) {
                DamageStruct damage = data.value<DamageStruct>();
                reduce = damage.damage;
            } else if (!data.isNull()) {
                reduce = data.toInt();
            }
            if (hp <= 2 && hp + reduce > 2)
                index = 2;
        }

        if (index > 0) {
            if (player->getGeneralName() == "gongsunzan"
                || (player->getGeneralName() != "st_gongsunzan" && player->getGeneral2Name() == "gongsunzan"))
                index += 2;
            room->broadcastSkillInvoke("yicong", index);
        }
        return false;
    }
};

class Danji : public PhaseChangeSkill
{
public:
    Danji() : PhaseChangeSkill("danji")
    { // What a silly skill!
        frequency = Wake;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return PhaseChangeSkill::triggerable(target)
            && target->getPhase() == Player::Start
            && target->getMark("danji") == 0
            && target->getHandcardNum() > target->getHp();
    }

    bool onPhaseChange(ServerPlayer *guanyu) const
    {
        Room *room = guanyu->getRoom();
        ServerPlayer *the_lord = room->getLord();
        if (the_lord && (the_lord->getGeneralName().contains("caocao") || the_lord->getGeneral2Name().contains("caocao"))) {
            room->notifySkillInvoked(guanyu, objectName());

            LogMessage log;
            log.type = "#DanjiWake";
            log.from = guanyu;
            log.arg = QString::number(guanyu->getHandcardNum());
            log.arg2 = QString::number(guanyu->getHp());
            room->sendLog(log);
            room->broadcastSkillInvoke(objectName());
            //room->doLightbox("$DanjiAnimate", 5000);

            room->doSuperLightbox("sp_guanyu", "danji");

            room->setPlayerMark(guanyu, "danji", 1);
            if (room->changeMaxHpForAwakenSkill(guanyu) && guanyu->getMark("danji") == 1)
                room->acquireSkill(guanyu, "mashu");
        }

        return false;
    }
};

YuanhuCard::YuanhuCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool YuanhuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    if (!targets.isEmpty())
        return false;

    const Card *card = Sanguosha->getCard(subcards.first());
    const EquipCard *equip = qobject_cast<const EquipCard *>(card->getRealCard());
    int equip_index = static_cast<int>(equip->location());
    return to_select->getEquip(equip_index) == NULL;
}

void YuanhuCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    int index = -1;
    if (card_use.to.first() == card_use.from)
        index = 5;
    else if (card_use.to.first()->getGeneralName().contains("caocao"))
        index = 4;
    else {
        const Card *card = Sanguosha->getCard(card_use.card->getSubcards().first());
        if (card->isKindOf("Weapon"))
            index = 1;
        else if (card->isKindOf("Armor"))
            index = 2;
        else if (card->isKindOf("Horse"))
            index = 3;
    }
    room->broadcastSkillInvoke("yuanhu", index);
    SkillCard::onUse(room, card_use);
}

void YuanhuCard::onEffect(const CardEffectStruct &effect) const
{
    ServerPlayer *caohong = effect.from;
    Room *room = caohong->getRoom();
    room->moveCardTo(this, caohong, effect.to, Player::PlaceEquip,
        CardMoveReason(CardMoveReason::S_REASON_PUT, caohong->objectName(), "yuanhu", QString()));

    const Card *card = Sanguosha->getCard(subcards.first());

    LogMessage log;
    log.type = "$ZhijianEquip";
    log.from = effect.to;
    log.card_str = QString::number(card->getEffectiveId());
    room->sendLog(log);

    if (card->isKindOf("Weapon")) {
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (effect.to->distanceTo(p) == 1 && caohong->canDiscard(p, "hej"))
                targets << p;
        }
        if (!targets.isEmpty()) {
            ServerPlayer *to_dismantle = room->askForPlayerChosen(caohong, targets, "yuanhu", "@yuanhu-discard:" + effect.to->objectName());
            int card_id = room->askForCardChosen(caohong, to_dismantle, "hej", "yuanhu", false, Card::MethodDiscard);
            room->throwCard(Sanguosha->getCard(card_id), to_dismantle, caohong);
        }
    } else if (card->isKindOf("Armor")) {
        effect.to->drawCards(1, "yuanhu");
    } else if (card->isKindOf("Horse")) {
        room->recover(effect.to, RecoverStruct(effect.from));
    }
}

class YuanhuViewAsSkill : public OneCardViewAsSkill
{
public:
    YuanhuViewAsSkill() : OneCardViewAsSkill("yuanhu")
    {
        filter_pattern = "EquipCard";
        response_pattern = "@@yuanhu";
    }

    const Card *viewAs(const Card *originalcard) const
    {
        YuanhuCard *first = new YuanhuCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        return first;
    }
};

class Yuanhu : public PhaseChangeSkill
{
public:
    Yuanhu() : PhaseChangeSkill("yuanhu")
    {
        view_as_skill = new YuanhuViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *target) const
    {
        Room *room = target->getRoom();
        if (target->getPhase() == Player::Finish && !target->isNude())
            room->askForUseCard(target, "@@yuanhu", "@yuanhu-equip", -1, Card::MethodNone);
        return false;
    }
};

XuejiCard::XuejiCard()
{
}

bool XuejiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (targets.length() >= Self->getLostHp())
        return false;

    if (to_select == Self)
        return false;

    int range_fix = 0;
    if (Self->getWeapon() && Self->getWeapon()->getEffectiveId() == getEffectiveId()) {
        const Weapon *weapon = qobject_cast<const Weapon *>(Self->getWeapon()->getRealCard());
        range_fix += weapon->getRange() - Self->getAttackRange(false);
    } else if (Self->getOffensiveHorse() && Self->getOffensiveHorse()->getEffectiveId() == getEffectiveId())
        range_fix += 1;

    return Self->inMyAttackRange(to_select, range_fix);
}

void XuejiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    DamageStruct damage;
    damage.from = source;
    damage.reason = "xueji";

    foreach (ServerPlayer *p, targets) {
        damage.to = p;
        room->damage(damage);
    }
    foreach (ServerPlayer *p, targets) {
        if (p->isAlive())
            p->drawCards(1, "xueji");
    }
}

class Xueji : public OneCardViewAsSkill
{
public:
    Xueji() : OneCardViewAsSkill("xueji")
    {
        filter_pattern = ".|red!";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getLostHp() > 0 && player->canDiscard(player, "he") && !player->hasUsed("XuejiCard");
    }

    const Card *viewAs(const Card *originalcard) const
    {
        XuejiCard *first = new XuejiCard;
        first->addSubcard(originalcard->getId());
        first->setSkillName(objectName());
        return first;
    }
};

class Huxiao : public TargetModSkill
{
public:
    Huxiao() : TargetModSkill("huxiao")
    {
    }

    int getResidueNum(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill(this))
            return from->getMark(objectName());
        else
            return 0;
    }
};

class HuxiaoCount : public TriggerSkill
{
public:
    HuxiaoCount() : TriggerSkill("#huxiao-count")
    {
        events << SlashMissed << EventPhaseChanging;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == SlashMissed) {
            if (player->getPhase() == Player::Play)
                room->addPlayerMark(player, "huxiao");
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.from == Player::Play)
                if (player->getMark("huxiao") > 0)
                    room->setPlayerMark(player, "huxiao", 0);
        }

        return false;
    }
};

class HuxiaoClear : public DetachEffectSkill
{
public:
    HuxiaoClear() : DetachEffectSkill("huxiao")
    {
    }

    void onSkillDetached(Room *room, ServerPlayer *player) const
    {
        room->setPlayerMark(player, "huxiao", 0);
    }
};

class Wuji : public PhaseChangeSkill
{
public:
    Wuji() : PhaseChangeSkill("wuji")
    {
        frequency = Wake;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return PhaseChangeSkill::triggerable(target)
            && target->getPhase() == Player::Finish
            && target->getMark("wuji") == 0
            && target->getMark("damage_point_round") >= 3;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        room->notifySkillInvoked(player, objectName());

        LogMessage log;
        log.type = "#WujiWake";
        log.from = player;
        log.arg = QString::number(player->getMark("damage_point_round"));
        log.arg2 = objectName();
        room->sendLog(log);

        room->broadcastSkillInvoke(objectName());
        //room->doLightbox("$WujiAnimate", 4000);

        room->doSuperLightbox("guanyinping", "wuji");

        room->setPlayerMark(player, "wuji", 1);
        if (room->changeMaxHpForAwakenSkill(player, 1)) {
            room->recover(player, RecoverStruct(player));
            if (player->getMark("wuji") == 1)
                room->detachSkillFromPlayer(player, "huxiao");
        }

        return false;
    }
};

class Baobian : public TriggerSkill
{
public:
    Baobian() : TriggerSkill("baobian")
    {
        events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventLoseSkill) {
            if (data.toString() == objectName()) {
                QStringList baobian_skills = player->tag["BaobianSkills"].toStringList();
                QStringList detachList;
                foreach(QString skill_name, baobian_skills)
                    detachList.append("-" + skill_name);
                room->handleAcquireDetachSkills(player, detachList);
                player->tag["BaobianSkills"] = QVariant();
            }
            return false;
        } else if (triggerEvent == EventAcquireSkill) {
            if (data.toString() != objectName()) return false;
        }

        if (!player->isAlive() || !player->hasSkill(this, true)) return false;

        acquired_skills.clear();
        detached_skills.clear();
        BaobianChange(room, player, 1, "shensu");
        BaobianChange(room, player, 2, "paoxiao");
        BaobianChange(room, player, 3, "tiaoxin");
        if (!acquired_skills.isEmpty() || !detached_skills.isEmpty())
            room->handleAcquireDetachSkills(player, acquired_skills + detached_skills);
        return false;
    }

private:
    void BaobianChange(Room *room, ServerPlayer *player, int hp, const QString &skill_name) const
    {
        QStringList baobian_skills = player->tag["BaobianSkills"].toStringList();
        if (player->getHp() <= hp) {
            if (!baobian_skills.contains(skill_name)) {
                room->notifySkillInvoked(player, "baobian");
                if (player->getHp() == hp)
                    room->broadcastSkillInvoke("baobian", 4 - hp);
                acquired_skills.append(skill_name);
                baobian_skills << skill_name;
            }
        } else {
            if (baobian_skills.contains(skill_name)) {
                detached_skills.append("-" + skill_name);
                baobian_skills.removeOne(skill_name);
            }
        }
        player->tag["BaobianSkills"] = QVariant::fromValue(baobian_skills);
    }

    mutable QStringList acquired_skills, detached_skills;
};

BifaCard::BifaCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool BifaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getPile("bifa").isEmpty() && to_select != Self;
}

void BifaCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();
    target->tag["BifaSource" + QString::number(getEffectiveId())] = QVariant::fromValue(source);
    target->addToPile("bifa", this, false);
}

class BifaViewAsSkill : public OneCardViewAsSkill
{
public:
    BifaViewAsSkill() : OneCardViewAsSkill("bifa")
    {
        filter_pattern = ".|.|.|hand";
        response_pattern = "@@bifa";
    }

    const Card *viewAs(const Card *originalcard) const
    {
        Card *card = new BifaCard;
        card->addSubcard(originalcard);
        return card;
    }
};

class Bifa : public TriggerSkill
{
public:
    Bifa() : TriggerSkill("bifa")
    {
        events << EventPhaseStart;
        view_as_skill = new BifaViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (TriggerSkill::triggerable(player) && player->getPhase() == Player::Finish && !player->isKongcheng()) {
            room->askForUseCard(player, "@@bifa", "@bifa-remove", -1, Card::MethodNone);
        } else if (player->getPhase() == Player::RoundStart && player->getPile("bifa").length() > 0) {
            int card_id = player->getPile("bifa").first();
            ServerPlayer *chenlin = player->tag["BifaSource" + QString::number(card_id)].value<ServerPlayer *>();
            QList<int> ids;
            ids << card_id;

            LogMessage log;
            log.type = "$BifaView";
            log.from = player;
            log.card_str = QString::number(card_id);
            log.arg = "bifa";
            room->sendLog(log, player);

            room->fillAG(ids, player);
            const Card *cd = Sanguosha->getCard(card_id);
            QString pattern;
            if (cd->isKindOf("BasicCard"))
                pattern = "BasicCard";
            else if (cd->isKindOf("TrickCard"))
                pattern = "TrickCard";
            else if (cd->isKindOf("EquipCard"))
                pattern = "EquipCard";
            QVariant data_for_ai = QVariant::fromValue(pattern);
            pattern.append("|.|.|hand");
            const Card *to_give = NULL;
            if (!player->isKongcheng() && chenlin && chenlin->isAlive())
                to_give = room->askForCard(player, pattern, "@bifa-give", data_for_ai, Card::MethodNone, chenlin);
            if (chenlin && to_give) {
                room->broadcastSkillInvoke(objectName(), 2);
                CardMoveReason reasonG(CardMoveReason::S_REASON_GIVE, player->objectName(), chenlin->objectName(), "bifa", QString());
                room->obtainCard(chenlin, to_give, reasonG, false);
                CardMoveReason reason(CardMoveReason::S_REASON_EXCHANGE_FROM_PILE, player->objectName(), "bifa", QString());
                room->obtainCard(player, cd, reason, false);
            } else {
                room->broadcastSkillInvoke(objectName(), 3);
                CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), objectName(), QString());
                room->throwCard(cd, reason, NULL);
                room->loseHp(player);
            }
            room->clearAG(player);
            player->tag.remove("BifaSource" + QString::number(card_id));
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return 1;
    }
};

SongciCard::SongciCard()
{
    mute = true;
}

bool SongciCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getMark("songci" + Self->objectName()) == 0 && to_select->getHandcardNum() != to_select->getHp();
}

void SongciCard::onEffect(const CardEffectStruct &effect) const
{
    int handcard_num = effect.to->getHandcardNum();
    int hp = effect.to->getHp();
    Room *room = effect.from->getRoom();
    room->setPlayerMark(effect.to, "@songci", 1);
    room->addPlayerMark(effect.to, "songci" + effect.from->objectName());
    if (handcard_num > hp) {
        room->broadcastSkillInvoke("songci", 2);
        room->askForDiscard(effect.to, "songci", 2, 2, false, true);
    } else if (handcard_num < hp) {
        room->broadcastSkillInvoke("songci", 1);
        effect.to->drawCards(2, "songci");
    }
}

class SongciViewAsSkill : public ZeroCardViewAsSkill
{
public:
    SongciViewAsSkill() : ZeroCardViewAsSkill("songci")
    {
    }

    const Card *viewAs() const
    {
        return new SongciCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (player->getMark("songci" + player->objectName()) == 0 && player->getHandcardNum() != player->getHp()) return true;
        foreach(const Player *sib, player->getAliveSiblings())
            if (sib->getMark("songci" + player->objectName()) == 0 && sib->getHandcardNum() != sib->getHp())
                return true;
        return false;
    }
};

class Songci : public TriggerSkill
{
public:
    Songci() : TriggerSkill("songci")
    {
        events << Death;
        view_as_skill = new SongciViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->hasSkill(this);
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (death.who != player) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getMark("@songci") > 0)
                room->setPlayerMark(p, "@songci", 0);
            if (p->getMark("songci" + player->objectName()) > 0)
                room->setPlayerMark(p, "songci" + player->objectName(), 0);
        }
        return false;
    }
};

class Xiuluo : public PhaseChangeSkill
{
public:
    Xiuluo() : PhaseChangeSkill("xiuluo")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return PhaseChangeSkill::triggerable(target)
            && target->getPhase() == Player::Start
            && target->canDiscard(target, "h")
            && hasDelayedTrick(target);
    }

    bool onPhaseChange(ServerPlayer *target) const
    {
        Room *room = target->getRoom();
        while (hasDelayedTrick(target) && target->canDiscard(target, "h")) {
            QStringList suits;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (!suits.contains(jcard->getSuitString()))
                    suits << jcard->getSuitString();
            }

            const Card *card = room->askForCard(target, QString(".|%1|.|hand").arg(suits.join(",")),
                "@xiuluo", QVariant(), objectName());
            if (!card || !hasDelayedTrick(target)) break;
            room->broadcastSkillInvoke(objectName());

            QList<int> avail_list, other_list;
            foreach (const Card *jcard, target->getJudgingArea()) {
                if (jcard->isKindOf("SkillCard")) continue;
                if (jcard->getSuit() == card->getSuit())
                    avail_list << jcard->getEffectiveId();
                else
                    other_list << jcard->getEffectiveId();
            }
            room->fillAG(avail_list + other_list, NULL, other_list);
            int id = room->askForAG(target, avail_list, false, objectName());
            room->clearAG();
            room->throwCard(id, NULL);
        }

        return false;
    }

private:
    static bool hasDelayedTrick(const ServerPlayer *target)
    {
        foreach(const Card *card, target->getJudgingArea())
            if (!card->isKindOf("SkillCard")) return true;
        return false;
    }
};

class Shenwei : public DrawCardsSkill
{
public:
    Shenwei() : DrawCardsSkill("#shenwei-draw")
    {
        frequency = Compulsory;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        Room *room = player->getRoom();

        room->broadcastSkillInvoke("shenwei");
        room->sendCompulsoryTriggerLog(player, "shenwei");

        return n + 2;
    }
};

class ShenweiKeep : public MaxCardsSkill
{
public:
    ShenweiKeep() : MaxCardsSkill("shenwei")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill(this))
            return 2;
        else
            return 0;
    }
};

class Shenji : public TargetModSkill
{
public:
    Shenji() : TargetModSkill("shenji")
    {
    }

    int getExtraTargetNum(const Player *from, const Card *) const
    {
        if (from->hasSkill(this) && from->getWeapon() == NULL)
            return 2;
        else
            return 0;
    }
};

class Xingwu : public TriggerSkill
{
public:
    Xingwu() : TriggerSkill("xingwu")
    {
        events << PreCardUsed << CardResponded << EventPhaseStart << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == PreCardUsed || triggerEvent == CardResponded) {
            const Card *card = NULL;
            if (triggerEvent == PreCardUsed)
                card = data.value<CardUseStruct>().card;
            else {
                CardResponseStruct response = data.value<CardResponseStruct>();
                if (response.m_isUse)
                    card = response.m_card;
            }
            if (card && card->getTypeId() != Card::TypeSkill && card->getHandlingMethod() == Card::MethodUse) {
                int n = player->getMark(objectName());
                if (card->isBlack())
                    n |= 1;
                else if (card->isRed())
                    n |= 2;
                player->setMark(objectName(), n);
            }
        } else if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() == Player::Discard) {
                int n = player->getMark(objectName());
                bool red_avail = ((n & 2) == 0), black_avail = ((n & 1) == 0);
                if (player->isKongcheng() || (!red_avail && !black_avail))
                    return false;
                QString pattern = ".|.|.|hand";
                if (red_avail != black_avail)
                    pattern = QString(".|%1|.|hand").arg(red_avail ? "red" : "black");
                const Card *card = room->askForCard(player, pattern, "@xingwu", QVariant(), Card::MethodNone);
                if (card) {
                    room->notifySkillInvoked(player, objectName());
                    room->broadcastSkillInvoke(objectName(), 1);

                    LogMessage log;
                    log.type = "#InvokeSkill";
                    log.from = player;
                    log.arg = objectName();
                    room->sendLog(log);

                    player->addToPile(objectName(), card);
                }
            } else if (player->getPhase() == Player::RoundStart) {
                player->setMark(objectName(), 0);
            }
        } else if (triggerEvent == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceSpecial && player->getPile(objectName()).length() >= 3) {
                player->clearOnePrivatePile(objectName());
                QList<ServerPlayer *> males;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->isMale())
                        males << p;
                }
                if (males.isEmpty()) return false;

                ServerPlayer *target = room->askForPlayerChosen(player, males, objectName(), "@xingwu-choose");
                room->broadcastSkillInvoke(objectName(), 2);
                room->damage(DamageStruct(objectName(), player, target, 2));

                if (!player->isAlive()) return false;
                QList<const Card *> equips = target->getEquips();
                if (!equips.isEmpty()) {
                    DummyCard *dummy = new DummyCard;
                    foreach (const Card *equip, equips) {
                        if (player->canDiscard(target, equip->getEffectiveId()))
                            dummy->addSubcard(equip);
                    }
                    if (dummy->subcardsLength() > 0)
                        room->throwCard(dummy, target, player);
                    delete dummy;
                }
            }
        }
        return false;
    }
};

class Luoyan : public TriggerSkill
{
public:
    Luoyan() : TriggerSkill("luoyan")
    {
        events << CardsMoveOneTime << EventAcquireSkill << EventLoseSkill;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventLoseSkill && data.toString() == objectName()) {
            room->handleAcquireDetachSkills(player, "-tianxiang|-liuli", true);
        } else if (triggerEvent == EventAcquireSkill && data.toString() == objectName()) {
            if (!player->getPile("xingwu").isEmpty()) {
                room->notifySkillInvoked(player, objectName());
                room->handleAcquireDetachSkills(player, "tianxiang|liuli");
            }
        } else if (triggerEvent == CardsMoveOneTime && player->isAlive() && player->hasSkill(this, true)) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to == player && move.to_place == Player::PlaceSpecial && move.to_pile_name == "xingwu") {
                if (player->getPile("xingwu").length() == 1) {
                    room->notifySkillInvoked(player, objectName());
                    room->handleAcquireDetachSkills(player, "tianxiang|liuli");
                }
            } else if (move.from == player && move.from_places.contains(Player::PlaceSpecial)
                && move.from_pile_names.contains("xingwu")) {
                if (player->getPile("xingwu").isEmpty())
                    room->handleAcquireDetachSkills(player, "-tianxiang|-liuli", true);
            }
        }
        return false;
    }
};

class Yanyu : public TriggerSkill
{
public:
    Yanyu() : TriggerSkill("yanyu")
    {
        events << EventPhaseStart << BeforeCardsMove << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            ServerPlayer *xiahou = room->findPlayerBySkillName(objectName());
            if (xiahou && player->getPhase() == Player::Play) {
                if (!xiahou->canDiscard(xiahou, "he")) return false;
                const Card *card = room->askForCard(xiahou, "..", "@yanyu-discard", QVariant(), objectName());
                if (card) {
                    room->broadcastSkillInvoke(objectName(), 1);
                    xiahou->addMark("YanyuDiscard" + QString::number(card->getTypeId()), 3);
                }
            }
        } else if (triggerEvent == BeforeCardsMove && TriggerSkill::triggerable(player)) {
            ServerPlayer *current = room->getCurrent();
            if (!current || current->getPhase() != Player::Play) return false;
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to_place == Player::DiscardPile) {
                QList<int> ids, disabled;
                QList<int> all_ids = move.card_ids;
                foreach (int id, move.card_ids) {
                    const Card *card = Sanguosha->getCard(id);
                    if (player->getMark("YanyuDiscard" + QString::number(card->getTypeId())) > 0)
                        ids << id;
                    else
                        disabled << id;
                }
                if (ids.isEmpty()) return false;
                while (!ids.isEmpty()) {
                    room->fillAG(all_ids, player, disabled);
                    bool only = (all_ids.length() == 1);
                    int card_id = -1;
                    if (only)
                        card_id = ids.first();
                    else
                        card_id = room->askForAG(player, ids, true, objectName());
                    room->clearAG(player);
                    if (card_id == -1) break;
                    if (only)
                        player->setMark("YanyuOnlyId", card_id + 1); // For AI
                    const Card *card = Sanguosha->getCard(card_id);
                    ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(),
                        QString("@yanyu-give:::%1:%2\\%3").arg(card->objectName())
                        .arg(card->getSuitString() + "_char")
                        .arg(card->getNumberString()),
                        only, true);
                    player->setMark("YanyuOnlyId", 0);
                    if (target) {
                        player->removeMark("YanyuDiscard" + QString::number(card->getTypeId()));
                        Player::Place place = move.from_places.at(move.card_ids.indexOf(card_id));
                        QList<int> _card_id;
                        _card_id << card_id;
                        move.removeCardIds(_card_id);
                        data = QVariant::fromValue(move);
                        ids.removeOne(card_id);
                        disabled << card_id;
                        foreach (int id, ids) {
                            const Card *card = Sanguosha->getCard(id);
                            if (player->getMark("YanyuDiscard" + QString::number(card->getTypeId())) == 0) {
                                ids.removeOne(id);
                                disabled << id;
                            }
                        }
                        if (move.from && move.from->objectName() == target->objectName() && place != Player::PlaceTable) {
                            // just indicate which card she chose...
                            LogMessage log;
                            log.type = "$MoveCard";
                            log.from = target;
                            log.to << target;
                            log.card_str = QString::number(card_id);
                            room->sendLog(log);
                        }

                        room->broadcastSkillInvoke(objectName(), 2);
                        target->obtainCard(card);
                    } else
                        break;
                }
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    p->setMark("YanyuDiscard1", 0);
                    p->setMark("YanyuDiscard2", 0);
                    p->setMark("YanyuDiscard3", 0);
                }
            }
        }
        return false;
    }
};

class Xiaode : public TriggerSkill
{
public:
    Xiaode() : TriggerSkill("xiaode")
    {
        events << BuryVictim;
    }

    int getPriority(TriggerEvent) const
    {
        return -2;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
    {
        ServerPlayer *xiahoushi = room->findPlayerBySkillName(objectName());
        if (!xiahoushi || !xiahoushi->tag["XiaodeSkill"].toString().isEmpty()) return false;
        QStringList skill_list = xiahoushi->tag["XiaodeVictimSkills"].toStringList();
        if (skill_list.isEmpty()) return false;
        if (!room->askForSkillInvoke(xiahoushi, objectName(), QVariant::fromValue(skill_list))) return false;
        QString skill_name = room->askForChoice(xiahoushi, objectName(), skill_list.join("+"));
        room->broadcastSkillInvoke(objectName());
        xiahoushi->tag["XiaodeSkill"] = skill_name;
        room->acquireSkill(xiahoushi, skill_name);
        return false;
    }
};

class XiaodeEx : public TriggerSkill
{
public:
    XiaodeEx() : TriggerSkill("#xiaode")
    {
        events << EventPhaseChanging << EventLoseSkill << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                QString skill_name = player->tag["XiaodeSkill"].toString();
                if (!skill_name.isEmpty()) {
                    room->detachSkillFromPlayer(player, skill_name, false, true);
                    player->tag.remove("XiaodeSkill");
                }
            }
        } else if (triggerEvent == EventLoseSkill && data.toString() == "xiaode") {
            QString skill_name = player->tag["XiaodeSkill"].toString();
            if (!skill_name.isEmpty()) {
                room->detachSkillFromPlayer(player, skill_name, false, true);
                player->tag.remove("XiaodeSkill");
            }
        } else if (triggerEvent == Death && TriggerSkill::triggerable(player)) {
            DeathStruct death = data.value<DeathStruct>();
            QStringList skill_list;
            skill_list.append(addSkillList(death.who->getGeneral()));
            skill_list.append(addSkillList(death.who->getGeneral2()));
            player->tag["XiaodeVictimSkills"] = QVariant::fromValue(skill_list);
        }
        return false;
    }

private:
    QStringList addSkillList(const General *general) const
    {
        if (!general) return QStringList();
        QStringList skill_list;
        foreach (const Skill *skill, general->getSkillList()) {
            if (skill->isVisible() && !skill->isLordSkill() && skill->getFrequency() != Skill::Wake)
                skill_list.append(skill->objectName());
        }
        return skill_list;
    }
};

ZhoufuCard::ZhoufuCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool ZhoufuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->getPile("incantation").isEmpty();
}

void ZhoufuCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    ServerPlayer *target = targets.first();
    target->tag["ZhoufuSource" + QString::number(getEffectiveId())] = QVariant::fromValue(source);
    target->addToPile("incantation", this);
}

class ZhoufuViewAsSkill : public OneCardViewAsSkill
{
public:
    ZhoufuViewAsSkill() : OneCardViewAsSkill("zhoufu")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("ZhoufuCard");
    }

    const Card *viewAs(const Card *originalcard) const
    {
        Card *card = new ZhoufuCard;
        card->addSubcard(originalcard);
        return card;
    }
};

class Zhoufu : public TriggerSkill
{
public:
    Zhoufu() : TriggerSkill("zhoufu")
    {
        events << StartJudge << EventPhaseChanging;
        view_as_skill = new ZhoufuViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->getPile("incantation").length() > 0;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == StartJudge) {
            int card_id = player->getPile("incantation").first();

            JudgeStruct *judge = data.value<JudgeStruct *>();
            judge->card = Sanguosha->getCard(card_id);

            LogMessage log;
            log.type = "$ZhoufuJudge";
            log.from = player;
            log.arg = objectName();
            log.card_str = QString::number(judge->card->getEffectiveId());
            room->sendLog(log);

            room->moveCardTo(judge->card, NULL, judge->who, Player::PlaceJudge,
                CardMoveReason(CardMoveReason::S_REASON_JUDGE,
                judge->who->objectName(),
                QString(), QString(), judge->reason), true);
            judge->updateResult();
            room->setTag("SkipGameRule", true);
        } else {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                int id = player->getPile("incantation").first();
                ServerPlayer *zhangbao = player->tag["ZhoufuSource" + QString::number(id)].value<ServerPlayer *>();
                if (zhangbao && zhangbao->isAlive())
                    zhangbao->obtainCard(Sanguosha->getCard(id));
            }
        }
        return false;
    }
};

class Yingbing : public TriggerSkill
{
public:
    Yingbing() : TriggerSkill("yingbing")
    {
        events << StartJudge;
        frequency = Frequent;
    }

    int getPriority(TriggerEvent) const
    {
        return -1;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        JudgeStruct *judge = data.value<JudgeStruct *>();
        int id = judge->card->getEffectiveId();
        ServerPlayer *zhangbao = player->tag["ZhoufuSource" + QString::number(id)].value<ServerPlayer *>();
        if (zhangbao && TriggerSkill::triggerable(zhangbao)
            && zhangbao->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName());
            zhangbao->drawCards(2, "yingbing");
        }
        return false;
    }
};

class Kangkai : public TriggerSkill
{
public:
    Kangkai() : TriggerSkill("kangkai")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash")) {
            foreach (ServerPlayer *to, use.to) {
                if (!player->isAlive()) break;
                if (player->distanceTo(to) <= 1 && TriggerSkill::triggerable(player)) {
                    player->tag["KangkaiSlash"] = data;
                    bool will_use = room->askForSkillInvoke(player, objectName(), QVariant::fromValue(to));
                    player->tag.remove("KangkaiSlash");
                    if (!will_use) continue;

                    room->broadcastSkillInvoke(objectName());

                    player->drawCards(1, "kangkai");
                    if (!player->isNude() && player != to) {
                        const Card *card = NULL;
                        if (player->getCardCount() > 1) {
                            card = room->askForCard(player, "..!", "@kangkai-give:" + to->objectName(), data, Card::MethodNone);
                            if (!card)
                                card = player->getCards("he").at(qrand() % player->getCardCount());
                        } else {
                            Q_ASSERT(player->getCardCount() == 1);
                            card = player->getCards("he").first();
                        }
                        CardMoveReason r(CardMoveReason::S_REASON_GIVE, player->objectName(), objectName(), QString());
                        room->obtainCard(to, card, r);
                        if (card->getTypeId() == Card::TypeEquip && room->getCardOwner(card->getEffectiveId()) == to && !to->isLocked(card)) {
                            to->tag["KangkaiSlash"] = data;
                            to->tag["KangkaiGivenCard"] = QVariant::fromValue(card);
                            bool will_use = room->askForSkillInvoke(to, "kangkai_use", "use");
                            to->tag.remove("KangkaiSlash");
                            to->tag.remove("KangkaiGivenCard");
                            if (will_use)
                                room->useCard(CardUseStruct(card, to, to));
                        }
                    }
                }
            }
        }
        return false;
    }
};

class Shenxian : public TriggerSkill
{
public:
    Shenxian() : TriggerSkill("shenxian")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (player->getPhase() == Player::NotActive && move.from && move.from->isAlive()
            && move.from->objectName() != player->objectName()
            && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))
            && (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) {
            foreach (int id, move.card_ids) {
                if (Sanguosha->getCard(id)->getTypeId() == Card::TypeBasic) {
                    if (room->askForSkillInvoke(player, objectName(), data)) {
                        room->broadcastSkillInvoke(objectName());
                        player->drawCards(1, "shenxian");
                    }
                    break;
                }
            }
        }
        return false;
    }
};

QiangwuCard::QiangwuCard()
{
    target_fixed = true;
}

void QiangwuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    JudgeStruct judge;
    judge.pattern = ".";
    judge.who = source;
    judge.reason = "qiangwu";
    judge.play_animation = false;
    room->judge(judge);

    bool ok = false;
    int num = judge.pattern.toInt(&ok);
    if (ok)
        room->setPlayerMark(source, "qiangwu", num);
}

class QiangwuViewAsSkill : public ZeroCardViewAsSkill
{
public:
    QiangwuViewAsSkill() : ZeroCardViewAsSkill("qiangwu")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("QiangwuCard");
    }

    const Card *viewAs() const
    {
        return new QiangwuCard;
    }
};

class Qiangwu : public TriggerSkill
{
public:
    Qiangwu() : TriggerSkill("qiangwu")
    {
        events << EventPhaseChanging << PreCardUsed << FinishJudge;
        view_as_skill = new QiangwuViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive)
                room->setPlayerMark(player, "qiangwu", 0);
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == "qiangwu")
                judge->pattern = QString::number(judge->card->getNumber());
        } else if (triggerEvent == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("Slash") && player->getMark("qiangwu") > 0
                && use.card->getNumber() > player->getMark("qiangwu")) {
                if (use.m_addHistory) {
                    room->addPlayerHistory(player, use.card->getClassName(), -1);
                    use.m_addHistory = false;
                    data = QVariant::fromValue(use);
                }
            }
        }
        return false;
    }
};

class QiangwuTargetMod : public TargetModSkill
{
public:
    QiangwuTargetMod() : TargetModSkill("#qiangwu-target")
    {
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (card->getNumber() < from->getMark("qiangwu"))
            return 1000;
        else
            return 0;
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (from->getMark("qiangwu") > 0
            && (card->getNumber() > from->getMark("qiangwu") || card->hasFlag("Global_SlashAvailabilityChecker")))
            return 1000;
        else
            return 0;
    }
};

class Meibu : public TriggerSkill
{
public:
    Meibu() : TriggerSkill("meibu")
    {
        events << EventPhaseStart << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Play) {
            foreach (ServerPlayer *sunluyu, room->getOtherPlayers(player)) {
                if (!player->inMyAttackRange(sunluyu) && TriggerSkill::triggerable(sunluyu) && room->askForSkillInvoke(sunluyu, objectName())) {
                    room->broadcastSkillInvoke(objectName());
                    if (!player->hasSkill("#meibu-filter", true)) {
                        room->acquireSkill(player, "#meibu-filter", false);
                        room->filterCards(player, player->getCards("he"), false);
                    }
                    QVariantList sunluyus = player->tag[objectName()].toList();
                    sunluyus << QVariant::fromValue(sunluyu);
                    player->tag[objectName()] = QVariant::fromValue(sunluyus);
                    room->insertAttackRangePair(player, sunluyu);
                }
            }
        } else if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;

            QVariantList sunluyus = player->tag[objectName()].toList();
            foreach (QVariant sunluyu, sunluyus) {
                ServerPlayer *s = sunluyu.value<ServerPlayer *>();
                room->removeAttackRangePair(player, s);
            }
            room->detachSkillFromPlayer(player, "#meibu-filter");

            player->tag[objectName()] = QVariantList();

            room->filterCards(player, player->getCards("he"), true);
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *card) const
    {
        if (card->isKindOf("Slash"))
            return -2;
        
        return -1;
    }
};


MeibuFilter::MeibuFilter(const QString &skill_name) : FilterSkill(QString("#%1-filter").arg(skill_name)), n(skill_name)
{

}

bool MeibuFilter::viewFilter(const Card *to_select) const
{
    return to_select->getTypeId() == Card::TypeTrick;
}

const Card * MeibuFilter::viewAs(const Card *originalCard) const
{
    Slash *slash = new Slash(originalCard->getSuit(), originalCard->getNumber());
    slash->setSkillName("_" + n);
    WrappedCard *card = Sanguosha->getWrappedCard(originalCard->getId());
    card->takeOver(slash);
    return card;
}

class Mumu : public TriggerSkill
{
public:
    Mumu() : TriggerSkill("mumu")
    {
        events << EventPhaseStart;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() == Player::Finish && player->getMark("damage_point_play_phase") == 0) {
            QList<ServerPlayer *> weapon_players, armor_players;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->getWeapon() && player->canDiscard(p, p->getWeapon()->getEffectiveId()))
                    weapon_players << p;
                if (p != player && p->getArmor())
                    armor_players << p;
            }
            QStringList choices;
            choices << "cancel";
            if (!armor_players.isEmpty()) choices.prepend("armor");
            if (!weapon_players.isEmpty()) choices.prepend("weapon");
            if (choices.length() == 1) return false;
            QString choice = room->askForChoice(player, objectName(), choices.join("+"));
            if (choice == "cancel") {
                return false;
            } else {
                room->notifySkillInvoked(player, objectName());
                if (choice == "weapon") {
                    room->broadcastSkillInvoke(objectName(), 1);
                    ServerPlayer *victim = room->askForPlayerChosen(player, weapon_players, objectName(), "@mumu-weapon");
                    room->throwCard(victim->getWeapon(), victim, player);
                    player->drawCards(1, objectName());
                } else {
                    room->broadcastSkillInvoke(objectName(), 2);
                    ServerPlayer *victim = room->askForPlayerChosen(player, armor_players, objectName(), "@mumu-armor");
                    int equip = victim->getArmor()->getEffectiveId();
                    QList<CardsMoveStruct> exchangeMove;
                    CardsMoveStruct move1(equip, player, Player::PlaceEquip, CardMoveReason(CardMoveReason::S_REASON_ROB, player->objectName()));
                    exchangeMove.push_back(move1);
                    if (player->getArmor()) {
                        CardsMoveStruct move2(player->getArmor()->getEffectiveId(), NULL, Player::DiscardPile,
                            CardMoveReason(CardMoveReason::S_REASON_CHANGE_EQUIP, player->objectName()));
                        exchangeMove.push_back(move2);
                    }
                    room->moveCardsAtomic(exchangeMove, true);
                }
            }
        }
        return false;
    }
};

YinbingCard::YinbingCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void YinbingCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("yinbing", this);
}

class YinbingViewAsSkill : public ViewAsSkill
{
public:
    YinbingViewAsSkill() : ViewAsSkill("yinbing")
    {
        response_pattern = "@@yinbing";
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return to_select->getTypeId() != Card::TypeBasic;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 0) return NULL;

        Card *acard = new YinbingCard;
        acard->addSubcards(cards);
        acard->setSkillName(objectName());
        return acard;
    }
};

class Yinbing : public TriggerSkill
{
public:
    Yinbing() : TriggerSkill("yinbing")
    {
        events << EventPhaseStart << Damaged;
        view_as_skill = new YinbingViewAsSkill;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart && player->getPhase() == Player::Finish && !player->isNude()) {
            room->askForUseCard(player, "@@yinbing", "@yinbing", -1, Card::MethodNone);
        } else if (triggerEvent == Damaged && !player->getPile("yinbing").isEmpty()) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.card && (damage.card->isKindOf("Slash") || damage.card->isKindOf("Duel"))) {
                room->sendCompulsoryTriggerLog(player, objectName());

                QList<int> ids = player->getPile("yinbing");
                room->fillAG(ids, player);
                int id = room->askForAG(player, ids, false, objectName());
                room->clearAG(player);
                room->throwCard(id, NULL);
            }
        }

        return false;
    }
};

class Juedi : public PhaseChangeSkill
{
public:
    Juedi() : PhaseChangeSkill("juedi")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return PhaseChangeSkill::triggerable(target) && target->getPhase() == Player::Start
            && !target->getPile("yinbing").isEmpty();
    }

    bool onPhaseChange(ServerPlayer *target) const
    {
        Room *room = target->getRoom();
        if (!room->askForSkillInvoke(target, objectName())) return false;
        room->broadcastSkillInvoke(objectName());

        QList<ServerPlayer *> playerlist;
        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
            if (p->getHp() <= target->getHp())
                playerlist << p;
        }
        ServerPlayer *to_give = NULL;
        if (!playerlist.isEmpty())
            to_give = room->askForPlayerChosen(target, playerlist, objectName(), "@juedi", true);
        if (to_give) {
            room->recover(to_give, RecoverStruct(target));
            DummyCard *dummy = new DummyCard(target->getPile("yinbing"));
            room->obtainCard(to_give, dummy);
            delete dummy;
        } else {
            int len = target->getPile("yinbing").length();
            target->clearOnePrivatePile("yinbing");
            if (target->isAlive())
                room->drawCards(target, len, objectName());
        }
        return false;
    }
};

class Gongao : public TriggerSkill
{
public:
    Gongao() : TriggerSkill("gongao")
    {
        events << Death;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());
        room->gainMaxHp(player);
        room->recover(player, RecoverStruct(player));
        return false;
    }
};

class Juyi : public PhaseChangeSkill
{
public:
    Juyi() : PhaseChangeSkill("juyi")
    {
        frequency = Wake;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && PhaseChangeSkill::triggerable(target)
            && target->getPhase() == Player::Start
            && target->getMark("juyi") == 0
            && target->isWounded()
            && target->getMaxHp() > target->aliveCount();
    }

    bool onPhaseChange(ServerPlayer *zhugedan) const
    {
        Room *room = zhugedan->getRoom();

        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(zhugedan, objectName());
        //room->doLightbox("$JuyiAnimate");

        room->doSuperLightbox("zhugedan", "juyi");

        LogMessage log;
        log.type = "#JuyiWake";
        log.from = zhugedan;
        log.arg = QString::number(zhugedan->getMaxHp());
        log.arg2 = QString::number(zhugedan->aliveCount());
        room->sendLog(log);

        room->setPlayerMark(zhugedan, "juyi", 1);
        room->addPlayerMark(zhugedan, "@waked");
        int diff = zhugedan->getHandcardNum() - zhugedan->getMaxHp();
        if (diff < 0)
            room->drawCards(zhugedan, -diff, objectName());
        if (zhugedan->getMark("juyi") == 1)
            room->handleAcquireDetachSkills(zhugedan, "benghuai|weizhong");

        return false;
    }
};

class Weizhong : public TriggerSkill
{
public:
    Weizhong() : TriggerSkill("weizhong")
    {
        events << MaxHpChanged;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        room->broadcastSkillInvoke(objectName());
        room->sendCompulsoryTriggerLog(player, objectName());

        player->drawCards(1, objectName());
        return false;
    }
};

XiemuCard::XiemuCard()
{
    target_fixed = true;
}

void XiemuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QString kingdom = room->askForKingdom(source, "xiemu");
    room->setPlayerMark(source, "@xiemu_" + kingdom, 1);
}

class XiemuViewAsSkill : public OneCardViewAsSkill
{
public:
    XiemuViewAsSkill() : OneCardViewAsSkill("xiemu")
    {
        filter_pattern = "Slash";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->canDiscard(player, "he") && !player->hasUsed("XiemuCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        XiemuCard *card = new XiemuCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Xiemu : public TriggerSkill
{
public:
    Xiemu() : TriggerSkill("xiemu")
    {
        events << TargetConfirmed << EventPhaseStart;
        view_as_skill = new XiemuViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == TargetConfirmed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from && player != use.from && use.card->getTypeId() != Card::TypeSkill
                && use.card->isBlack() && use.to.contains(player)
                && player->getMark("@xiemu_" + use.from->getKingdom()) > 0) {
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);

                room->notifySkillInvoked(player, objectName());
                player->drawCards(2, objectName());
            }
        } else {
            if (player->getPhase() == Player::RoundStart) {
                foreach (QString kingdom, Sanguosha->getKingdoms()) {
                    QString markname = "@xiemu_" + kingdom;
                    if (player->getMark(markname) > 0)
                        room->setPlayerMark(player, markname, 0);
                }
            }
        }
        return false;
    }
};

class Naman : public TriggerSkill
{
public:
    Naman() : TriggerSkill("naman")
    {
        events << BeforeCardsMove;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile) return false;
        const Card *to_obtain = NULL;
        if ((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_RESPONSE) {
            if (move.from && player->objectName() == move.from->objectName())
                return false;
            to_obtain = move.reason.m_extraData.value<const Card *>();
            if (!to_obtain || !to_obtain->isKindOf("Slash"))
                return false;
        } else {
            return false;
        }
        if (to_obtain && room->askForSkillInvoke(player, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());
            room->obtainCard(player, to_obtain);
            move.removeCardIds(move.card_ids);
            data = QVariant::fromValue(move);
        }

        return false;
    }
};

ShefuCard::ShefuCard()
{
    will_throw = false;
    target_fixed = true;
}

void ShefuCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QString mark = "Shefu_" + user_string;
    source->setMark(mark, getEffectiveId() + 1);

    JsonArray arg;
    arg << source->objectName() << mark << (getEffectiveId() + 1);
    room->doNotify(source, QSanProtocol::S_COMMAND_SET_MARK, arg);

    source->addToPile("ambush", this, false);

    LogMessage log;
    log.type = "$ShefuRecord";
    log.from = source;
    log.card_str = QString::number(getEffectiveId());
    log.arg = user_string;
    room->sendLog(log, source);
}

ShefuDialog *ShefuDialog::getInstance(const QString &object)
{
    static ShefuDialog *instance;
    if (instance == NULL || instance->objectName() != object)
        instance = new ShefuDialog(object);

    return instance;
}

ShefuDialog::ShefuDialog(const QString &object)
    : GuhuoDialog(object, true, true, false, true, true)
{
}

bool ShefuDialog::isButtonEnabled(const QString &button_name) const
{
    return Self->getMark("Shefu_" + button_name) == 0;
}

class ShefuViewAsSkill : public OneCardViewAsSkill
{
public:
    ShefuViewAsSkill() : OneCardViewAsSkill("shefu")
    {
        filter_pattern = ".|.|.|hand";
        response_pattern = "@@shefu";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        const Card *c = Self->tag.value("shefu").value<const Card *>();
        if (c) {
            QString user_string = c->objectName();
            if (Self->getMark("Shefu_" + user_string) > 0)
                return NULL;

            ShefuCard *card = new ShefuCard;
            card->setUserString(user_string);
            card->addSubcard(originalCard);
            return card;
        } else
            return NULL;
    }
};

class Shefu : public PhaseChangeSkill
{
public:
    Shefu() : PhaseChangeSkill("shefu")
    {
        view_as_skill = new ShefuViewAsSkill;
    }

    bool onPhaseChange(ServerPlayer *target) const
    {
        Room *room = target->getRoom();
        if (target->getPhase() != Player::Finish || target->isKongcheng())
            return false;
        room->askForUseCard(target, "@@shefu", "@shefu-prompt", -1, Card::MethodNone);
        return false;
    }

    QDialog *getDialog() const
    {
        return ShefuDialog::getInstance("shefu");
    }
};

class ShefuCancel : public TriggerSkill
{
public:
    ShefuCancel() : TriggerSkill("#shefu-cancel")
    {
        events << CardUsed << JinkEffect << NullificationEffect;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == JinkEffect) {
            bool invoked = false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (ShefuTriggerable(p, player)) {
                    room->setTag("ShefuData", data);
                    if (!room->askForSkillInvoke(p, "shefu_cancel", "data:::jink") || p->getMark("Shefu_jink") == 0)
                        continue;

                    room->broadcastSkillInvoke("shefu", 2);

                    invoked = true;

                    LogMessage log;
                    log.type = "#ShefuEffect";
                    log.from = p;
                    log.to << player;
                    log.arg = "jink";
                    log.arg2 = "shefu";
                    room->sendLog(log);

                    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), "shefu", QString());
                    int id = p->getMark("Shefu_jink") - 1;
                    room->setPlayerMark(p, "Shefu_jink", 0);
                    room->throwCard(Sanguosha->getCard(id), reason, NULL);
                }
            }
            return invoked;
        } else if (triggerEvent == NullificationEffect) {
            bool invoked = false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (ShefuTriggerable(p, player)) {
                    room->setTag("ShefuData", data);
                    if (!room->askForSkillInvoke(p, "shefu_cancel", "data:::nullification") || p->getMark("Shefu_nullification") == 0)
                        continue;

                    room->broadcastSkillInvoke("shefu", 2);

                    invoked = true;

                    LogMessage log;
                    log.type = "#ShefuEffect";
                    log.from = p;
                    log.to << player;
                    log.arg = "nullification";
                    log.arg2 = "shefu";
                    room->sendLog(log);

                    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), "shefu", QString());
                    int id = p->getMark("Shefu_nullification") - 1;
                    room->setPlayerMark(p, "Shefu_nullification", 0);
                    room->throwCard(Sanguosha->getCard(id), reason, NULL);
                }
            }
            return invoked;
        } else if (triggerEvent == CardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getTypeId() != Card::TypeBasic && use.card->getTypeId() != Card::TypeTrick)
                return false;
            if (use.card->isKindOf("Nullification"))
                return false;
            QString card_name = use.card->objectName();
            if (card_name.contains("slash")) card_name = "slash";
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (ShefuTriggerable(p, player)) {
                    room->setTag("ShefuData", data);
                    if (!room->askForSkillInvoke(p, "shefu_cancel", "data:::" + card_name) || p->getMark("Shefu_" + card_name) == 0)
                        continue;

                    room->broadcastSkillInvoke("shefu", 2);

                    LogMessage log;
                    log.type = "#ShefuEffect";
                    log.from = p;
                    log.to << player;
                    log.arg = card_name;
                    log.arg2 = "shefu";
                    room->sendLog(log);

                    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), "shefu", QString());
                    int id = p->getMark("Shefu_" + card_name) - 1;
                    room->setPlayerMark(p, "Shefu_" + card_name, 0);
                    room->throwCard(Sanguosha->getCard(id), reason, NULL);

                    use.nullified_list << "_ALL_TARGETS";
                }
            }
            data = QVariant::fromValue(use);
        }
        return false;
    }

    int getEffectIndex(const ServerPlayer *, const Card *) const
    {
        return 1;
    }

private:
    bool ShefuTriggerable(ServerPlayer *chengyu, ServerPlayer *user) const
    {
        return chengyu->getPhase() == Player::NotActive && chengyu != user
            && chengyu->hasSkill("shefu") && !chengyu->getPile("ambush").isEmpty();
    }
};

class BenyuViewAsSkill : public ViewAsSkill
{
public:
    BenyuViewAsSkill() : ViewAsSkill("benyu")
    {
        response_pattern = "@@benyu";
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return !to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() < Self->getMark("benyu"))
            return NULL;

        DummyCard *card = new DummyCard;
        card->addSubcards(cards);
        return card;
    }
};

class Benyu : public MasochismSkill
{
public:
    Benyu() : MasochismSkill("benyu")
    {
        view_as_skill = new BenyuViewAsSkill;
    }

    void onDamaged(ServerPlayer *target, const DamageStruct &damage) const
    {
        if (!damage.from || damage.from->isDead())
            return;
        Room *room = target->getRoom();
        int from_handcard_num = damage.from->getHandcardNum(), handcard_num = target->getHandcardNum();
        QVariant data = QVariant::fromValue(damage);
        if (handcard_num == from_handcard_num) {
            return;
        } else if (handcard_num < from_handcard_num && handcard_num < 5 && room->askForSkillInvoke(target, objectName(), data)) {
            room->broadcastSkillInvoke(objectName(), 1);
            room->drawCards(target, qMin(5, from_handcard_num) - handcard_num, objectName());
        } else if (handcard_num > from_handcard_num) {
            room->setPlayerMark(target, objectName(), from_handcard_num + 1);
            //if (room->askForUseCard(target, "@@benyu", QString("@benyu-discard::%1:%2").arg(damage.from->objectName()).arg(from_handcard_num + 1), -1, Card::MethodDiscard)) 
            if (room->askForCard(target, "@@benyu", QString("@benyu-discard::%1:%2").arg(damage.from->objectName()).arg(from_handcard_num + 1), QVariant(), objectName())) {
                room->broadcastSkillInvoke(objectName(), 2);
                room->damage(DamageStruct(objectName(), target, damage.from));
            }
        }
        return;
    }
};

class FuluVS : public OneCardViewAsSkill
{
public:
    FuluVS() : OneCardViewAsSkill("fulu")
    {
        filter_pattern = "%slash";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player);
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "slash";
    }

    const Card *viewAs(const Card *originalCard) const
    {
        ThunderSlash *acard = new ThunderSlash(originalCard->getSuit(), originalCard->getNumber());
        acard->addSubcard(originalCard->getId());
        acard->setSkillName(objectName());
        return acard;
    }
};

class Fulu : public TriggerSkill
{
public:
    Fulu() : TriggerSkill("fulu")
    {
        events << ChangeSlash;
        view_as_skill = new FuluVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->objectName() != "slash") return false;
        bool has_changed = false;
        QString skill_name = use.card->getSkillName();
        if (!skill_name.isEmpty()) {
            const Skill *skill = Sanguosha->getSkill(skill_name);
            if (skill && !skill->inherits("FilterSkill") && !skill->objectName().contains("guhuo"))
                has_changed = true;
        }
        if (!has_changed || (use.card->isVirtualCard() && use.card->subcardsLength() == 0)) {
            ThunderSlash *thunder_slash = new ThunderSlash(use.card->getSuit(), use.card->getNumber());
            if (!use.card->isVirtualCard() || use.card->subcardsLength() > 0)
                thunder_slash->addSubcard(use.card);
            thunder_slash->setSkillName("fulu");
            bool can_use = true;
            foreach (ServerPlayer *p, use.to) {
                if (!player->canSlash(p, thunder_slash, false)) {
                    can_use = false;
                    break;
                }
            }
            if (can_use && room->askForSkillInvoke(player, "fulu", data, false)) {
                //room->broadcastSkillInvoke("fulu");
                use.card = thunder_slash;
                data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

class Zhuji : public TriggerSkill
{
public:
    Zhuji() : TriggerSkill("zhuji")
    {
        events << DamageCaused << FinishJudge;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature != DamageStruct::Thunder || !damage.from)
                return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (TriggerSkill::triggerable(p) && room->askForSkillInvoke(p, objectName(), data)) {
                    room->broadcastSkillInvoke(objectName());
                    JudgeStruct judge;
                    judge.good = true;
                    judge.play_animation = false;
                    judge.reason = objectName();
                    judge.pattern = ".";
                    judge.who = damage.from;

                    room->judge(judge);
                    if (judge.pattern == "black") {
                        LogMessage log;
                        log.type = "#ZhujiBuff";
                        log.from = p;
                        log.to << damage.to;
                        log.arg = QString::number(damage.damage);
                        log.arg2 = QString::number(++damage.damage);
                        room->sendLog(log);

                        data = QVariant::fromValue(damage);
                    }
                }
            }
        } else if (triggerEvent == FinishJudge) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (judge->reason == objectName()) {
                judge->pattern = (judge->card->isRed() ? "red" : "black");
                if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge && judge->card->isRed())
                    player->obtainCard(judge->card);
            }
        }
        return false;
    }
};


class SpZhenwei : public TriggerSkill
{
public:
    SpZhenwei() : TriggerSkill("spzhenwei")
    {
        events << TargetConfirming << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::NotActive) {
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (!p->getPile("zhenweipile").isEmpty()) {
                        DummyCard *dummy = new DummyCard(p->getPile("zhenweipile"));
                        room->obtainCard(p, dummy);
                        delete dummy;
                    }
                }
            }
            return false;
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card == NULL || use.to.length() != 1 || !(use.card->isKindOf("Slash") || (use.card->getTypeId() == Card::TypeTrick && use.card->isBlack())))
                return false;
            ServerPlayer *wp = room->findPlayerBySkillName(objectName());
            if (wp == NULL || wp->getHp() <= player->getHp())
                return false;
            if (!room->askForCard(wp, "..", QString("@sp_zhenwei:%1").arg(player->objectName()), data, objectName()))
                return false;
            room->broadcastSkillInvoke(objectName());
            QString choice = room->askForChoice(wp, objectName(), "draw+null", data);
            if (choice == "draw") {
                room->drawCards(wp, 1, objectName());

                if (use.card->isKindOf("Slash")) {
                    if (!use.from->canSlash(wp, use.card, false))
                        return false;
                }

                if (!use.card->isKindOf("DelayedTrick")) {
                    if (use.from->isProhibited(wp, use.card))
                        return false;

                    if (use.card->isKindOf("Collateral")) {
                        QList<ServerPlayer *> targets;
                        foreach (ServerPlayer *p, room->getOtherPlayers(wp)) {
                            if (wp->canSlash(p))
                                targets << p;
                        }

                        if (targets.isEmpty())
                            return false;

                        use.to.first()->tag.remove("collateralVictim");
                        ServerPlayer *target = room->askForPlayerChosen(use.from, targets, objectName(), QString("@dummy-slash2:%1").arg(wp->objectName()));
                        wp->tag["collateralVictim"] = QVariant::fromValue(target);

                        LogMessage log;
                        log.type = "#CollateralSlash";
                        log.from = use.from;
                        log.to << target;
                        room->sendLog(log);
                        room->doAnimate(1, wp->objectName(), target->objectName());
                    }
                    use.to = QList<ServerPlayer *>();
                    use.to << wp;
                    data = QVariant::fromValue(use);
                } else {
                    if (use.from->isProhibited(wp, use.card) || wp->containsTrick(use.card->objectName()))
                        return false;
                    room->moveCardTo(use.card, wp, Player::PlaceDelayedTrick, true);
                }
            } else {
                room->setCardFlag(use.card, "zhenweinull");
                use.from->addToPile("zhenweipile", use.card);

                use.nullified_list << "_ALL_TARGETS";
                data = QVariant::fromValue(use);
            }
            return false;
        }
        return false;
    }
};

class Junbing : public TriggerSkill
{
public:
    Junbing() : TriggerSkill("junbing")
    {
        events << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *player) const
    {
        return player && player->isAlive() && player->getPhase() == Player::Finish && player->getHandcardNum() <= 1;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        ServerPlayer *simalang = room->findPlayerBySkillName(objectName());
        if (!simalang || !simalang->isAlive())
            return false;
        if (player->askForSkillInvoke(this, QString("junbing_invoke:%1").arg(simalang->objectName()))) {
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(simalang, objectName());
            player->drawCards(1);
            if (player->objectName() != simalang->objectName()) {
                DummyCard *cards = player->wholeHandCards();
                CardMoveReason reason = CardMoveReason(CardMoveReason::S_REASON_GIVE, player->objectName());
                room->moveCardTo(cards, simalang, Player::PlaceHand, reason);

                int x = qMin(cards->subcardsLength(), simalang->getHandcardNum());

                if (x > 0) {
                    const Card *return_cards = room->askForExchange(simalang, objectName(), x, x, false, QString("@junbing-return:%1::%2").arg(player->objectName()).arg(cards->subcardsLength()));
                    CardMoveReason return_reason = CardMoveReason(CardMoveReason::S_REASON_GIVE, simalang->objectName());
                    room->moveCardTo(return_cards, player, Player::PlaceHand, return_reason);
                    delete return_cards;
                }
                delete cards;
            }
        }
        return false;
    }
};

QujiCard::QujiCard()
{
}

bool QujiCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    if (subcardsLength() <= targets.length())
        return false;
    return to_select->isWounded();
}

bool QujiCard::targetsFeasible(const QList<const Player *> &targets, const Player *) const
{
    if (targets.length() > 0) {
        foreach (const Player *p, targets) {
            if (!p->isWounded())
                return false;
        }
        return true;
    }
    return false;
}

void QujiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    foreach(ServerPlayer *p, targets)
        room->cardEffect(this, source, p);

    foreach (int id, getSubcards()) {
        if (Sanguosha->getCard(id)->isBlack()) {
            room->loseHp(source);
            break;
        }
    }
}

void QujiCard::onEffect(const CardEffectStruct &effect) const
{
    RecoverStruct recover;
    recover.who = effect.from;
    recover.recover = 1;
    effect.to->getRoom()->recover(effect.to, recover);
}

class Quji : public ViewAsSkill
{
public:
    Quji() : ViewAsSkill("quji")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *) const
    {
        return selected.length() < Self->getLostHp();
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->isWounded() && !player->hasUsed("QujiCard");
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == Self->getLostHp()) {
            QujiCard *quji = new QujiCard;
            quji->addSubcards(cards);
            return quji;
        }
        return NULL;
    }
};

class Canshi : public TriggerSkill
{
public:
    Canshi() : TriggerSkill("canshi")
    {
        events << EventPhaseStart << CardUsed << CardResponded;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (TriggerSkill::triggerable(player) && player->getPhase() == Player::Draw) {
                int n = 0;
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (p->isWounded())
                        ++n;
                }

                if (n > 0 && player->askForSkillInvoke(this)) {
                    room->broadcastSkillInvoke(objectName());
                    player->setFlags(objectName());
                    player->drawCards(n, objectName());
                    return true;
                }
            }
        } else {
            if (player->hasFlag(objectName())) {
                const Card *card = NULL;
                if (triggerEvent == CardUsed)
                    card = data.value<CardUseStruct>().card;
                else {
                    CardResponseStruct resp = data.value<CardResponseStruct>();
                    if (resp.m_isUse)
                        card = resp.m_card;
                }
                if (card != NULL && (card->isKindOf("BasicCard") || card->isKindOf("TrickCard"))) {
                    room->sendCompulsoryTriggerLog(player, objectName());
                    if (!room->askForDiscard(player, objectName(), 1, 1, false, true, "@canshi-discard")) {
                        QList<const Card *> cards = player->getCards("he");
                        const Card *c = cards.at(qrand() % cards.length());
                        room->throwCard(c, player);
                    }
                }
            }
        }
        return false;
    }
};

class Chouhai : public TriggerSkill
{
public:
    Chouhai() : TriggerSkill("chouhai")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->isKongcheng()) {
            room->sendCompulsoryTriggerLog(player, objectName(), true);
            room->broadcastSkillInvoke(objectName());

            DamageStruct damage = data.value<DamageStruct>();
            ++damage.damage;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class Guiming : public TriggerSkill // play audio effect only. This skill is coupled in Player::isWounded().
{
public:
    Guiming() : TriggerSkill("guiming$")
    {
        events << EventPhaseStart;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive() && target->hasLordSkill(this) && target->getPhase() == Player::RoundStart;
    }

    int getPriority(TriggerEvent) const
    {
        return 6;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        foreach (const ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getKingdom() == "wu" && p->isWounded() && p->getHp() == p->getMaxHp()) {
                if (player->hasSkill("weidi"))
                    room->broadcastSkillInvoke("weidi");
                else
                    room->broadcastSkillInvoke(objectName());
                return false;
            }
        }

        return false;
    }
};

class Conqueror : public TriggerSkill
{
public:
    Conqueror() : TriggerSkill("conqueror")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card != NULL && use.card->isKindOf("Slash")) {
            int n = 0;
            foreach (ServerPlayer *target, use.to) {
                if (player->askForSkillInvoke(this, QVariant::fromValue(target))) {
                    QString choice = room->askForChoice(player, objectName(), "BasicCard+EquipCard+TrickCard", QVariant::fromValue(target));

                    room->broadcastSkillInvoke(objectName(), 1);

                    const Card *c = room->askForCard(target, choice, QString("@conqueror-exchange:%1::%2").arg(player->objectName()).arg(choice), choice, Card::MethodNone);
                    if (c != NULL) {
                        room->broadcastSkillInvoke(objectName(), 2);
                        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, target->objectName(), player->objectName(), objectName(), QString());
                        room->obtainCard(player, c, reason);
                        use.nullified_list << target->objectName();
                        data = QVariant::fromValue(use);
                    } else {
                        room->broadcastSkillInvoke(objectName(), 3);
                        QVariantList jink_list = player->tag["Jink_" + use.card->toString()].toList();
                        jink_list[n] = 0;
                        player->tag["Jink_" + use.card->toString()] = jink_list;
                        LogMessage log;
                        log.type = "#NoJink";
                        log.from = target;
                        room->sendLog(log);
                    }
                }
                ++n;
            }
        }
        return false;
    }
};

class Fentian : public PhaseChangeSkill
{
public:
    Fentian() : PhaseChangeSkill("fentian")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *hanba) const
    {
        if (hanba->getPhase() != Player::Finish)
            return false;

        if (hanba->getHandcardNum() >= hanba->getHp())
            return false;

        QList<ServerPlayer*> targets;
        Room* room = hanba->getRoom();

        foreach (ServerPlayer *p, room->getOtherPlayers(hanba)) {
            if (hanba->inMyAttackRange(p) && !p->isNude())
                targets << p;
        }

        if (targets.isEmpty())
            return false;

        room->broadcastSkillInvoke(objectName());
        ServerPlayer *target = room->askForPlayerChosen(hanba, targets, objectName(), "@fentian-choose", false, true);
        int id = room->askForCardChosen(hanba, target, "he", objectName());
        hanba->addToPile("burn", id);
        return false;
    }
};

class FentianRange : public AttackRangeSkill
{
public:
    FentianRange() : AttackRangeSkill("#fentian")
    {

    }

    int getExtra(const Player *target, bool) const
    {
        if (target->hasSkill(this))
            return target->getPile("burn").length();

        return 0;
    }
};

class Zhiri : public PhaseChangeSkill
{
public:
    Zhiri() : PhaseChangeSkill("zhiri")
    {
        frequency = Wake;
    }

    bool onPhaseChange(ServerPlayer *hanba) const
    {
        if (hanba->getMark(objectName()) > 0 || hanba->getPhase() != Player::Start)
            return false;

        if (hanba->getPile("burn").length() < 3)
            return false;

        Room *room = hanba->getRoom();
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("hanba", "zhiri");

        room->setPlayerMark(hanba, objectName(), 1);
        if (room->changeMaxHpForAwakenSkill(hanba) && hanba->getMark("zhiri") > 0)
            room->acquireSkill(hanba, "xintan");

        return false;
    }

};

XintanCard::XintanCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool XintanCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void XintanCard::onEffect(const CardEffectStruct &effect) const
{
    ServerPlayer *hanba = effect.from;
    Room *room = hanba->getRoom();

    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, hanba->objectName(), objectName(), QString());
    room->moveCardTo(this, NULL, Player::DiscardPile, reason, true);

    room->loseHp(effect.to);
}

class Xintan : public ViewAsSkill
{
public:
    Xintan() : ViewAsSkill("xintan")
    {
        expand_pile = "burn";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getPile("burn").length() >= 2 && !player->hasUsed("XintanCard");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() < 2)
            return Self->getPile("burn").contains(to_select->getId());

        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.length() == 2) {
            XintanCard *xt = new XintanCard;
            xt->addSubcards(cards);
            return xt;
        }

        return NULL;
    }
};

class Xiefang : public DistanceSkill
{
public:
    Xiefang() : DistanceSkill("xiefang")
    {
    }

    int getCorrect(const Player *from, const Player *) const
    {
        if (from->hasSkill(this)) {
            int x=0;
            QList<const Player *> players = from->getAliveSiblings();
            players << from;
            foreach (const Player *p, players) {
                if (p->isFemale())
                    x++;
            }
            return -x;
        } else
            return 0;
    }
};

class Zhengnan : public TriggerSkill
{
public:
    Zhengnan() : TriggerSkill("zhengnan")
    {
        events << Death;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *guansuo, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *player = death.who;
        if (guansuo == player) return false;
        if (guansuo->isAlive() && room->askForSkillInvoke(guansuo, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());
            QStringList choices;
            choices << "draw";
            if (!guansuo->hasSkill("wusheng", true)) choices << "wusheng";
            if (!guansuo->hasSkill("dangxian", true)) choices << "dangxian";
            if (!guansuo->hasSkill("zhiman"), true) choices << "zhiman";
            if (choices.isEmpty()) return false;
            QString choice = room->askForChoice(guansuo, "zhengnan", choices.join("+"), QVariant());
            if (choice == "draw")
                guansuo->drawCards(3, objectName());
            else {
                if (!guansuo->hasSkill(choice))
                    room->handleAcquireDetachSkills(guansuo, choice);
            }
        }
        return false;
    }
};

class OLZhengnan : public TriggerSkill
{
public:
    OLZhengnan() : TriggerSkill("olzhengnan")
    {
        events << Death;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *guansuo, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        ServerPlayer *player = death.who;
        if (guansuo == player) return false;
        if (guansuo->isAlive() && room->askForSkillInvoke(guansuo, objectName(), data)) {
            room->broadcastSkillInvoke(objectName());
            guansuo->drawCards(3, objectName());
            if (guansuo->isDead()) return false;
            QStringList choices;
            if (!guansuo->hasSkill("wusheng")) choices << "wusheng";
            if (!guansuo->hasSkill("dangxian")) choices << "dangxian";
            if (!guansuo->hasSkill("zhiman")) choices << "zhiman";
            if (choices.isEmpty()) return false;
            QString choice = room->askForChoice(guansuo, "olzhengnan", choices.join("+"), QVariant());
            if (!guansuo->hasSkill(choice))
                room->handleAcquireDetachSkills(guansuo, choice);
        }
        return false;
    }
};

class TenyearZhengnan : public TriggerSkill
{
public:
    TenyearZhengnan() : TriggerSkill("tenyearzhengnan")
    {
        events << Dying;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *guansuo, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        QStringList names = guansuo->property("tenyearzhengnan_names").toStringList();
        if (names.contains(dying.who->objectName()) || guansuo->getLostHp() <= 0) return false;
        if (!guansuo->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());
        names << dying.who->objectName();
        room->setPlayerProperty(guansuo, "tenyearzhengnan_names", names);
        room->recover(guansuo, RecoverStruct(guansuo));
        guansuo->drawCards(1, objectName());
        QStringList choices;
        if (!guansuo->hasSkill("wusheng")) choices << "wusheng";
        if (!guansuo->hasSkill("dangxian")) choices << "dangxian";
        if (!guansuo->hasSkill("zhiman")) choices << "zhiman";
        if (choices.isEmpty())
            guansuo->drawCards(3, objectName());
        else {
            QString choice = room->askForChoice(guansuo, "tenyearzhengnan", choices.join("+"), QVariant());
            if (!guansuo->hasSkill(choice))
                room->handleAcquireDetachSkills(guansuo, choice);
        }
        return false;
    }
};

class MeiyingMark : public TriggerSkill
{
public:
    MeiyingMark() : TriggerSkill("meiyingmark")
    {
        events << MarkChanged;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        MarkStruct mark = data.value<MarkStruct>();
        if (mark.name == "&meiying" && mark.gain < 0)
            room->addPlayerMark(player, "meiying", -mark.gain);
        return false;
    }
};

FanghunCard::FanghunCard(QString this_skill_name, QString mark_name) : this_skill_name(this_skill_name), mark_name(mark_name)
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool FanghunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();

    if (this->subcardsLength() != 0 && (reason == CardUseStruct::CARD_USE_REASON_PLAY || (reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "slash"))) {
        Slash *slash = new Slash(NoSuit, 0);
        slash->setSkillName("_longdan");
        slash->addSubcards(this->getSubcards());
        slash->deleteLater();
        return slash->targetFilter(targets, to_select, Self);
    }
    return false;
}

bool FanghunCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || (reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "slash")) {
        const Card *card = Sanguosha->getCard(this->getSubcards().first());
        Slash *slash = new Slash(card->getSuit(), card->getNumber());
        slash->setSkillName("_longdan");
        slash->addSubcard(card);
        slash->deleteLater();
        QList<const Player *> players;
        foreach (const Player *p, targets) {
            players <<p;
        }
        return slash->targetsFeasible(players, Self);
    } else {
        return targets.length() == 0;
    }
}

const Card *FanghunCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;
    Room *room = player->getRoom();

    CardUseStruct::CardUseReason reason = Sanguosha->currentRoomState()->getCurrentCardUseReason();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (reason == CardUseStruct::CARD_USE_REASON_PLAY || (reason == CardUseStruct::CARD_USE_REASON_RESPONSE_USE && pattern == "slash")) {
        const Card *card = Sanguosha->getCard(this->getSubcards().first());
        Slash *slash = new Slash(card->getSuit(), card->getNumber());
        slash->setSkillName("_longdan");
        slash->addSubcard(card);
        slash->setFlags("JINGYIN");
        for (int i = card_use.to.length() - 1; i >=0 ; i--) {
            if (!player->canSlash(card_use.to.at(i)))
                card_use.to.removeOne(card_use.to.at(i));
        }
        slash->deleteLater();
        if (card_use.to.isEmpty()) return NULL;
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = this_skill_name;
        room->sendLog(log);
        room->notifySkillInvoked(player, this_skill_name);
        room->broadcastSkillInvoke(this_skill_name);
        player->loseMark("&meiying");
        room->setPlayerMark(player, mark_name, this->getSubcards().first() + 1);
        //room->useCard(CardUseStruct(slash, player, card_use.to), player->getPhase() == Player::Play);
       // player->drawCards(1, objectName());
        return slash;
    } else {
        const Card *card = Sanguosha->getCard(this->getSubcards().first());
        Jink *jink = new Jink(card->getSuit(), card->getNumber());
        jink->setSkillName("_longdan");
        jink->addSubcard(card);
        jink->setFlags("JINGYIN");
        jink->deleteLater();
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = this_skill_name;
        room->sendLog(log);
        room->notifySkillInvoked(player, this_skill_name);
        room->broadcastSkillInvoke(this_skill_name);
        player->loseMark("&meiying");
        room->setPlayerMark(player, mark_name, this->getSubcards().first() + 1);
        return jink;
    }
    return NULL;
}

const Card *FanghunCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();

    if (pattern == "jink") {
        const Card *card = Sanguosha->getCard(this->getSubcards().first());
        Jink *jink = new Jink(card->getSuit(), card->getNumber());
        jink->setSkillName("_longdan");
        jink->addSubcard(card);
        jink->setFlags("JINGYIN");
        jink->deleteLater();
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = this_skill_name;
        room->sendLog(log);
        room->notifySkillInvoked(player, this_skill_name);
        room->broadcastSkillInvoke(this_skill_name);
        player->loseMark("&meiying");
        room->setPlayerMark(player, mark_name, this->getSubcards().first() + 1);
        return jink;
    } else {
        const Card *card = Sanguosha->getCard(this->getSubcards().first());
        Slash *slash = new Slash(card->getSuit(), card->getNumber());
        slash->setSkillName("_longdan");
        slash->addSubcard(card);
        slash->setFlags("JINGYIN");
        slash->deleteLater();
        LogMessage log;
        log.type = "#InvokeSkill";
        log.from = player;
        log.arg = this_skill_name;
        room->sendLog(log);
        room->notifySkillInvoked(player, this_skill_name);
        room->broadcastSkillInvoke(this_skill_name);
        player->loseMark("&meiying");
        room->setPlayerMark(player, mark_name, this->getSubcards().first() + 1);
        return slash;
    }
    return NULL;
}

class FanghunViewAsSkill : public OneCardViewAsSkill
{
public:
    FanghunViewAsSkill() : OneCardViewAsSkill("fanghun")
    {
        response_or_use = true;
    }

    bool viewFilter(const Card *to_select) const
    {
        const Card *card = to_select;

        switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
        case CardUseStruct::CARD_USE_REASON_PLAY: {
            return card->isKindOf("Jink");
        }
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern == "slash")
                return card->isKindOf("Jink");
            else if (pattern == "jink")
                return card->isKindOf("Slash");
        }
        default:
            return false;
        }
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && player->getMark("&meiying") > 0;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (pattern == "jink" || pattern == "slash") && player->getMark("&meiying") > 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        FanghunCard *card = new FanghunCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Fanghun : public TriggerSkill
{
public:
    Fanghun() : TriggerSkill("fanghun")
    {
        events << Damage << Damaged << CardResponded << CardFinished;
        view_as_skill = new FanghunViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == Damage ||  triggerEvent == Damaged)
            return TriggerSkill::getPriority(triggerEvent);
        else
            return -2;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage || triggerEvent == Damaged) {
            if (!player->hasSkill(objectName()) || player->isDead()) return false;
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash")) return false;
            if ((triggerEvent == Damage && damage.by_user) || triggerEvent == Damaged) {
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                player->gainMark("&meiying");
            }
        } else if (triggerEvent == CardResponded) {
            const Card *card = data.value<CardResponseStruct>().m_card;
            int id = card->getEffectiveId();
            //if (!card->getSkillName() == "longdan") return false;
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark("meiying_id") - 1;
                if (marks < 0) continue;
                if (marks == id) {
                    room->setPlayerMark(player, "meiying_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        } else if (triggerEvent == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card;
            int id = card->getEffectiveId();
            //if (card->getSkillName() != "longdan") return false;
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark("meiying_id") - 1;
                if (marks < 0) continue;
                if (marks == id) {
                    room->setPlayerMark(player, "meiying_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        }
        return false;
     }
};

class Fuhan : public PhaseChangeSkill
{
public:
    Fuhan() : PhaseChangeSkill("fuhan")
    {
        frequency = Limited;
        limit_mark = "@fuhanMark";
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::RoundStart) return false;
        if (player->getMark("@fuhanMark") <= 0 || player->getMark("&meiying") <= 0) return false;
        QString num = QString::number(player->getMark("meiying") + player->getMark("&meiying"));
        if (!player->askForSkillInvoke("fuhan", QString("fuhan_invoke:%1").arg(num))) return false;
        Room *room = player->getRoom();
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("zhaoxiang", "fuhan");
        room->removePlayerMark(player, "@fuhanMark");
        int meiying = player->getMark("&meiying");
        player->loseAllMarks("&meiying");
        player->drawCards(meiying, objectName());
        QStringList shus;
        foreach (QString name, Sanguosha->getLimitedGeneralNames()) {
            if (Sanguosha->getGeneral(name)->getKingdom() == "shu")
                shus << name;
        }
        QStringList five_shus;
        for (int i = 1; i < 6; i++) {
            if (shus.isEmpty()) break;
            QString name = shus.at((qrand() % shus.length()));
            five_shus << name;
            shus.removeOne(name);
        }
        if (five_shus.isEmpty()) return false;
        QString shu_general = room->askForGeneral(player, five_shus);
        room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "zhaoxiang" && player->getGeneral2Name() == "zhaoxiang"));
        int n = player->getMark("meiying");
        room->setPlayerProperty(player, "maxhp", n);
        int hp = player->getHp();
        bool recover = true;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->getHp() < hp) {
                recover = false;
                break;
            }
        }
        if (recover == false) return false;
        room->recover(player, RecoverStruct(player));
        return false;
    }
};

OLFanghunCard::OLFanghunCard() : FanghunCard("olfanghun", "olmeiying_id")
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

class OLFanghunViewAsSkill : public OneCardViewAsSkill
{
public:
    OLFanghunViewAsSkill() : OneCardViewAsSkill("olfanghun")
    {
        response_or_use = true;
    }

    bool viewFilter(const Card *to_select) const
    {
        const Card *card = to_select;

        switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
        case CardUseStruct::CARD_USE_REASON_PLAY: {
            return card->isKindOf("Jink");
        }
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern == "slash")
                return card->isKindOf("Jink");
            else if (pattern == "jink")
                return card->isKindOf("Slash");
        }
        default:
            return false;
        }
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && player->getMark("&meiying") > 0;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (pattern == "jink" || pattern == "slash") && player->getMark("&meiying") > 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        OLFanghunCard *card = new OLFanghunCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class OLFanghun : public TriggerSkill
{
public:
    OLFanghun() : TriggerSkill("olfanghun")
    {
        events << Damage << Damaged << CardResponded << CardFinished;
        view_as_skill = new OLFanghunViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == Damage ||  triggerEvent == Damaged)
            return TriggerSkill::getPriority(triggerEvent);
        else
            return -2;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage || triggerEvent == Damaged) {
            if (!player->hasSkill(objectName()) || player->isDead()) return false;
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash")) return false;
            if ((triggerEvent == Damage && damage.by_user) || triggerEvent == Damaged) {
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                player->gainMark("&meiying", damage.damage);
            }
        } else if (triggerEvent == CardResponded) {
            const Card *card = data.value<CardResponseStruct>().m_card;
            int id = card->getEffectiveId();
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark("olmeiying_id") - 1;
                if (marks < 0) continue;
                if (marks == id) {
                    room->setPlayerMark(player, "olmeiying_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        } else if (triggerEvent == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card;
            int id = card->getEffectiveId();
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark("olmeiying_id") - 1;
                if (marks < 0) continue;
                if (marks == id) {
                    room->setPlayerMark(player, "olmeiying_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        }
        return false;
     }
};

class OLFuhan : public PhaseChangeSkill
{
public:
    OLFuhan() : PhaseChangeSkill("olfuhan")
    {
        frequency = Limited;
        limit_mark = "@olfuhanMark";
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::RoundStart) return false;
        if (player->getMark("@olfuhanMark") <= 0 || player->getMark("&meiying") <= 0) return false;
        int nn = player->getMark("meiying") + player->getMark("&meiying");
        nn = qMin(8, nn);
        nn = qMax(2, nn);
        QString num = QString::number(nn);
        if (!player->askForSkillInvoke("olfuhan", QString("olfuhan_invoke:%1").arg(num))) return false;
        Room *room = player->getRoom();
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("ol_zhaoxiang", "olfuhan");
        room->removePlayerMark(player, "@olfuhanMark");
        player->loseAllMarks("&meiying");
        QStringList shus;
        foreach (QString name, Sanguosha->getLimitedGeneralNames()) {
            if (Sanguosha->getGeneral(name)->getKingdom() == "shu")
                shus << name;
        }
        QStringList five_shus;
        for (int i = 1; i < 6; i++) {
            if (shus.isEmpty()) break;
            QString name = shus.at((qrand() % shus.length()));
            five_shus << name;
            shus.removeOne(name);
        }
        if (five_shus.isEmpty()) return false;
        QString shu_general = room->askForGeneral(player, five_shus);
        room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "ol_zhaoxiang" && player->getGeneral2Name() == "ol_zhaoxiang"));
        int n = player->getMark("meiying");
        n = qMin(8, n);
        n = qMax(2, n);
        room->setPlayerProperty(player, "maxhp", n);
        room->recover(player, RecoverStruct(player));
        return false;
    }
};

MobileFanghunCard::MobileFanghunCard() : FanghunCard("mobilefanghun", "mobilemeiying_id")
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

class MobileFanghunViewAsSkill : public OneCardViewAsSkill
{
public:
    MobileFanghunViewAsSkill() : OneCardViewAsSkill("mobilefanghun")
    {
        response_or_use = true;
    }

    bool viewFilter(const Card *to_select) const
    {
        const Card *card = to_select;

        switch (Sanguosha->currentRoomState()->getCurrentCardUseReason()) {
        case CardUseStruct::CARD_USE_REASON_PLAY: {
            return card->isKindOf("Jink");
        }
        case CardUseStruct::CARD_USE_REASON_RESPONSE:
        case CardUseStruct::CARD_USE_REASON_RESPONSE_USE: {
            QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
            if (pattern == "slash")
                return card->isKindOf("Jink");
            else if (pattern == "jink")
                return card->isKindOf("Slash");
        }
        default:
            return false;
        }
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && player->getMark("&meiying") > 0;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return (pattern == "jink" || pattern == "slash") && player->getMark("&meiying") > 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        MobileFanghunCard *card = new MobileFanghunCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class MobileFanghun : public TriggerSkill
{
public:
    MobileFanghun() : TriggerSkill("mobilefanghun")
    {
        events << Damage << CardResponded << CardFinished;
        view_as_skill = new MobileFanghunViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == Damage)
            return TriggerSkill::getPriority(triggerEvent);
        else
            return -2;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damage) {
            if (!player->hasSkill(objectName()) || player->isDead()) return false;
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash")) return false;
            if ((triggerEvent == Damage && damage.by_user) || triggerEvent == Damaged) {
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                player->gainMark("&meiying", 1);
            }
        } else if (triggerEvent == CardResponded) {
            const Card *card = data.value<CardResponseStruct>().m_card;
            int id = card->getEffectiveId();
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark("mobilemeiying_id") - 1;
                if (marks < 0) continue;
                if (marks == id) {
                    room->setPlayerMark(player, "mobilemeiying_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        } else if (triggerEvent == CardFinished) {
            const Card *card = data.value<CardUseStruct>().card;
            int id = card->getEffectiveId();
            foreach(ServerPlayer *p, room->getAllPlayers()) {
                int marks = p->getMark("mobilemeiying_id") - 1;
                if (marks < 0) continue;
                if (marks == id) {
                    room->setPlayerMark(player, "mobilemeiying_id", 0);
                    p->drawCards(1, objectName());
                    break;
                }
            }
        }
        return false;
     }
};

class MobileFuhan : public PhaseChangeSkill
{
public:
    MobileFuhan() : PhaseChangeSkill("mobilefuhan")
    {
        frequency = Limited;
        limit_mark = "@mobilefuhanMark";
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::RoundStart) return false;
        if (player->getMark("@mobilefuhanMark") <= 0) return false;

        Room *room = player->getRoom();
        int nn = player->getMark("meiying") + player->getMark("&meiying");
        if (nn <= 0) return false;
        nn = qMin(room->getPlayers().length(), nn);
        QString num = QString::number(nn);
        if (!player->askForSkillInvoke("mobilefuhan", QString("mobilefuhan_invoke:%1").arg(num))) return false;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("mobile_zhaoxiang", "mobilefuhan");
        room->removePlayerMark(player, "@mobilefuhanMark");
        player->loseAllMarks("&meiying");

        QStringList shus;
        foreach (QString name, Sanguosha->getLimitedGeneralNames()) {
            if (Sanguosha->getGeneral(name)->getKingdom() == "shu" && !hasshu(name, room))
                shus << name;
        }
        QStringList five_shus;
        for (int i = 1; i < 6; i++) {
            if (shus.isEmpty()) break;
            QString name = shus.at((qrand() % shus.length()));
            five_shus << name;
            shus.removeOne(name);
        }
        if (five_shus.isEmpty()) return false;
        QString shu_general = room->askForGeneral(player, five_shus);
        room->changeHero(player, shu_general, false, false, (player->getGeneralName() != "mobile_zhaoxiang" && player->getGeneral2Name() == "mobile_zhaoxiang"));
        int n = player->getMark("meiying");
        n = qMin(room->getPlayers().length(), n);
        room->setPlayerProperty(player, "maxhp", n);
        if (!player->isLowestHpPlayer()) return false;
        room->recover(player, RecoverStruct(player));
        return false;
    }

    bool hasshu(const QString name, Room *room) const
    {
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getGeneralName() == name || p->getGeneral2Name() == name)
                return true;
        }
        return false;
    }
};

class Wuniang : public TriggerSkill
{
public:
    Wuniang() : TriggerSkill("wuniang")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card;
        if (triggerEvent == CardUsed) card = data.value<CardUseStruct>().card;
        else card = data.value<CardResponseStruct>().m_card;
        if (!card || !card->isKindOf("Slash")) return false;
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isNude())
                players << p;
        }
        if (players.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@wuniang-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        if (target->isNude()) return false;
        int id = room->askForCardChosen(player, target, "he", objectName());
        room->obtainCard(player, id, false);
        if (target->isAlive()) target->drawCards(1, objectName());
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo"))
                p->drawCards(1, objectName());
        }
        return false;
    }
};

class Xushen : public TriggerSkill
{
public:
    Xushen() : TriggerSkill("xushen")
    {
        events << QuitDying;
        frequency = Limited;
        limit_mark = "@xushenMark";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getMark("@xushenMark") <= 0) return false;
        ServerPlayer *saver = player->getSaver();
        if (!saver || !saver->isMale() || saver == player) return false;
        bool guansuo = false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")) {
                guansuo = true;
                break;
            }
        }
        if (guansuo) return false;
        if (!saver->askForSkillInvoke(objectName(), player, false)) return false;
        LogMessage log;
        log.type = "#InvokeOthersSkill";
        log.from = saver;
        log.to << player;
        log.arg = "xushen";
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("baosanniang", "xushen");
        room->removePlayerMark(player, "@xushenMark");
        room->changeHero(saver, "guansuo", false, false);
        room->recover(player, RecoverStruct(player));
        if (!player->hasSkill("zhennan"))
            room->handleAcquireDetachSkills(player, "zhennan");
        return false;
    }
};

class Zhennan : public TriggerSkill
{
public:
    Zhennan() : TriggerSkill("zhennan")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = data.value<CardUseStruct>().card;
        if (!card->isKindOf("SavageAssault")) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@zhennan-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        int n = qrand() % 3 + 1;
        room->damage(DamageStruct(objectName(), player, target, n));
        return false;
    }
};

class Shuyong : public TriggerSkill
{
public:
    Shuyong() : TriggerSkill("shuyong")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card;
        if (triggerEvent == CardUsed)
            card = data.value<CardUseStruct>().card;
        else
            card = data.value<CardResponseStruct>().m_card;
        if (!card || !card->isKindOf("Slash")) return false;
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isAllNude())
                players << p;
        }
        if (players.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@shuyong-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        if (target->isAllNude()) return false;
        int id = room->askForCardChosen(player, target, "hej", objectName());
        room->obtainCard(player, id, false);
        if (target->isAlive())
            target->drawCards(1, objectName());
        return false;
    }
};

MobileXushenCard::MobileXushenCard()
{
    target_fixed = true;
}

void MobileXushenCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->doSuperLightbox("mobile_baosanniang", "mobilexushen");
    room->removePlayerMark(source, "@mobilexushenMark");
    int male = 0;
    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (!p->isMale()) continue;
        male++;
    }
    if (male <= 0) return;
    try {
        room->setPlayerFlag(source, "mobilexushen");
        room->loseHp(source, male);
        if (source->hasFlag("mobilexushen"))
            room->setPlayerFlag(source, "-mobilexushen");
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange) {
            if (source->hasFlag("mobilexushen"))
                room->setPlayerFlag(source, "-mobilexushen");
        }
        throw triggerEvent;
    }
}

class MobileXushenVS : public ZeroCardViewAsSkill
{
public:
    MobileXushenVS() : ZeroCardViewAsSkill("mobilexushen")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (player->getMark("@mobilexushenMark") <= 0) return false;
        QList<const Player *> as = player->getAliveSiblings();
        as << player;
        foreach (const Player *p, as) {
            if (p->isMale())
                return true;
        }
        return false;
    }

    const Card *viewAs() const
    {
        return new MobileXushenCard;
    }
};

class MobileXushen : public TriggerSkill
{
public:
    MobileXushen() : TriggerSkill("mobilexushen")
    {
        events << QuitDying;
        frequency = Limited;
        limit_mark = "@mobilexushenMark";
        view_as_skill = new MobileXushenVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (!player->hasFlag("mobilexushen")) return false;
        room->setPlayerFlag(player, "-mobilexushen");
        ServerPlayer *saver = player->getSaver();
        if (!saver) return false;
        if (!player->askForSkillInvoke(objectName(), QString("mobilexushen:%1").arg(saver->objectName()))) return false;
        QStringList skills;
        skills << "wusheng" << "dangxian";
        room->handleAcquireDetachSkills(saver, skills);
        return false;
    }
};

class MoboleZhennan : public TriggerSkill
{
public:
    MoboleZhennan() : TriggerSkill("mobolezhennan")
    {
        events << TargetSpecified;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard")) return false;
        if (use.to.length() <= 1) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->isDead()) return false;
            if (use.to.length() <= player->getHp()) return false;
            if (p->isDead() || !p->hasSkill(this) || !use.to.contains(p)) continue;
            if (!p->canDiscard(p, "he")) continue;
            if (!room->askForCard(p, "..", "@mobolezhennan-discard:" + player->objectName(), data, objectName())) continue;
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), p, player));
        }
        return false;
    }
};

class TenyearXushen : public TriggerSkill
{
public:
    TenyearXushen() : TriggerSkill("tenyearxushen")
    {
        events << QuitDying;
        frequency = Limited;
        limit_mark = "@tenyearxushenMark";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getMark("@tenyearxushenMark") <= 0) return false;
        ServerPlayer *saver = player->getSaver();
        if (!saver || saver == player) return false;
        bool guansuo = false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")) {
                guansuo = true;
                break;
            }
        }
        if (guansuo) return false;

        if (!player->askForSkillInvoke(objectName(), QVariant::fromValue(saver))) return false;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("tenyear_baosanniang", "tenyearxushen");
        room->removePlayerMark(player, "@tenyearxushenMark");
        if (saver->askForSkillInvoke("tenyearxushenChange", "guansuo"))
            room->changeHero(saver, "tenyear_guansuo", false, false);
        saver->drawCards(3, objectName());
        room->recover(player, RecoverStruct(player));
        if (!player->hasSkill("tenyearzhennan"))
            room->handleAcquireDetachSkills(player, "tenyearzhennan");
        return false;
    }
};

class TenyearZhennan : public TriggerSkill
{
public:
    TenyearZhennan() : TriggerSkill("tenyearzhennan")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("TrickCard") || !use.to.contains(player)) return false;
        if (use.to.length() <= 1) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@tenyearzhennan-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        room->damage(DamageStruct(objectName(), player, target));
        return false;
    }
};

class SecondWuniang : public TriggerSkill
{
public:
    SecondWuniang() : TriggerSkill("secondwuniang")
    {
        events << CardUsed << CardResponded;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card;
        if (triggerEvent == CardUsed) card = data.value<CardUseStruct>().card;
        else card = data.value<CardResponseStruct>().m_card;
        if (!card || !card->isKindOf("Slash")) return false;
        QList<ServerPlayer *> players;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (!p->isNude())
                players << p;
        }
        if (players.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@wuniang-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        if (target->isNude()) return false;
        int id = room->askForCardChosen(player, target, "he", objectName());
        room->obtainCard(player, id, false);
        if (target->isAlive()) target->drawCards(1, objectName());
        if (!player->tag["secondxushen_used"].toBool()) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo"))
                p->drawCards(1, objectName());
        }
        return false;
    }
};

class SecondXushen : public TriggerSkill
{
public:
    SecondXushen() : TriggerSkill("secondxushen")
    {
        events << Dying;
        frequency = Limited;
        limit_mark = "@secondxushenMark";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (player != dying.who || player->getMark("@secondxushenMark") <= 0) return false;
        if (!player->askForSkillInvoke(this)) return false;
        player->tag["secondxushen_used"] = true;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("second_tenyear_baosanniang", "secondxushen");
        room->removePlayerMark(player, "@secondxushenMark");
        room->recover(player, RecoverStruct(player));
        if (player->isDead()) return false;
        room->acquireSkill(player, "secondzhennan");
        if (player->isDead()) return false;
        bool guansuo = false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->getGeneralName().contains("guansuo") || p->getGeneral2Name().contains("guansuo")) {
                guansuo = true;
                break;
            }
        }
        if (guansuo) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@secondxushen-invoke", true);
        if (!target) return false;
        room->doAnimate(1, player->objectName(), target->objectName());
        if (target->askForSkillInvoke("tenyearxushenChange", "guansuo"))
            room->changeHero(target, "tenyear_guansuo", false, false);
        target->drawCards(3, objectName());
        return false;
    }
};

class SecondZhennan : public TriggerSkill
{
public:
    SecondZhennan() : TriggerSkill("secondzhennan")
    {
        events << TargetSpecified;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isNDTrick()) return false;
        if (use.to.length() <= 1) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(this)) continue;
            ServerPlayer *target = room->askForPlayerChosen(p, room->getOtherPlayers(p), objectName(), "@tenyearzhennan-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            room->damage(DamageStruct(objectName(), p, target));
        }
        return false;
    }
};

class Shajue : public TriggerSkill
{
public:
    Shajue() : TriggerSkill("shajue")
    {
        events << Dying;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        if (dying.who == player || dying.who->getHp() >= 0) return false;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->gainMark("&brutal");
        if (!dying.damage || !dying.damage->card) return false;
        if (dying.damage->card->isKindOf("SkillCard") || !room->CardInTable(dying.damage->card)) return false;
        room->obtainCard(player, dying.damage->card);
        return false;
    }
};

XionghuoCard::XionghuoCard()
{
}

bool XionghuoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getMark("&brutal") <= 0 && to_select != Self;
}

void XionghuoCard::onEffect(const CardEffectStruct &effect) const
{
    if (effect.from->getMark("&brutal") < 0) return;
    effect.from->loseMark("&brutal");
    effect.to->gainMark("&brutal");
}

class XionghuoViewAsSkill : public ZeroCardViewAsSkill
{
public:
    XionghuoViewAsSkill() : ZeroCardViewAsSkill("xionghuo")
    {
    }

    const Card *viewAs() const
    {
        return new XionghuoCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("&brutal") > 0;
    }
};

class Xionghuo : public TriggerSkill
{
public:
    Xionghuo() : TriggerSkill("xionghuo")
    {
        events << DamageCaused << EventPhaseStart;
        view_as_skill = new XionghuoViewAsSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->getPhase() != Player::Play) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->hasSkill(objectName()) && player->isAlive() && player->getMark("&brutal") > 0) {
                    room->sendCompulsoryTriggerLog(p, "xionghuo", true, true);
                    player->loseAllMarks("&brutal");
                    int i = qrand() % 3;
                    LogMessage log;
                    log.type = "#XionghuoEffect";
                    log.from = player;
                    log.arg = "xionghuo";
                    if (i == 0) {
                        log.arg2 = "xionghuo_choice0";
                        room->sendLog(log);
                        room->damage(DamageStruct(objectName(), p, player, 1, DamageStruct::Fire));
                        if (player->isAlive()) {
                            room->setPlayerMark(player, "xionghuo_from-Clear", 1);
                            room->setPlayerMark(p, "xionghuo_to-Clear", 1);
                        }
                    } else if (i == 1) {
                        log.arg2 = "xionghuo_choice1";
                        room->sendLog(log);
                        room->loseHp(player);
                        if (player->isAlive())
                            room->addMaxCards(player, -1);
                    } else {
                        log.arg2 = "xionghuo_choice2";
                        room->sendLog(log);
                        DummyCard *dummy = new DummyCard;
                        if (player->hasEquip()) {
                            int i = qrand() % player->getEquips().length();
                            dummy->addSubcard(player->getEquips().at(i));
                        }
                        if (!player->isKongcheng())
                            dummy->addSubcard(player->getRandomHandCardId());
                        if (dummy->subcardsLength() > 0)
                            room->obtainCard(p, dummy, false);
                        delete dummy;
                    }
                }
            }
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.from->hasSkill(objectName()) || damage.from == damage.to || damage.to->getMark("&brutal") <= 0) return false;
            LogMessage log;
            log.type = "#XionghuoDamage";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = "xionghuo";
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class XionghuoMark : public GameStartSkill
{
public:
    XionghuoMark() : GameStartSkill("#xionghuomark")
    {
    }

    void onGameStart(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        room->sendCompulsoryTriggerLog(player, "xionghuo", true, true);
        player->gainMark("&brutal", 3);
    }
};

class XionghuoPro : public ProhibitSkill
{
public:
    XionghuoPro() : ProhibitSkill("#xionghuopro")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return card->isKindOf("Slash") && from->getMark("xionghuo_from-Clear") > 0 && to->getMark("xionghuo_to-Clear") > 0;
    }
};

class Lingren : public TriggerSkill
{
public:
    Lingren() : TriggerSkill("lingren")
    {
        events << TargetSpecified;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getMark("lingren-PlayClear") > 0) return false;
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash") || (use.card->isKindOf("TrickCard") && use.card->isDamageCard())) {
            QList<ServerPlayer *> targets = use.to;
            if (targets.contains(player))
                targets.removeOne(player);
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@lingren-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(player, "lingren-PlayClear");
            bool hasbasiccard = false;
            bool hastrickcard = false;
            bool hasequipcard = false;
            foreach (const Card *c, target->getCards("h")) {
                if (c->isKindOf("BasicCard"))
                    hasbasiccard = true;
                else if (c->isKindOf("TrickCard"))
                    hastrickcard = true;
                else if (c->isKindOf("EquipCard"))
                    hasequipcard = true;
                if (hasbasiccard && hastrickcard && hasequipcard)
                    break;
            }
            LogMessage log;
            log.type = "#LingrenGuess";
            log.from = player;
            log.to << target;

            QString choiceo = room->askForChoice(player, "lingren", "hasbasic+hasnobasic", QVariant::fromValue(target));
            log.arg = "lingren:" + choiceo;
            room->sendLog(log);

            QString choicet = room->askForChoice(player, "lingren", "hastrick+hasnotrick", QVariant::fromValue(target));
            log.arg = "lingren:" + choicet;
            room->sendLog(log);

            QString choiceth = room->askForChoice(player, "lingren", "hasequip+hasnoequip", QVariant::fromValue(target));
            log.arg = "lingren:" + choiceth;
            room->sendLog(log);

            int n = 0;
            if ((choiceo == "hasbasic" && hasbasiccard) || (choiceo == "hasnobasic" && !hasbasiccard))
                n++;
            if ((choicet == "hastrick" && hastrickcard) || (choicet == "hasnotrick" && !hastrickcard))
                n++;
            if ((choiceth == "hasequip" && hasequipcard) || (choiceth == "hasnoequip" && !hasequipcard))
                n++;

            log.type = "#LingrenGuessResult";
            log.arg= QString::number(n);
            room->sendLog(log);

            if (n == 0) return false;
            if (n == 3) {
                room->setPlayerFlag(target, "lingren_damage_to");
                room->setCardFlag(use.card, "lingren_damage_card");
                player->drawCards(2, objectName());
                room->acquireNextTurnSkills(player, objectName(), "jianxiong|xingshang");
            } else if (n == 2) {
                room->setPlayerFlag(target, "lingren_damage_to");
                room->setCardFlag(use.card, "lingren_damage_card");
                player->drawCards(2, objectName());
            } else {
                room->setPlayerFlag(target, "lingren_damage_to");
                room->setCardFlag(use.card, "lingren_damage_card");
            }
        }
        return false;
    }
};

class LingrenEffect : public TriggerSkill
{
public:
    LingrenEffect() : TriggerSkill("#lingreneffect")
    {
        events << DamageCaused;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isAlive() && damage.to->hasFlag("lingren_damage_to") && damage.card->hasFlag("lingren_damage_card")) {
            room->setPlayerFlag(damage.to, "-lingren_damage_to");
            room->setCardFlag(damage.card, "-lingren_damage_card");
            LogMessage log;
            log.type = "#LingrenDamage";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = "lingren";
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

class Fujian : public PhaseChangeSkill
{
public:
    Fujian() : PhaseChangeSkill("fujian")
    {
        frequency = Compulsory;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Finish) return false;
        int n = player->getHandcardNum();
        Room *room = player->getRoom();
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHandcardNum() < n)
                n = p->getHandcardNum();
        }
        if (n < 0) n = 0;
        ServerPlayer *target = room->getOtherPlayers(player).at(qrand() % room->getOtherPlayers(player).length());
        room->notifySkillInvoked(player, objectName());
        room->broadcastSkillInvoke(objectName());
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());;
        LogMessage log;
        log.type = "#FujianWatch";
        log.from = player;
        log.to << target;
        log.arg = "fujian";
        log.arg2 = QString::number(n);
        if (n == 0)
            room->sendLog(log);
        else {
            QList<int> handcards;
            QList<int> list;
            foreach (int id, target->handCards()) {
                handcards << id;
            }
            for (int i = 1; i <= n; i++) {
                if (handcards.isEmpty()) break;
                int id = handcards.at(qrand() % handcards.length());
                handcards.removeOne(id);
                list << id;
            }
            if (list.isEmpty())
                room->sendLog(log);
            else {
                QStringList slist;
                foreach (int id, list) {
                    slist << Sanguosha->getCard(id)->toString();
                }
                foreach(ServerPlayer *p, room->getAllPlayers(true)) {
                    if (p == player) continue;
                    room->sendLog(log, p);
                }
                log.type = "$FujianWatch";
                log.card_str = slist.join("+");
                room->sendLog(log, player);
                room->fillAG(list, player);
                room->askForAG(player, list, true, objectName());
                room->clearAG(player);
            }
        }
        return false;
    }
};

ShanjiaCard::ShanjiaCard()
{
    mute = true;
}

bool ShanjiaCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_shanjia");
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

void ShanjiaCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("_shanjia");
    slash->deleteLater();

    QList<ServerPlayer *> targets;
    foreach (ServerPlayer *p, card_use.to) {
        if (card_use.from->canSlash(p, slash, true))
            targets << p;
    }
    if (targets.isEmpty()) return;

    room->useCard(CardUseStruct(slash, card_use.from, targets), false);
}

class ShanjiaViewAsSkill : public ZeroCardViewAsSkill
{
public:
    ShanjiaViewAsSkill() : ZeroCardViewAsSkill("shanjia")
    {
        response_pattern = "@@shanjia";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        //Slash *slash = new Slash(Card::NoSuit, 0);
        //slash->setSkillName("_shanjia");
        return new ShanjiaCard;
    }
};

class Shanjia : public TriggerSkill
{
public:
    Shanjia() : TriggerSkill("shanjia")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        view_as_skill = new ShanjiaViewAsSkill;
        global = true;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventPhaseStart) {
            if (player->isDead() || player->getPhase() != Player::Play || !player->hasSkill(this)) return false;
            if (!player->askForSkillInvoke(objectName())) return false;
            room->broadcastSkillInvoke(objectName());
            player->drawCards(3, objectName());
            int n = 3 - player->getMark("&shanjia") - player->getMark("shanjiaMark");
            bool flag = true;
            if (n > 0) {
                const Card *card = room->askForExchange(player, objectName(), n, n, true, "shanjia-discard:" + QString::number(n));
                room->throwCard(card, player, NULL);
                foreach(int id, card->getSubcards()) {
                    const Card *c = Sanguosha->getCard(id);
                    if (c->isKindOf("BasicCard") || c->isKindOf("TrickCard")) {
                        flag = false;
                        break;
                    }
                }
            }
            if (flag == true) {
                Slash *slash = new Slash(Card::NoSuit, 0);
                slash->setSkillName("_shanjia");
                bool canslash = false;
                foreach(ServerPlayer *p, room->getAlivePlayers()) {
                    if (player->canSlash(p, slash)) {
                        canslash = true;
                        break;
                    }
                }
                if (canslash == false) return false;
                room->askForUseCard(player, "@@shanjia", "@shanjia");
            }
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.from || move.from->isDead() || move.from != player || !move.from_places.contains(Player::PlaceEquip)) return false;
            int n = 0;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places[i] == Player::PlaceEquip) {
                    n++;
                }
            }
            n = qMin(n, 3 - player->getMark("&shanjia") - player->getMark("shanjiaMark"));
            if (n <= 0) return false;
            if (player->hasSkill(this, true))
                room->addPlayerMark(player, "&shanjia", n);
            else
                room->addPlayerMark(player, "shanjiaMark", n);
        }
        return false;
    }
};

class Falu : public TriggerSkill
{
public:
    Falu() : TriggerSkill("falu")
    {
        events << GameStart << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == GameStart) {
            bool send = false;
            if (player->getMark("@flziwei") <= 0) {
                if (!send) {
                    send = true;
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                }
                player->gainMark("@flziwei");
            }
            if (player->getMark("@flhoutu") <= 0) {
                if (!send) {
                    send = true;
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                }
                player->gainMark("@flhoutu");
            }
            if (player->getMark("@flyuqing") <= 0) {
                if (!send) {
                    send = true;
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                }
                player->gainMark("@flyuqing");
            }
            if (player->getMark("@flgouchen") <= 0) {
                if (!send) {
                    send = true;
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                }
                player->gainMark("@flgouchen");
            }
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from == player && move.to_place == Player::DiscardPile &&
            (move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD &&
                    (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))) {
                bool send = false;
                for (int i = 0; i < move.card_ids.length(); i++) {
                    if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip) {
                        const Card *c = Sanguosha->getCard(move.card_ids.at(i));
                        if (c->getSuit() == Card::Spade) {
                            if (player->getMark("@flziwei") <= 0) {
                                if (!send) {
                                    send = true;
                                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                                }
                                player->gainMark("@flziwei");
                            }
                        } else if (c->getSuit() == Card::Club) {
                            if (player->getMark("@flhoutu") <= 0) {
                                if (!send) {
                                    send = true;
                                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                                }
                                player->gainMark("@flhoutu");
                            }
                        } else if (c->getSuit() == Card::Heart) {
                            if (player->getMark("@flyuqing") <= 0) {
                                if (!send) {
                                    send = true;
                                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                                }
                                player->gainMark("@flyuqing");
                            }
                        } else if (c->getSuit() == Card::Diamond) {
                            if (player->getMark("@flgouchen") <= 0) {
                                if (!send) {
                                    send = true;
                                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                                }
                                player->gainMark("@flgouchen");
                            }
                        }
                    }
                }
            }
        }
        return false;
    }
};

class ZhenyiVS : public OneCardViewAsSkill
{
public:
    ZhenyiVS() : OneCardViewAsSkill("zhenyi")
    {
        filter_pattern = ".|.|.|hand";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern == "peach+analeptic" && !player->hasFlag("Global_PreventPeach") && player->getMark("@flhoutu") > 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Peach *peach = new Peach(originalCard->getSuit(), originalCard->getNumber());
        peach->addSubcard(originalCard->getId());
        peach->setSkillName(objectName());
        return peach;
    }
};

class Zhenyi : public TriggerSkill
{
public:
    Zhenyi() : TriggerSkill("zhenyi")
    {
        events << AskForRetrial << DamageCaused << Damaged << PreCardUsed;
        view_as_skill = new ZhenyiVS;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == PreCardUsed)
            return 5;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DamageCaused) {
            if (player->getMark("@flyuqing") > 0) {
                player->tag["flyuqing"] = data;
                DamageStruct damage = data.value<DamageStruct>();
                bool invoke = player->askForSkillInvoke(this, QString("flyuqing:%1").arg(damage.to->objectName()));
                player->tag.remove("flyuqing");
                if (!invoke) return false;
                room->broadcastSkillInvoke(objectName());
                player->loseMark("@flyuqing");
                JudgeStruct judge;
                judge.pattern = ".|black";
                judge.who = player;
                judge.reason = objectName();
                judge.good = true;
                room->judge(judge);
                if (judge.isGood()) {
                    ++damage.damage;
                    data = QVariant::fromValue(damage);
                }
            }
        } else if (event == AskForRetrial) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (player->getMark("@flziwei") > 0) {
                player->tag["flziwei"] = data;
                bool invoke = player->askForSkillInvoke(this, QString("flziwei:%1").arg(judge->who->objectName()));
                player->tag.remove("flziwei");
                if (!invoke) return false;
                room->broadcastSkillInvoke(objectName());
                player->loseMark("@flziwei");
                QString choice = room->askForChoice(player, objectName(), "spade+heart", data);

                WrappedCard *new_card = Sanguosha->getWrappedCard(judge->card->getId());
                new_card->setSkillName("zhenyi");
                new_card->setNumber(5);
                new_card->setModified(true);
                new_card->deleteLater();

                if (choice == "spade")
                    new_card->setSuit(Card::Spade);
                else
                    new_card->setSuit(Card::Heart);

                LogMessage log;
                log.type = "#ZhenyiRetrial";
                log.from = player;
                log.to << judge->who;
                log.arg2 = QString::number(5);
                log.arg = new_card->getSuitString();
                room->sendLog(log);
                room->broadcastUpdateCard(room->getAllPlayers(true), judge->card->getId(), new_card);
                judge->updateResult();
            }
        } else if (event == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getSkillName() != objectName()) return false;
            player->loseMark("@flhoutu");
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature == DamageStruct::Normal) return false;
            if (player->getMark("@flgouchen") <= 0 || !player->askForSkillInvoke(this, QString("flgouchen"))) return false;
            room->broadcastSkillInvoke(objectName());
            player->loseMark("@flgouchen");

            QList<int> basic, equip, trick;
            foreach (int id, room->getDrawPile()) {
                const Card *c = Sanguosha->getCard(id);
                if (c->isKindOf("BasicCard"))
                    basic << id;
                else if (c->isKindOf("EquipCard"))
                    equip << id;
                else if (c->isKindOf("TrickCard"))
                    trick << id;
            }

            DummyCard *dummy = new DummyCard;
            if (!basic.isEmpty())
                dummy->addSubcard(basic.at(qrand() % basic.length()));
            if (!equip.isEmpty())
                dummy->addSubcard(equip.at(qrand() % equip.length()));
            if (!trick.isEmpty())
                dummy->addSubcard(trick.at(qrand() % trick.length()));

            if (dummy->subcardsLength() > 0)
                room->obtainCard(player, dummy, false);
            delete dummy;
        }
        return false;
    }
};

class Dianhua : public PhaseChangeSkill
{
public:
    Dianhua() : PhaseChangeSkill("dianhua")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Start && player->getPhase() != Player::Finish) return false;
        int x = 0;
        if (player->getMark("@flziwei") > 0)
            x++;
        if (player->getMark("@flhoutu") > 0)
            x++;
        if (player->getMark("@flyuqing") > 0)
            x++;
        if (player->getMark("@flgouchen") > 0)
            x++;
        if (x == 0) return false;
        if (!player->askForSkillInvoke(this)) return false;
        Room *room = player->getRoom();
        room->broadcastSkillInvoke(objectName());
        room->askForGuanxing(player, room->getNCards(x, false), Room::GuanxingUpOnly);
        return false;
    }
};

class TenyearZhenyiVS : public OneCardViewAsSkill
{
public:
    TenyearZhenyiVS() : OneCardViewAsSkill("tenyearzhenyi")
    {
        filter_pattern = ".|.|.|hand";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern == "peach" && !player->hasFlag("Global_PreventPeach") && player->getMark("@flhoutu") > 0 &&
                player->getPhase() == Player::NotActive;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Peach *peach = new Peach(originalCard->getSuit(), originalCard->getNumber());
        peach->addSubcard(originalCard->getId());
        peach->setSkillName(objectName());
        return peach;
    }
};

class TenyearZhenyi : public TriggerSkill
{
public:
    TenyearZhenyi() : TriggerSkill("tenyearzhenyi")
    {
        events << AskForRetrial << DamageCaused << Damaged << PreCardUsed;
        view_as_skill = new TenyearZhenyiVS;
    }

    int getPriority(TriggerEvent triggerEvent) const
    {
        if (triggerEvent == PreCardUsed)
            return 5;
        return TriggerSkill::getPriority(triggerEvent);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DamageCaused) {
            if (player->getMark("@flyuqing") > 0) {
                player->tag["flyuqing_tenyear"] = data;
                DamageStruct damage = data.value<DamageStruct>();
                bool invoke = player->askForSkillInvoke(this, QString("flyuqing:%1").arg(damage.to->objectName()));
                player->tag.remove("flyuqing_tenyear");
                if (!invoke) return false;
                room->broadcastSkillInvoke(objectName());
                player->loseMark("@flyuqing");
                ++damage.damage;
                data = QVariant::fromValue(damage);
            }
        } else if (event == AskForRetrial) {
            JudgeStruct *judge = data.value<JudgeStruct *>();
            if (player->getMark("@flziwei") > 0) {
                player->tag["flziwei_tenyear"] = data;
                bool invoke = player->askForSkillInvoke("tenyearzhenyi", QString("flziwei:%1").arg(judge->who->objectName()));
                player->tag.remove("flziwei_tenyear");
                if (!invoke) return false;
                room->broadcastSkillInvoke(objectName());
                player->loseMark("@flziwei");
                QString choice = room->askForChoice(player, "zhenyi", "spade+heart", data);

                WrappedCard *new_card = Sanguosha->getWrappedCard(judge->card->getId());
                new_card->setSkillName("tenyearzhenyi");
                new_card->setNumber(5);
                new_card->setModified(true);
                new_card->deleteLater();

                if (choice == "spade")
                    new_card->setSuit(Card::Spade);
                else
                    new_card->setSuit(Card::Heart);

                LogMessage log;
                log.type = "#ZhenyiRetrial";
                log.from = player;
                log.to << judge->who;
                log.arg2 = QString::number(5);
                log.arg = new_card->getSuitString();
                room->sendLog(log);
                room->broadcastUpdateCard(room->getAllPlayers(true), judge->card->getId(), new_card);
                judge->updateResult();
            }
        } else if (event == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->getSkillName() != objectName()) return false;
            player->loseMark("@flhoutu");
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature == DamageStruct::Normal) return false;
            if (player->getMark("@flgouchen") <= 0 || !player->askForSkillInvoke("zhenyi", QString("flgouchen"))) return false;
            room->broadcastSkillInvoke(objectName());
            player->loseMark("@flgouchen");

            QList<int> basic, equip, trick;
            foreach (int id, room->getDrawPile()) {
                const Card *c = Sanguosha->getCard(id);
                if (c->isKindOf("BasicCard"))
                    basic << id;
                else if (c->isKindOf("EquipCard"))
                    equip << id;
                else if (c->isKindOf("TrickCard"))
                    trick << id;
            }

            DummyCard *dummy = new DummyCard;
            if (!basic.isEmpty())
                dummy->addSubcard(basic.at(qrand() % basic.length()));
            if (!equip.isEmpty())
                dummy->addSubcard(equip.at(qrand() % equip.length()));
            if (!trick.isEmpty())
                dummy->addSubcard(trick.at(qrand() % trick.length()));

            if (dummy->subcardsLength() > 0)
                room->obtainCard(player, dummy, false);
            delete dummy;
        }
        return false;
    }
};

class Xianfu : public TriggerSkill
{
public:
    Xianfu() : TriggerSkill("xianfu")
    {
        events << Damaged << HpRecover;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == Damaged) {
            int n = data.value<DamageStruct>().damage;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                ServerPlayer *target = p->tag["XianfuTarget"].value<ServerPlayer *>();
                if (target && target->isAlive() && target == player) {
                    room->sendCompulsoryTriggerLog(p, objectName(), true, true, qrand() % 2 + 3);
                    room->damage(DamageStruct(objectName(), NULL, p, n));
                }
            }
        } else {
            int rec = data.value<RecoverStruct>().recover;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                ServerPlayer *target = p->tag["XianfuTarget"].value<ServerPlayer *>();
                if (target && target->isAlive() && p->getLostHp() > 0 && target == player) {
                    room->sendCompulsoryTriggerLog(p, objectName(), true, true, qrand() % 2 + 5);
                    room->recover(p, RecoverStruct(p, NULL, qMin(rec, p->getMaxHp() - p->getHp())));
                }
            }
        }
        return false;
    }
};

class XianfuTarget : public GameStartSkill
{
public:
    XianfuTarget() : GameStartSkill("#xianfu-target")
    {
        frequency = Compulsory;
    }

    void onGameStart(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), "xianfu", "@xianfu-choose", false, true);
        room->broadcastSkillInvoke("xianfu", qrand() % 2 + 1);
        player->tag["XianfuTarget"] = QVariant::fromValue(target);
        room->addPlayerMark(target, "&xianfu");
    }
};

class Chouce : public MasochismSkill
{
public:
    Chouce() : MasochismSkill("chouce")
    {
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
    {
        Room *room = player->getRoom();
        for (int i = 0; i < damage.damage; i++) {
            if (player->isAlive() && room->askForSkillInvoke(player, objectName())) {
                room->broadcastSkillInvoke(objectName());
                JudgeStruct judge;
                judge.pattern = ".";
                judge.play_animation = false;
                judge.reason = objectName();
                judge.who = player;

                room->judge(judge);

                if (judge.card->isBlack()) {
                    QList<ServerPlayer *> targets;
                    foreach (ServerPlayer *p, room->getAlivePlayers()) {
                        if (player->canDiscard(p, "hej"))
                            targets << p;
                    }
                    if (targets.isEmpty()) continue;
                    ServerPlayer *target = room->askForPlayerChosen(player, targets, "chouce", "@chouce-discard");
                    int card_id = room->askForCardChosen(player, target, "hej", objectName(), false, Card::MethodDiscard);
                    room->throwCard(card_id, room->getCardPlace(card_id) == Player::PlaceDelayedTrick ? NULL : target, player);
                } else if (judge.card->isRed()) {
                    ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), "chouce_draw", "@chouce-draw");
                    int n = 1;
                    ServerPlayer *xianfu = player->tag["XianfuTarget"].value<ServerPlayer *>();
                    if (xianfu && xianfu == target)
                        n = 2;
                    target->drawCards(n, objectName());
                }
            } else {
                break;
            }
        }
    }
};

class Qianya : public TriggerSkill
{
public:
    Qianya() : TriggerSkill("qianya")
    {
        events << TargetConfirmed;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.to.contains(player)) return false;
        if (use.card->isKindOf("TrickCard")) {
            if (player->isKongcheng()) return false;
            QList<int> handcards = player->handCards();
            room->askForYiji(player, handcards, objectName(), false, false, true, -1, QList<ServerPlayer *>(),
                             CardMoveReason(), "qianya-give", true);
        }
        return false;
    }
};

class Shuomeng : public TriggerSkill
{
public:
    Shuomeng() : TriggerSkill("shuomeng")
    {
        events << EventPhaseEnd;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Play) return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->canPindian(p))
                targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@shuomeng-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        bool success = player->pindian(target, objectName());
        if (success) {
            ExNihilo *ex_nihilo = new ExNihilo(Card::NoSuit, 0);
            ex_nihilo->setSkillName("_shuomeng");
            if (!player->isLocked(ex_nihilo) && !player->isProhibited(player, ex_nihilo))
                room->useCard(CardUseStruct(ex_nihilo, player, player));
        } else {
            Dismantlement *dismantlement = new Dismantlement(Card::NoSuit, 0);
            dismantlement->setSkillName("_shuomeng");
            if (!target->isLocked(dismantlement) && !target->isProhibited(player, dismantlement) &&
                    dismantlement->targetFilter(QList<const Player *>(), player, target))
                room->useCard(CardUseStruct(dismantlement, target, player));
        }
        return false;
    }
};

class Tuifeng : public TriggerSkill
{
public:
    Tuifeng() : TriggerSkill("tuifeng")
    {
        events << Damaged << EventPhaseStart;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            for (int i = 1; i <= damage.damage; i++) {
                if (player->isDead() || player->isNude()) break;
                const Card *card = room->askForCard(player, "..", "tuifeng-put", data, Card::MethodNone);
                if (!card) break;
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName());
                player->addToPile("tfeng", card);
            }
        } else {
            if (player->getPhase() != Player::Start) return false;
            int n = player->getPile("tfeng").length();
            if (n <= 0) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            player->clearOnePrivatePile("tfeng");
            player->drawCards(2 * n, objectName());
            room->addSlashCishu(player, n);
        }
        return false;
    }
};

class Andong : public TriggerSkill
{
public:
    Andong() : TriggerSkill("andong")
    {
        events << DamageInflicted << EventPhaseProceeding;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DamageInflicted) {
            if (!player->hasSkill(this)) return false;
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.from || damage.from->isDead() || damage.from == player) return false;
            if (!player->askForSkillInvoke(this, QVariant::fromValue(damage.from))) return false;
            room->broadcastSkillInvoke(objectName());
            QStringList choices;
            choices << "prevent" << "get";
            QString choice = room->askForChoice(damage.from, objectName(), choices.join("+"), data);
            LogMessage log;
            log.type = "#FumianFirstChoice";
            log.from = damage.from;
            log.arg = "andong:" + choice;
            room->sendLog(log);
            if (choice == "prevent") {
                room->addPlayerMark(damage.from, "andong_heart-Clear");
                return true;
            } else {
                room->doGongxin(player, damage.from, QList<int>(), objectName());
                QList<int> hearts;
                foreach (int id, damage.from->handCards()) {
                    if (Sanguosha->getCard(id)->getSuit() == Card::Heart) {
                        hearts << id;
                    }
                }
                if (hearts.isEmpty()) return false;
                DummyCard get(hearts);
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                room->obtainCard(player, &get, reason, false);
            }
        } else {
            if (player->getPhase() != Player::Discard) return false;
            if (player->getMark("andong_heart-Clear") <= 0) return false;
            QList<int> hearts;
            foreach (int id, player->handCards()) {
                if (Sanguosha->getCard(id)->getSuit() == Card::Heart)
                    hearts << id;
            }
            room->ignoreCards(player, hearts);
        }
        return false;
    }
};

YingshiCard::YingshiCard()
{
    target_fixed = true;
    will_throw = false;
}

void YingshiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    room->setPlayerProperty(source, "yingshi_name", QString());
    LogMessage log;
    log.type = "$KuangbiGet";
    log.from = source;
    log.arg = "yschou";
    log.card_str = Sanguosha->getCard(getSubcards().first())->toString();
    room->sendLog(log);
    room->obtainCard(source, this, true);
}

class YingshiVS : public OneCardViewAsSkill
{
public:
    YingshiVS() : OneCardViewAsSkill("yingshi")
    {
        expand_pile = "%yschou";
        response_pattern = "@@yingshi";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (selected.length() >= 2)
            return false;
        QString name = Self->property("yingshi_name").toString();
        QList<const Player *> as = Self->getAliveSiblings();
        as << Self;
        foreach (const Player *p, as) {
            if (!p->getPile("yschou").isEmpty() && p->objectName() == name) {
                return p->getPile("yschou").contains(to_select->getId());
            }
        }
        return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        YingshiCard *card = new YingshiCard;
        card->addSubcard(originalCard);
        return card;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }
};

class Yingshi : public TriggerSkill
{
public:
    Yingshi() : TriggerSkill("yingshi")
    {
        events << EventPhaseStart << Damage;
        view_as_skill = new YingshiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play || !player->hasSkill(this)) return false;
            bool has_chou = false;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (!p->getPile("yschou").isEmpty()) {
                    has_chou = true;
                    break;
                }
            }
            if (has_chou) return false;
            QList<int> hearts;
            foreach (const Card *c, player->getCards("he")) {
                if (c->getSuit() == Card::Heart)
                    hearts << c->getEffectiveId();
            }
            if (hearts.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@yingshi-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            target->addToPile("yschou", hearts);
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash")) return false;
            if (damage.to->isDead() || damage.to->getPile("yschou").isEmpty()) return false;
            room->setPlayerProperty(player, "yingshi_name", damage.to->objectName());
            if (!room->askForUseCard(player, "@@yingshi", "@yingshi:" + damage.to->objectName()))
                room->setPlayerProperty(player, "yingshi_name", QString());
        }
        return false;
    }
};

class YingshiDeath : public TriggerSkill
{
public:
    YingshiDeath() : TriggerSkill("#yingshi-death")
    {
        events << Death;
        view_as_skill = new YingshiVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (death.who == player || death.who->getPile("yschou").isEmpty()) return false;
        room->sendCompulsoryTriggerLog(player, "yingshi", true, true);
        DummyCard get(death.who->getPile("yschou"));
        room->obtainCard(player, &get, true);
        return false;
    }
};

class Weilu : public TriggerSkill
{
public:
    Weilu() : TriggerSkill("weilu")
    {
        events << Damaged << EventPhaseStart << EventPhaseChanging;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.from || damage.from == player) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            QStringList names = player->property("weilu_damage_from").toStringList();
            if (!names.contains(damage.from->objectName())) {
                names << damage.from->objectName();
                room->setPlayerProperty(player, "weilu_damage_from", names);
                room->addPlayerMark(damage.from, "&weilu");
            }
        } else if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play) return false;
            QStringList names = player->property("weilu_damage_from").toStringList();
            if (names.isEmpty()) return false;
            bool log = true;
            foreach (QString name, names) {
                ServerPlayer *p = room->findPlayerByObjectName(name);
                if (!p || p->isDead() || p->getHp() <= 1) continue;
                if (log) {
                    log = false;
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                }
                int lose = p->getHp() - 1;
                room->loseHp(p, lose);
                if (p->isAlive())
                    room->addPlayerMark(p, "weilu_losehp-Clear", lose);
            }
        } else {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            QStringList names = player->property("weilu_damage_from").toStringList();
            if (names.isEmpty()) return false;
            room->setPlayerProperty(player, "weilu_damage_from", QStringList());
            bool log = true;
            foreach (QString name, names) {
                ServerPlayer *p = room->findPlayerByObjectName(name);
                if (!p || p->isDead()) continue;
                if (p->getMark("&weilu") > 0)
                    room->removePlayerMark(p, "&weilu");
                int recover = p->getMark("weilu_losehp-Clear");
                room->setPlayerMark(p, "weilu_losehp-Clear", 0);
                recover = qMin(recover, p->getMaxHp() - p->getHp());
                if (recover > 0) {
                    if (log) {
                        log = false;
                        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                    }
                    room->recover(p, RecoverStruct(player, NULL, recover));
                }
            }
        }
        return false;
    }
};

ZengdaoCard::ZengdaoCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void ZengdaoCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->removePlayerMark(effect.from, "@zengdaoMark");
    room->doSuperLightbox("lvqian", "zengdao");
    effect.to->addToPile("zengdao", this);
}

ZengdaoRemoveCard::ZengdaoRemoveCard()
{
    mute = true;
    target_fixed = true;
    will_throw = false;
    m_skillName = "zengdao";
    handling_method = Card::MethodNone;
}

void ZengdaoRemoveCard::onUse(Room *room, const CardUseStruct &) const
{
    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), "zengdao", QString());
    room->throwCard(this, reason, NULL);
}

class ZengdaoVS : public ViewAsSkill
{
public:
    ZengdaoVS() : ViewAsSkill("zengdao")
    {
        expand_pile = "zengdao";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@zengdao!")
            return selected.isEmpty() && Self->getPile("zengdao").contains(to_select->getEffectiveId());
        return to_select->isEquipped();
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;

        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@zengdao!") {
            ZengdaoRemoveCard *card = new ZengdaoRemoveCard;
            card->addSubcards(cards);
            return card;
        }
        ZengdaoCard *card = new ZengdaoCard;
        card->addSubcards(cards);
        return card;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@zengdaoMark") > 0 && !player->getEquips().isEmpty();
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@zengdao!";
    }
};

class Zengdao : public TriggerSkill
{
public:
    Zengdao() : TriggerSkill("zengdao")
    {
        events << DamageCaused;
        view_as_skill = new ZengdaoVS;
        frequency = Limited;
        limit_mark = "@zengdaoMark";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive() && !target->getPile("zengdao").isEmpty();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->isDead()) return false;
        LogMessage log;
        log.type = "#Zengdao";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);

        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), "zengdao", QString());
        if (player->getPile("zengdao").length() == 1) {
            room->throwCard(Sanguosha->getCard(player->getPile("zengdao").first()), reason, NULL);
        } else {
            if (!room->askForUseCard(player, "@@zengdao!", "@zengdao")) {
                int id = player->getPile("zengdao").at(qrand() % player->getPile("zengdao").length());
                room->throwCard(Sanguosha->getCard(id), reason, NULL);
            }
        }
        LogMessage newlog;
        newlog.type = "#ZengdaoDamage";
        newlog.from = player;
        newlog.to << damage.to;
        newlog.arg = QString::number(damage.damage);
        newlog.arg2 = QString::number(++damage.damage);
        room->sendLog(newlog);

        data = QVariant::fromValue(damage);

        return false;
    }
};

GusheCard::GusheCard()
{
}

bool GusheCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.length() < 3 && Self->canPindian(to_select);
}

int GusheCard::pindian(ServerPlayer *from, ServerPlayer *target, const Card *card1, const Card *card2) const
{
    if (card1 == NULL || card2 == NULL) return -2;

    Room *room = from->getRoom();

    PindianStruct *pindian_struct = new PindianStruct;
    pindian_struct->from = from;
    pindian_struct->to = target;
    pindian_struct->from_card = card1;
    pindian_struct->to_card = card2;

    RoomThread *thread = room->getThread();
    QVariant data = QVariant::fromValue(pindian_struct);
    QList<ServerPlayer *> pd;
    pd << from << target;
    room->sortByActionOrder(pd);
    foreach (ServerPlayer *p, pd)
        thread->trigger(AskforPindianCard, room, p, data);

    PindianStruct *new_star = data.value<PindianStruct *>();
    card1 = new_star->from_card;
    card2 = new_star->to_card;

    if (card1 == NULL && card2 == NULL) {
        QList<const Card *> cards = room->askForPindianRace(from, target, "gushe");
        card1 = cards.first();
        card2 = cards.last();
    } else if (card2 == NULL) {
        if (card1->isVirtualCard()) {
            int card_id = card1->getEffectiveId();
            card1 = Sanguosha->getCard(card_id);
        }
        card2 = room->askForPindian(target, from, target, "gushe");
    } else if (card1 == NULL) {
        if (card2->isVirtualCard()) {
            int card_id = card2->getEffectiveId();
            card2 = Sanguosha->getCard(card_id);
        }
        card1 = room->askForPindian(from, from, target, "gushe");
    }

    if (card1 == NULL || card2 == NULL) return -2;

    pindian_struct->from_card = card1;
    pindian_struct->to_card = card2;
    pindian_struct->from_number = card1->getNumber();
    pindian_struct->to_number = card2->getNumber();

    QList<CardsMoveStruct> moves;
    CardsMoveStruct move_table_1;
    move_table_1.card_ids << pindian_struct->from_card->getEffectiveId();
    move_table_1.from = pindian_struct->from;
    move_table_1.to = NULL;
    move_table_1.to_place = Player::PlaceTable;
    move_table_1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
        pindian_struct->to->objectName(), pindian_struct->reason, QString());

    CardsMoveStruct move_table_2;
    move_table_2.card_ids << pindian_struct->to_card->getEffectiveId();
    move_table_2.from = pindian_struct->to;
    move_table_2.to = NULL;
    move_table_2.to_place = Player::PlaceTable;
    move_table_2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName());

    moves.append(move_table_1);
    moves.append(move_table_2);
    room->moveCardsAtomic(moves, true);

    LogMessage log;
    log.type = "$PindianResult";
    log.from = pindian_struct->from;
    log.card_str = QString::number(pindian_struct->from_card->getEffectiveId());
    room->sendLog(log);

    log.type = "$PindianResult";
    log.from = pindian_struct->to;
    log.card_str = QString::number(pindian_struct->to_card->getEffectiveId());
    room->sendLog(log);

    thread->trigger(PindianVerifying, room, from, data);

    pindian_struct->success = pindian_struct->from_number > pindian_struct->to_number;

    log.type = pindian_struct->success ? "#PindianSuccess" : "#PindianFailure";
    log.from = from;
    log.to.clear();
    log.to << target;
    log.card_str.clear();
    room->sendLog(log);

    JsonArray arg;
    arg << QSanProtocol::S_GAME_EVENT_REVEAL_PINDIAN << objectName() << pindian_struct->from_card->getEffectiveId() << target->objectName()
        << pindian_struct->to_card->getEffectiveId() << pindian_struct->success << "gushe";
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

    thread->trigger(Pindian, room, from, data);

    moves.clear();
    if (room->getCardPlace(pindian_struct->from_card->getEffectiveId()) == Player::PlaceTable) {
        CardsMoveStruct move_discard_1;
        move_discard_1.card_ids << pindian_struct->from_card->getEffectiveId();
        move_discard_1.from = pindian_struct->from;
        move_discard_1.to = NULL;
        move_discard_1.to_place = Player::DiscardPile;
        move_discard_1.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->from->objectName(),
            pindian_struct->to->objectName(), pindian_struct->reason, QString());
        moves.append(move_discard_1);
    }

    if (room->getCardPlace(pindian_struct->to_card->getEffectiveId()) == Player::PlaceTable) {
        CardsMoveStruct move_discard_2;
        move_discard_2.card_ids << pindian_struct->to_card->getEffectiveId();
        move_discard_2.from = pindian_struct->to;
        move_discard_2.to = NULL;
        move_discard_2.to_place = Player::DiscardPile;
        move_discard_2.reason = CardMoveReason(CardMoveReason::S_REASON_PINDIAN, pindian_struct->to->objectName());
        moves.append(move_discard_2);
    }
    if (!moves.isEmpty())
        room->moveCardsAtomic(moves, true);

    QVariant decisionData = QVariant::fromValue(QString("pindian:%1:%2:%3:%4:%5")
        .arg("gushe")
        .arg(from->objectName())
        .arg(pindian_struct->from_card->getEffectiveId())
        .arg(target->objectName())
        .arg(pindian_struct->to_card->getEffectiveId()));
    thread->trigger(ChoiceMade, room, from, decisionData);

    if (pindian_struct->success) return 1;
    else if (pindian_struct->from_number == pindian_struct->to_number) return 0;
    else if (pindian_struct->from_number < pindian_struct->to_number) return -1;
    return -2;
}

void GusheCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    if (!source->canPindian()) return;
    LogMessage log;
    log.type = "#Pindian";
    log.from = source;
    log.to = targets;
    room->sendLog(log);

    const Card *cardss = NULL;
    QList<const Card *> pindian_cards;
    QList<ServerPlayer *> pindian_targets;

    foreach (ServerPlayer *p ,targets) { //太麻烦了，还是耦合吧，虽然有二次询问的问题。以后再解耦
        if (!source->canPindian(p)) continue;
        if (p->hasSkill("olhanzhan") && !cardss) {
            if (p->askForSkillInvoke("olhanzhan", QVariant::fromValue(source))) {
                room->broadcastSkillInvoke("olhanzhan");
                cardss = source->getRandomHandCard();
                break;
            }
        } else if (p->hasSkill("secondolhanzhan") && !cardss) {
            if (p->askForSkillInvoke("secondolhanzhan", QVariant::fromValue(source))) {
                room->broadcastSkillInvoke("secondolhanzhan");
                cardss = source->getRandomHandCard();
                break;
            }
        }
    }

    foreach (ServerPlayer *p ,targets) {
        if (!source->canPindian(p)) continue;
        if (!cardss) {
            QList<const Card *> cards = room->askForPindianRace(source, p, "gushe");
            cardss = cards.first();
            const Card *cardtt = cards.last();
            if (cardtt) {
                pindian_targets << p;
                pindian_cards << cardtt;
            }
        } else {
            const Card *cardtt = room->askForPindian(p, source, p, "gushe");
            if (cardtt) {
                pindian_targets << p;
                pindian_cards << cardtt;
            }
        }
    }

    if (!cardss || pindian_targets.isEmpty() || pindian_cards.isEmpty()) return;

    for (int i = 0; i < pindian_targets.length(); i++) {
        int n = pindian(source, pindian_targets.at(i), cardss, pindian_cards.at(i));
        if (n == -2) continue;

        QList<ServerPlayer *>losers;
        if (n == 1)
            losers << pindian_targets.at(i);
        else if (n == 0)
            losers << source << pindian_targets.at(i);
        else if (n == -1)
            losers << source;
        if (losers.isEmpty()) continue;

        room->sortByActionOrder(losers);

        foreach (ServerPlayer *p, losers) {
            if (!p->canDiscard(p, "he"))
                source->drawCards(1, "gushe");
            else {
                p->tag["GusheDiscard"] = QVariant::fromValue(source);
                bool dis = room->askForDiscard(p, "gushe", 1, 1, true, true, "gushe-discard:" + source->objectName());
                p->tag.remove("GusheDiscard");
                if (!dis)
                    source->drawCards(1, "gushe");
            }
        }

        if (losers.contains(source))
            source->gainMark("&raoshe");
    }
}

class GusheVS : public ZeroCardViewAsSkill
{
public:
    GusheVS() : ZeroCardViewAsSkill("gushe")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->usedTimes("GusheCard") < 1 + player->getMark("gushe_extra-Clear") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new GusheCard;
    }
};

class Gushe : public TriggerSkill
{
public:
    Gushe() : TriggerSkill("gushe")
    {
        events << MarkChanged;
        view_as_skill = new GusheVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        MarkStruct mark = data.value<MarkStruct>();
        if (mark.name == "&raoshe" && player->getMark("&raoshe") >= 7)
            room->killPlayer(player);
        return false;
    }
};

class Jici : public TriggerSkill
{
public:
    Jici() : TriggerSkill("jici")
    {
        events << PindianVerifying;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        PindianStruct *pindian = data.value<PindianStruct *>();
        if (pindian->reason != "gushe") return false;

        QList<ServerPlayer *> pindian_players;
        pindian_players << pindian->from << pindian->to;
        foreach (ServerPlayer *p, pindian_players) {
            if (p && p->isAlive() && p->hasSkill(this)) {
                int n = p->getMark("&raoshe");
                int number = (p == pindian->from) ? pindian->from_number : pindian->to_number;
                if (number < n) {
                    if (p->askForSkillInvoke(this, QString("jici_invoke:%1").arg(QString::number(n)))) {
                        room->broadcastSkillInvoke(objectName());
                        int num = 0;
                        if (p == pindian->from) {
                            pindian->from_number = qMin(13, pindian->from_number + n);
                            num = pindian->from_number;
                        } else {
                            pindian->to_number = qMin(13, pindian->to_number + n);
                            num = pindian->to_number;
                        }

                        LogMessage log;
                        log.type = "#JiciUp";
                        log.from = p;
                        log.arg = QString::number(num);
                        room->sendLog(log);

                        data = QVariant::fromValue(pindian);
                    }
                } else if (number == n) {
                    room->notifySkillInvoked(p, objectName());
                    room->broadcastSkillInvoke(objectName());
                    if (p->hasSkill("gushe")) {
                        LogMessage log;
                        log.type = "#Jici";
                        log.from = p;
                        log.arg = objectName();
                        log.arg2 = "gushe";
                        room->sendLog(log);
                    }

                    room->addPlayerMark(p, "gushe_extra-Clear");
                }
            }
        }
        return false;
    }
};

class Qingzhong : public TriggerSkill
{
public:
    Qingzhong() : TriggerSkill("qingzhong")
    {
        events << EventPhaseStart << EventPhaseEnd;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Play) return false;
        if (event == EventPhaseStart) {
            if (!player->hasSkill(this) || !player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(player, "qingzhong-PlayClear");
            player->drawCards(2, objectName());
        } else {
            if (player->getMark("qingzhong-PlayClear") <= 0) return false;
            room->setPlayerMark(player, "qingzhong-PlayClear", 0);
            int n = room->getOtherPlayers(player).first()->getHandcardNum();
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() < n)
                    n = p->getHandcardNum();
            }

            if (n <= 0 && player->getHandcardNum() <= 0) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            QList<ServerPlayer *>targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHandcardNum() == n)
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@qingzhong-invoke");
            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());

            LogMessage log;
            log.type = "#Dimeng";
            log.from = player;
            log.to << target;
            log.arg = QString::number(player->getHandcardNum());
            log.arg2 = QString::number(target->getHandcardNum());
            room->sendLog(log);
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p != player && p != target) {
                    JsonArray arr;
                    arr << player->objectName() << target->objectName();
                    room->doNotify(p, QSanProtocol::S_COMMAND_EXCHANGE_KNOWN_CARDS, arr);
                }
            }
            QList<CardsMoveStruct> exchangeMove;
            CardsMoveStruct move1(player->handCards(), target, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, player->objectName(), target->objectName(), "qingzhong", QString()));
            CardsMoveStruct move2(target->handCards(), player, Player::PlaceHand,
                CardMoveReason(CardMoveReason::S_REASON_SWAP, target->objectName(), player->objectName(), "qingzhong", QString()));
            exchangeMove.push_back(move1);
            exchangeMove.push_back(move2);
            room->moveCardsAtomic(exchangeMove, false);
            room->getThread()->delay();
        }
        return false;
    }
};

class WeijingVS : public ZeroCardViewAsSkill
{
public:
    WeijingVS() : ZeroCardViewAsSkill("weijing")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && player->getMark("weijing_lun") <= 0;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
            return false;
        return (pattern == "jink" || pattern == "slash") && player->getMark("weijing_lun") <= 0;
    }

    const Card *viewAs() const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY){
            Slash *slash = new Slash(Card::NoSuit, -1);
            slash->setSkillName(objectName());
            return slash;
        }

        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE)
            return NULL;
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "jink") {
            Jink *jink = new Jink(Card::NoSuit, 0);
            jink->setSkillName(objectName());
            return jink;
        } else if (pattern == "slash") {
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName(objectName());
            return slash;
        } else
            return NULL;
    }
};

class Weijing : public TriggerSkill
{
public:
    Weijing() : TriggerSkill("weijing")
    {
        events << PreCardUsed << PreCardResponded;
        view_as_skill = new WeijingVS;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = NULL;
        if (event == PreCardUsed) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard") || use.card->getSkillName() != objectName()) return false;
            card = use.card;
        } else {
            CardResponseStruct resp = data.value<CardResponseStruct>();
            if (resp.m_card->isKindOf("SkillCard") || resp.m_card->getSkillName() != objectName()) return false;
            card = resp.m_card;
        }
        if (!card) return false;
        room->addPlayerMark(player, "weijing_lun");
        return false;
    }
};

class Lvli : public TriggerSkill
{
public:
    Lvli() : TriggerSkill("lvli")
    {
        events << Damage << Damaged;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        ServerPlayer *current = room->getCurrent();
        if (!current || current->getPhase() == Player::NotActive) return false;
        int marks = player->getMark("lvli-Clear");
        int choujue = player->getMark("choujue");

        int max = 1;
        if (choujue > 0 && current == player)
            max = 2;
        if (marks >= max) return false;

        if (event == Damaged) {
            if (player->getMark("beishui") <= 0)
                return false;
        }

        if ((player->getHandcardNum() < player->getHp()) || (player->getHandcardNum() > player->getHp() && player->getLostHp() > 0)) {
            if (!player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(player, "lvli-Clear");
            if (player->getHandcardNum() < player->getHp()) {
                int draw = player->getHp() - player->getHandcardNum();
                if (draw <= 0) return false;
                player->drawCards(draw, objectName());
            } else if (player->getHandcardNum() > player->getHp() && player->getLostHp() > 0) {
                int recover = player->getHandcardNum() - player->getHp();
                recover = qMin(recover, player->getMaxHp() - player->getHp());
                if (recover <= 0) return false;
                room->recover(player, RecoverStruct(player, NULL, recover));
            }
        }
        return false;
    }
};

class Choujue : public TriggerSkill
{
public:
    Choujue() : TriggerSkill("choujue")
    {
        events << EventPhaseChanging;
        frequency = Wake;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive)
            return false;
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (p->isDead() || p->getMark(objectName()) > 0 || !p->hasSkill(this)) continue;
            if (qAbs(p->getHandcardNum() - p->getHp()) >= 3) {
                room->sendCompulsoryTriggerLog(p, objectName(), true, true);
                room->doSuperLightbox("wenyang", "choujue");
                room->setPlayerMark(p, "choujue", 1);
                if (room->changeMaxHpForAwakenSkill(p)) {
                    if (!p->hasSkill("beishui"))
                        room->acquireSkill(p, "beishui");
                    if (p->hasSkill("lvli"), true) {
                        LogMessage log;
                        log.type = "#JiexunChange";
                        log.from = p;
                        log.arg = "lvli";
                        room->sendLog(log);
                    }
                    QString translate;
                    if (p->getMark("beishui") > 0)
                         translate = Sanguosha->translate(":lvli4");
                    else
                        translate = Sanguosha->translate(":lvli2");
                    Sanguosha->addTranslationEntry(":lvli", translate.toStdString().c_str());
                    room->doNotify(p, QSanProtocol::S_COMMAND_UPDATE_SKILL, QVariant("lvli"));
                }
            }
        }
        return false;
    }
};

class Beishui : public PhaseChangeSkill
{
public:
    Beishui() : PhaseChangeSkill("beishui")
    {
        frequency = Wake;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Start) return false;
        if (player->getMark(objectName()) > 0) return false;
        if (player->getHandcardNum() >= 2 && player->getHp() >= 2) return false;
        Room *room = player->getRoom();
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        room->doSuperLightbox("wenyang", "beishui");
        room->setPlayerMark(player, "beishui", 1);
        if (room->changeMaxHpForAwakenSkill(player)) {
            if (!player->hasSkill("qingjiao"))
                room->acquireSkill(player, "qingjiao");
            if (player->hasSkill("lvli"), true) {
                LogMessage log;
                log.type = "#JiexunChange";
                log.from = player;
                log.arg = "lvli";
                room->sendLog(log);
            }
            QString translate;
            if (player->getMark("choujue") > 0)
                 translate = Sanguosha->translate(":lvli4");
            else
                translate = Sanguosha->translate(":lvli3");
            Sanguosha->addTranslationEntry(":lvli", translate.toStdString().c_str());
            room->doNotify(player, QSanProtocol::S_COMMAND_UPDATE_SKILL, QVariant("lvli"));
        }
        return false;
    }
};

class Qingjiao : public PhaseChangeSkill
{
public:
    Qingjiao() : PhaseChangeSkill("qingjiao")
    {
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        if (player->getPhase() == Player::Play && player->hasSkill(this)) {
            if (!player->canDiscard(player, "h")) return false;
            if (!player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(player, "qingjiao-Clear");
            player->throwAllHandCards();

            QList<int> ids = room->getDrawPile() + room->getDiscardPile();
            if (ids.isEmpty()) return false;

            QStringList names;
            foreach (int id, ids) {
                const Card *card = Sanguosha->getCard(id);
                QString name = card->objectName();
                if (card->isKindOf("Weapon"))
                    name = "Weapon";
                else if (card->isKindOf("Armor"))
                    name = "Armor";
                else if (card->isKindOf("DefensiveHorse"))
                    name = "DefensiveHorse";
                else if (card->isKindOf("OffensiveHorse"))
                    name = "OffensiveHorse";
                else if (card->isKindOf("Treasure"))
                    name = "Treasure";
                if (!names.contains(name))
                    names << name;
            }
            if (names.isEmpty()) return false;

            QStringList eight_names;
            int length = names.length();
            length = qMin(length, 8);
            for (int i = 0; i < length; i++) {
                int n = qrand() % names.length();
                QString str = names.at(n);
                names.removeOne(str);
                eight_names << str;
                if (names.isEmpty()) break;
            }
            if (eight_names.isEmpty()) return false;

            QList<int> get;
            foreach (QString name, eight_names) {
                QList<int> name_ids;
                foreach (int id, ids) {
                    const Card *card = Sanguosha->getCard(id);
                    const char *ch = name.toStdString().c_str();
                    if ((name == "Weapon" || name == "Armor" || name == "DefensiveHorse" || name == "OffensiveHorse" || name == "Treasure")
                            && card->isKindOf(ch))
                        name_ids << id;
                    else if ((name != "Weapon" && name != "Armor" && name != "DefensiveHorse" && name != "OffensiveHorse" && name != "Treasure") &&
                             name == card->objectName())
                        name_ids << id;
                }
                if (name_ids.isEmpty()) continue;
                get << name_ids.at(qrand() % name_ids.length());
            }
            if (get.isEmpty()) return false;
            DummyCard *dummy = new DummyCard(get);
            room->obtainCard(player, dummy, true);
            delete dummy;
        } else if (player->getPhase() == Player::Finish) {
            if (player->getMark("qingjiao-Clear") <= 0) return false;
            if (player->isNude()) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            room->setPlayerMark(player, "qingjiao-Clear", 0);
            player->throwAllHandCardsAndEquips();
        }
        return false;
    }
};

class Weicheng : public TriggerSkill
{
public:
    Weicheng() : TriggerSkill("weicheng")
    {
        events << CardsMoveOneTime;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (!move.from || move.from != player || !move.to || move.to == player || player->isDead()) return false;
        if (!move.from_places.contains(Player::PlaceHand) || move.to_place != Player::PlaceHand) return false;
        if (player->getHandcardNum() >= player->getHp()) return false;
        if (!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());
        player->drawCards(1, objectName());
        return false;
    }
};

DaoshuCard::DaoshuCard()
{
}

bool DaoshuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && !to_select->isKongcheng();
}

void DaoshuCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    Card::Suit suit = room->askForSuit(effect.from, "daoshu");

    LogMessage log;
    log.type = "#ChooseSuit";
    log.from = effect.from;
    log.arg = Card::Suit2String(suit);
    room->sendLog(log);

    if (effect.to->isKongcheng()) return;
    int id = room->askForCardChosen(effect.from, effect.to, "h", "daoshu");
    CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, effect.from->objectName());
    const Card *card = Sanguosha->getCard(id);
    room->obtainCard(effect.from, card, reason, true);

    if (effect.from->isDead() || effect.to->isDead()) return;
    if (card->getSuit() == suit) {
        room->damage(DamageStruct("daoshu", effect.from, effect.to));
        int times = effect.from->usedTimes("DaoshuCard");
        if (times > 0)
            room->addPlayerHistory(effect.from, "DaoshuCard", -times);
    } else {
        QList<const Card *> cards;
        foreach (const Card *c, effect.from->getCards("h")) {
            if (c->getSuit() != card->getSuit()) {
                cards << c;
            }
        }
        if (cards.isEmpty())
            room->showAllCards(effect.from);
        else {
            QStringList data;
            data << effect.to->objectName() << card->getSuitString();
            const Card *give = room->askForCard(effect.from, ".|^" + card->getSuitString() + "|.|hand!", "daoshu-give:" + effect.to->objectName(),
                                                data, Card::MethodNone);

            if (!give)
                give = cards.at(qrand() % cards.length());

            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "daoshu", QString());
            room->obtainCard(effect.to, give, reason, true);
        }
    }
}

class Daoshu : public ZeroCardViewAsSkill
{
public:
    Daoshu() : ZeroCardViewAsSkill("daoshu")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("DaoshuCard");
    }

    const Card *viewAs() const
    {
        DaoshuCard *card = new DaoshuCard;
        return card;
    }
};

class Xingzhao : public TriggerSkill
{
public:
    Xingzhao() : TriggerSkill("xingzhao")
    {
        events << CardUsed <<  EventPhaseChanging;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardUsed) {
            if (getWounded(room) < 2) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("EquipCard")) return false;
            room->sendCompulsoryTriggerLog(player, "xingzhao", true, true);
            player->drawCards(1, "xingzhao");
        } else if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::Discard || getWounded(room) < 3 || player->isSkipped(Player::Discard)) return false;
            room->sendCompulsoryTriggerLog(player, "xingzhao", true, true);
            player->skip(Player::Discard);
        }
        return false;
    }

private:
    int getWounded(Room *room) const
    {
        int n = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->isWounded())
                n++;
        }
        return n;
    }
};

class XingzhaoXunxun : public TriggerSkill
{
public:
    XingzhaoXunxun() : TriggerSkill("#xingzhao-xunxun")
    {
        events << GameStart << HpChanged << MaxHpChanged << EventAcquireSkill << EventLoseSkill << Death << Revived;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == EventLoseSkill) {
            if (data.toString() == "xingzhao") {
                QStringList xingzhao_xunxun = player->tag["xingzhao_xunxun"].toStringList();
                player->tag["xingzhao_xunxun"] = QVariant();
                QStringList detachList;
                foreach(QString skill_name, xingzhao_xunxun)
                    detachList.append("-" + skill_name);
                if (!detachList.isEmpty())
                    room->handleAcquireDetachSkills(player, detachList);
            }
            return false;
        } else if (triggerEvent == EventAcquireSkill) {
            if (data.toString() != "xingzhao") return false;
            if (player->isDead() || !player->hasSkill(this)) return false;
            XunxunChange(room, player);
            return false;
        } else if (triggerEvent == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who == player || !player->hasSkill(this)) return false;
        }

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (!p->isAlive() || !p->hasSkill("xingzhao")) continue;
            XunxunChange(room, p);
        }
        return false;
    }

private:
    void XunxunChange(Room *room, ServerPlayer *player) const
    {
        QStringList xingzhao_xunxun = player->tag["xingzhao_xunxun"].toStringList();
        int n = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->isWounded())
                n++;
        }
        if (n >= 1 && !xingzhao_xunxun.contains("xunxun") && !player->hasSkill("xunxun")) {
            xingzhao_xunxun << "xunxun";
            player->tag["xingzhao_xunxun"] = QVariant::fromValue(xingzhao_xunxun);
            room->sendCompulsoryTriggerLog(player, "xingzhao", true, true);
            room->acquireSkill(player, "xunxun");
        } else if (n < 1 && xingzhao_xunxun.contains("xunxun") && player->hasSkill("xunxun")) {
            player->tag["xingzhao_xunxun"] = QVariant();
            room->sendCompulsoryTriggerLog(player, "xingzhao", true, true);
            room->detachSkillFromPlayer(player, "xunxun");
        }
    }
};

class Daigong : public TriggerSkill
{
public:
    Daigong() : TriggerSkill("daigong")
    {
        events << DamageInflicted;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->isKongcheng()) return false;
        if (!room->hasCurrent() || player->getMark("daigong-Clear") > 0) return false;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.damage <= 0) return false;
        QVariant d = QVariant();
        if (damage.from && damage.from->isAlive())
            d = QVariant::fromValue(damage.from);
        if (!player->askForSkillInvoke(this, d)) return false;
        room->broadcastSkillInvoke(objectName());
        room->addPlayerMark(player, "daigong-Clear");
        room->showAllCards(player);

        if (!damage.from || damage.from->isDead()) return false;
        QStringList suits;
        foreach (const Card *c, player->getCards("h")) {
            if (!suits.contains(c->getSuitString()))
                suits << c->getSuitString();
        }

        bool has = false;
        foreach (const Card *c, damage.from->getCards("h")) {
            QString str = c->getSuitString();
            if (!suits.contains(str)) {
                has = true;
                break;
            }
        }

        if (!has) {
            LogMessage log;
            log.type = "#Daigong";
            log.from = damage.from;
            log.to << player;
            log.arg = QString::number(damage.damage);
            room->sendLog(log);
            return true;
        } else {
            QStringList all_suits;
            all_suits << "spade" << "club" << "heart" << "diamond" << "no_suit_black" << "no_suit_red" << "no_suit";
            foreach (QString str, suits) {
                all_suits.removeOne(str);
            }
            if (all_suits.isEmpty()) {
                LogMessage log;
                log.type = "#Daigong";
                log.from = damage.from;
                log.to << player;
                log.arg = QString::number(damage.damage);
                room->sendLog(log);
                return true;
            } else {
                QString suitt = all_suits.join(",");
                QString pattern = ".|" + suitt + "|.|.";
                QStringList data_list;
                data_list << player->objectName() << suitt;
                const Card *give = room->askForCard(damage.from, pattern, "daigong-give:" + player->objectName(), data_list, Card::MethodNone);
                if (!give) {
                    LogMessage log;
                    log.type = "#Daigong";
                    log.from = damage.from;
                    log.to << player;
                    log.arg = QString::number(damage.damage);
                    room->sendLog(log);
                    return true;
                } else {
                    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, damage.from->objectName(), player->objectName(), "daigong", QString());
                    room->obtainCard(player, give, reason, true);
                }
            }
        }
        return false;
    }
};

SpZhaoxinCard::SpZhaoxinCard()
{
    target_fixed= true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void SpZhaoxinCard::use(Room *, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    source->addToPile("zxwang", this);
    source->drawCards(getSubcards().length(), "spzhaoxin");
}

SpZhaoxinChooseCard::SpZhaoxinChooseCard()
{
    m_skillName = "spzhaoxin";
    target_fixed= true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

class SpZhaoxinVS : public ViewAsSkill
{
public:
    SpZhaoxinVS() : ViewAsSkill("spzhaoxin")
    {
       expand_pile = "zxwang";
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            return selected.length() < 3 - Self->getPile("zxwang").length() && Self->hasCard(to_select);
        }
        return Self->getPile("zxwang").contains(to_select->getEffectiveId());
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) {
            if (cards.isEmpty())
                return NULL;
            SpZhaoxinCard *c = new SpZhaoxinCard;
            c->addSubcards(cards);
            return c;
        }
        if (cards.length() != 1)
            return NULL;
        SpZhaoxinChooseCard *c = new SpZhaoxinChooseCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("SpZhaoxinCard") && player->getPile("zxwang").length() < 3;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return pattern == "@@spzhaoxin" && !player->getPile("zxwang").isEmpty();
    }
};

class SpZhaoxin : public TriggerSkill
{
public:
    SpZhaoxin() : TriggerSkill("spzhaoxin")
    {
        events << EventPhaseEnd;
        view_as_skill = new SpZhaoxinVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Draw) return false;
        foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
            if (player->isDead() || !p->hasSkill(this)) return false;
            if (p->isDead() || p->getPile("zxwang").isEmpty()) continue;
            if (p != player && !p->inMyAttackRange(player)) continue;
            const Card *card = room->askForUseCard(p, "@@spzhaoxin", "@spzhaoxin:" + player->objectName());
            if (!card) continue;
            room->fillAG(QList<int>() << card->getSubcards().first(), player);
            if (!player->askForSkillInvoke(this, QString("spzhaoxin_get:%1::%2").arg(card->getSubcards().first()).arg(p->objectName()), false)) {
                room->clearAG(player);
                continue;
            }
            room->clearAG(player);
            player->obtainCard(card, true);
            if (!p->askForSkillInvoke(this, QString("spzhaoxin_damage:%1").arg(player->objectName()), false)) continue;
            room->damage(DamageStruct("spzhaoxin", p, player));
        }
        return false;
    }
};

class Zhongzuo : public TriggerSkill
{
public:
    Zhongzuo() : TriggerSkill("zhongzuo")
    {
        events << EventPhaseChanging;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to != Player::NotActive) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(this) || p->getMark("zhongzuo-Clear") <= 0) continue;
            room->setPlayerMark(p, "zhongzuo-Clear", 0);
            ServerPlayer *target = room->askForPlayerChosen(p, room->getAlivePlayers(), objectName(), "@zhongzuo-invoke", true, true);
            if (!target) continue;
            room->broadcastSkillInvoke(objectName());
            target->drawCards(2, objectName());
            if (target->isWounded())
                p->drawCards(1, objectName());
        }
        return false;
    }
};

class ZhongzuoRecord : public TriggerSkill
{
public:
    ZhongzuoRecord() : TriggerSkill("#zhongzuo-record")
    {
        events << DamageDone;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (!room->hasCurrent()) return false;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to && damage.to->isAlive())
            room->addPlayerMark(damage.to, "zhongzuo-Clear");
        if (damage.from && damage.from->isAlive())
            room->addPlayerMark(damage.from, "zhongzuo-Clear");
        return false;
    }
};

class Wanlan : public TriggerSkill
{
public:
    Wanlan() : TriggerSkill("wanlan")
    {
        events << Dying;
        frequency = Limited;
        limit_mark = "@wanlanMark";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DyingStruct dying = data.value<DyingStruct>();
        ServerPlayer *who = dying.who;
        if (player->getMark("@wanlanMark") <= 0 || !player->canDiscard(player, "h") ||
                !player->askForSkillInvoke(this, QVariant::fromValue(who))) return false;
        room->broadcastSkillInvoke(objectName());
        room->doSuperLightbox("jiakui", "wanlan");
        room->removePlayerMark(player, "@wanlanMark");
        player->throwAllHandCards();
        if (who->getHp() < 1) {
            int n = qMin(1 - who->getHp(), who->getMaxHp() - who->getHp());
            if (n > 0)
                room->recover(who, RecoverStruct(player, NULL, n));
        }
        ServerPlayer *current = room->getCurrent();
        if (current && current->isAlive() && current->getPhase() != Player::NotActive)
            room->addPlayerMark(current, "wanlan_" + player->objectName() + "-Clear");
        return false;
    }
};

class WanlanDamage : public TriggerSkill
{
public:
    WanlanDamage() : TriggerSkill("#wanlan-damage")
    {
        events << AskForPeachesDone;
        frequency = Limited;
        //limit_mark = "@wanlan";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
    {
        ServerPlayer *current = room->getCurrent();
        if (current && current->isAlive() && current->getPhase() != Player::NotActive) {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (current->getMark("wanlan_" + p->objectName() + "-Clear") > 0) {
                    room->setPlayerMark(current, "wanlan_" + p->objectName() + "-Clear", 0);
                    room->damage(DamageStruct("wanlan", p ,current));
                    break;
                }
            }
        }
        return false;
    }
};

TongquCard::TongquCard()
{
    mute = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool TongquCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select->getMark("&tqqu") > 0 && to_select != Self;
}

bool TongquCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    int id = getSubcards().first();
    if (Self->canDiscard(Self, id))
        return true;
    return !targets.isEmpty();
}

void TongquCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    int id = getSubcards().first();
    const Card *c = Sanguosha->getCard(id);
    if (card_use.to.isEmpty()) {
        if (card_use.from->canDiscard(card_use.from, id)) {
            CardMoveReason reason(CardMoveReason::S_REASON_DISCARD, card_use.from->objectName(), "tongqu", QString());
            room->throwCard(this, reason, card_use.from, NULL);
        } else {
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(card_use.from)) {
                if (p->getMark("&tqqu") <= 0) continue;
                targets << p;
            }
            if (targets.isEmpty()) return;
            ServerPlayer *target = targets.at(qrand() % targets.length());
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), target->objectName(), "tongqu", QString());
            room->obtainCard(target, this, reason, false);
            if (target->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(target) && !target->isProhibited(target, c))
                room->useCard(CardUseStruct(c, target, target));
        }
    } else {
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, card_use.from->objectName(), card_use.to.first()->objectName(), "tongqu", QString());
        room->obtainCard(card_use.to.first(), this, reason, false);
        if (card_use.to.first()->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(card_use.to.first())
                && !card_use.to.first()->isProhibited(card_use.to.first(), c))
            room->useCard(CardUseStruct(c, card_use.to.first(), card_use.to.first()));
    }
}

class TongquVS : public OneCardViewAsSkill
{
public:
    TongquVS() : OneCardViewAsSkill("tongqu")
    {
        filter_pattern = ".";
        response_pattern = "@@tongqu!";
    }

    const Card *viewAs(const Card *originalcard) const
    {
        TongquCard *c = new TongquCard;
        c->addSubcard(originalcard->getId());
        return c;
    }
};

class Tongqu : public TriggerSkill
{
public:
    Tongqu() : TriggerSkill("tongqu")
    {
        events << DrawNCards << AfterDrawNCards;
        view_as_skill = new TongquVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DrawNCards) {
            if (player->getMark("&tqqu") <= 0) return false;
            int length = 0;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || !p->hasSkill("tongqu")) continue;
                room->notifySkillInvoked(p, "tongqu");
                room->broadcastSkillInvoke("tongqu");
                length++;
            }
            if (length <= 0) return false;
            room->setPlayerFlag(player, "tongqu");
            room->addPlayerMark(player, "tongqu-Clear", length);
            LogMessage log;
            log.type = "#HuaijuDraw";
            log.from = player;
            log.arg = "tongqu";
            log.arg2 = QString::number(length);
            room->sendLog(log);
            data = QVariant::fromValue(data.toInt() + length);
        } else {
            if (!player->hasFlag("tongqu")) return false;
            room->setPlayerFlag(player, "-tongqu");
            int n = player->getMark("tongqu-Clear");
            if (n <= 0) return false;
            room->addPlayerMark(player, "tongqu-Clear", 0);
            for (int i = 0; i < n; i++) {
                if (player->isDead() || player->isNude()) return false;
                if (!room->askForUseCard(player, "@@tongqu!", "@tongqu")) {
                    QList<int> dis;
                    foreach (const Card *c, player->getCards("he")) {
                        if (!player->canDiscard(player, c->getEffectiveId())) continue;
                        dis << c->getEffectiveId();
                    }
                    if (!dis.isEmpty()) {
                        int id = dis.at(qrand() % dis.length());
                        room->throwCard(id, player);
                    } else {
                        const Card *c = player->getCards("he").at(qrand() % player->getCards("he").length());
                        QList<ServerPlayer *> targets;
                        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                            if (p->getMark("&tqqu") <= 0) continue;
                            targets << p;
                        }
                        if (targets.isEmpty()) return false;
                        ServerPlayer *target = targets.at(qrand() % targets.length());
                        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "tongqu", QString());
                        room->obtainCard(target, c, reason, false);
                        if (target->isAlive() && c->isKindOf("EquipCard") && c->isAvailable(target) && !target->isProhibited(target, c))
                            room->useCard(CardUseStruct(c, target, target));
                    }
                }
            }
        }
        return false;
    }
};

class TongquTrigger : public TriggerSkill
{
public:
    TongquTrigger() : TriggerSkill("#tongqu-trigger")
    {
        events << GameStart << EventPhaseStart << Dying;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == GameStart) {
            room->sendCompulsoryTriggerLog(player, "tongqu", true, true);
            player->gainMark("&tqqu");
        } else if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Start) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->getMark("&tqqu") > 0) continue;
                targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, "tongqu", "@tongqu-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke("tongqu");
            room->loseHp(player);
            target->gainMark("&tqqu");
        } else {
            DyingStruct dying = data.value<DyingStruct>();
            if (dying.who->getMark("&tqqu") <= 0) return false;
            room->sendCompulsoryTriggerLog(player, "tongqu", true, true);
            dying.who->loseAllMarks("&tqqu");
        }
        return false;
    }
};

class NewWanlan : public TriggerSkill
{
public:
    NewWanlan() : TriggerSkill("newwanlan")
    {
        events << DamageInflicted;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.damage < player->getHp()) return false;
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (player->isDead()) return false;
            if (p->isDead() || !p->hasSkill(this) || !p->canDiscard(p, "e")) continue;
            if (!p->askForSkillInvoke(this, QVariant::fromValue(player))) continue;
            room->broadcastSkillInvoke(objectName());
            p->throwAllEquips();
            return true;
        }
        return false;
    }
};

class Qianchong : public TriggerSkill
{
public:
    Qianchong() : TriggerSkill("qianchong")
    {
        events << CardsMoveOneTime << EventPhaseStart << EventAcquireSkill;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play) return false;
            if (QianchongJudge(player, "black") || QianchongJudge(player, "red")) return false;
            int n = 0;
            QString choice = room->askForChoice(player, objectName(), "basic+trick+equip");
            LogMessage log;
            log.type = "#QianchongChoice";
            log.from = player;
            log.arg = objectName();
            log.arg2 = choice;
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName(), 3);
            if (choice == "basic")
                n = 1;
            else if (choice == "trick")
                n = 2;
            else
                n = 3;
            room->addPlayerMark(player, "qianchong-Clear", n);
        } else {
            bool flag = false;
            if (event == CardsMoveOneTime) {
                CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
                if (move.from && move.from == player && move.from_places.contains(Player::PlaceEquip))
                    flag = true;
                if (move.to && move.to == player && move.to_place == Player::PlaceEquip)
                    flag = true;
            } else {
                if (data.toString() == objectName())
                    flag = true;
            }
            if (flag == true) {
                QString skill = player->property("qianchong_skill").toString();
                QStringList skills;
                int index = 1;
                if (QianchongJudge(player, "black") && !player->hasSkill("weimu") && skill == QString()) {
                    room->setPlayerProperty(player, "qianchong_skill", "weimu");
                    skills << "weimu";
                }
                if (!QianchongJudge(player, "black") && player->hasSkill("weimu") && skill == "weimu") {
                    room->setPlayerProperty(player, "qianchong_skill", QString());
                    skills << "-weimu";
                }
                if (QianchongJudge(player, "red") && !player->hasSkill("mingzhe") && skill == QString()) {
                    room->setPlayerProperty(player, "qianchong_skill", "mingzhe");
                    skills << "mingzhe";
                    index = 2;
                }
                if (!QianchongJudge(player, "red") && player->hasSkill("mingzhe") && skill == "mingzhe") {
                    room->setPlayerProperty(player, "qianchong_skill", QString());
                    skills << "-mingzhe";
                    index = 2;
                }
                if (!skills.isEmpty()) {
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true, index);
                    room->handleAcquireDetachSkills(player, skills);
                }
            }
        }
        return false;
    }
private:
    bool QianchongJudge(ServerPlayer *player, const QString &type) const
    {
        QList<const Card *>equips = player->getEquips();
        if (equips.isEmpty()) return false;
        if (type == "red") {
            foreach (const Card *c, equips) {
                if (!c->isRed())
                    return false;
            }
        } else if (type == "black") {
            foreach (const Card *c, equips) {
                if (!c->isBlack())
                    return false;
            }
        }
        return true;
    }
};

class QianchongLose : public TriggerSkill
{
public:
    QianchongLose() : TriggerSkill("#qianchong-lose")
    {
        events << EventLoseSkill;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (data.toString() != "qianchong") return false;
        QString skill = player->property("qianchong_skill").toString();
        room->setPlayerProperty(player, "qianchong_skill", QString());
        if (skill == QString()) return false;
        if (!player->hasSkill(skill)) return false;
        room->handleAcquireDetachSkills(player, "-" + skill);
        return false;
    }
};

class QianchongTargetMod : public TargetModSkill
{
public:
    QianchongTargetMod() : TargetModSkill("#qianchong-target")
    {
        pattern = ".";
    }

    int getResidueNum(const Player *from, const Card *card, const Player *) const
    {
        if (card->getTypeId() == from->getMark("qianchong-Clear"))
            return 1000;
        else
            return 0;
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (card->getTypeId() == from->getMark("qianchong-Clear"))
            return 1000;
        else
            return 0;
    }
};

class Shangjian : public PhaseChangeSkill
{
public:
    Shangjian() : PhaseChangeSkill("shangjian")
    {
        frequency = Frequent;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
       if (player->getPhase() != Player::Finish) return false;
       Room *room = player->getRoom();
       foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
           int n = p->getMark("shangjian-Clear");
           if (p->isDead() || n > p->getHp() || n <= 0 || !p->hasSkill(this)) continue;
           if (!p->askForSkillInvoke(this)) continue;
           room->broadcastSkillInvoke(objectName());
           room->setPlayerMark(p, "shangjian-Clear", 0);
           p->drawCards(n, objectName());
       }
       return false;
    }
};

class ShangjianMark : public TriggerSkill
{
public:
    ShangjianMark() : TriggerSkill("#shangjian-mark")
    {
        events << CardsMoveOneTime;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from && move.from == player) {
            if (move.to && move.to == player && (move.to_place == Player::PlaceEquip || move.to_place == Player::PlaceHand)) return false;
            int mark = 0;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) == Player::PlaceEquip || move.from_places.at(i) == Player::PlaceHand)
                    mark++;
            }
            if (mark > 0)
                room->addPlayerMark(player, "shangjian-Clear");
        }
        return false;
    }
};

class Chijie : public TriggerSkill
{
public:
    Chijie() : TriggerSkill("chijie")
    {
        events << Damaged << DamageCaused;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || damage.card->isKindOf("SkillCard")) return false;
        if (event == Damaged) {
            if (!room->hasCurrent() || player->getMark("chijie-Clear") > 0 || !player->hasSkill(this)) return false;
            if (player->hasSkill(this) && player->askForSkillInvoke(this, QString("chijie_damage:" + damage.card->objectName()))) {
                room->addPlayerMark(player, "chijie-Clear");
                room->setCardFlag(damage.card, "chijie");
                room->setCardFlag(damage.card, "chijie_" + player->objectName());
            }
        } else {
            if (damage.to->isDead() || damage.damage <= 0) return false;
            if (!damage.card->hasFlag("chijie") || damage.card->hasFlag("chijie_" + damage.to->objectName())) return false;
            LogMessage log;
            log.type = "#ChijiePrevent";
            log.from = damage.to;
            log.arg = objectName();
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            return true;
        }
        return false;
    }
};

YinjuCard::YinjuCard()
{
}

void YinjuCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->doSuperLightbox("xinpi", "yinju");
    room->removePlayerMark(effect.from, "@yinjuMark");
    if (!room->hasCurrent()) return;
    room->addPlayerMark(effect.from, "yinju_from-Clear");
    room->addPlayerMark(effect.to, "yinju_to-Clear");
}

class YinjuVS : public ZeroCardViewAsSkill
{
public:
    YinjuVS() : ZeroCardViewAsSkill("yinju")
    {
        frequency = Limited;
        limit_mark = "@yinjuMark";
    }

    const Card *viewAs() const
    {
        return new YinjuCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@yinjuMark") > 0;
    }
};

class Yinju : public TriggerSkill
{
public:
    Yinju() : TriggerSkill("yinju")
    {
        events << DamageCaused << TargetSpecified;
        view_as_skill = new YinjuVS;
        frequency = Limited;
        limit_mark = "@yinjuMark";
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == DamageCaused) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->isDead()) return false;
            if (player->getMark("yinju_from-Clear") <= 0 || damage.to->getMark("yinju_to-Clear") <= 0) return false;
            LogMessage log;
            log.type = damage.to->getLostHp() > 0 ? "#YinjuPrevent1" : "#YinjuPrevent2";
            log.from = player;
            log.to << damage.to;
            log.arg = objectName();
            log.arg2 = QString::number(damage.damage);
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName());
            int n = qMin(damage.damage, damage.to->getMaxHp() - damage.to->getHp());
            if (n > 0)
                room->recover(damage.to, RecoverStruct(player, NULL, n));
            return true;
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            if (player->getMark("yinju_from-Clear") <= 0) return false;
            foreach (ServerPlayer *p, use.to) {
                if (p->getMark("yinju_to-Clear") <= 0) continue;
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                player->drawCards(1, objectName());
            }
        }
        return false;
    }
};

class Zhuilie : public TriggerSkill
{
public:
    Zhuilie() : TriggerSkill("zhuilie")
    {
        events << TargetSpecified << ConfirmDamage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("Slash")) return false;
            foreach (ServerPlayer *p, use.to) {
                if (!player->inMyAttackRange(p)) {
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                    if (use.m_addHistory)
                        room->addPlayerHistory(player, use.card->getClassName(), -1);
                    JudgeStruct judge;
                    judge.reason = objectName();
                    judge.who = player;
                    judge.pattern = "Weapon,OffensiveHorse,DefensiveHorse";
                    judge.good = true;
                    room->judge(judge);

                    if (judge.isGood())
                        room->setCardFlag(use.card, "zhuilie_" + p->objectName());
                    else
                        room->loseHp(player);
                }
            }
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash") || damage.to->isDead()) return false;
            if (!damage.card->hasFlag("zhuilie_" + damage.to->objectName())) return false;
            room->setCardFlag(damage.card, "-zhuilie_" + damage.to->objectName());
            LogMessage log;
            log.type = damage.to->getHp() > 0 ? "#ZhuilieDamage" : "#ZhuiliePrevent";
            log.from = player;
            log.to << damage.to;
            log.arg = objectName();
            log.arg2 = QString::number(qMax(0, damage.to->getHp()));
            room->sendLog(log);
            if (damage.to->getHp() > 0) {
                damage.damage = damage.to->getHp();
                data = QVariant::fromValue(damage);
            } else
                return true;
            return false;
        }
        return false;
    }
};

class ZhuilieSlash : public TargetModSkill
{
public:
    ZhuilieSlash() : TargetModSkill("#zhuilie-slash")
    {
    }

    int getDistanceLimit(const Player *from, const Card *, const Player *) const
    {
        if (from->hasSkill("zhuilie"))
            return 1000;
        else
            return 0;
    }
};

class Tuiyan : public PhaseChangeSkill
{
public:
    Tuiyan() : PhaseChangeSkill("tuiyan")
    {
        frequency = Frequent;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
       if (player->getPhase() != Player::Play) return false;
       if (!player->askForSkillInvoke(this)) return false;
       Room *room = player->getRoom();
       room->broadcastSkillInvoke(objectName());
       QList<int> ids = room->getNCards(2, false);
       room->returnToTopDrawPile(ids);
       LogMessage log;
       log.type = "$ViewDrawPile";
       log.arg = QString::number(2);
       log.card_str = IntList2StringList(ids).join("+");
       room->sendLog(log, player);

       log.type = "#ViewDrawPile";
       room->sendLog(log, room->getOtherPlayers(player, true));

       room->fillAG(ids, player);
       room->askForAG(player, ids, true, objectName());
       room->clearAG(player);
       return false;
    }
};

BusuanCard::BusuanCard()
{
}

void BusuanCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    QStringList alllist;
    QList<int> ids;
    foreach(int id, Sanguosha->getRandomCards()) {
        const Card *c = Sanguosha->getEngineCard(id);
        if (c->isKindOf("EquipCard")) continue;
        if (alllist.contains(c->objectName())) continue;
        alllist << c->objectName();
        ids << id;
    }
    if (ids.isEmpty()) return;
    room->fillAG(ids, effect.from);
    int id = -1, id2 = -1;
    id = room->askForAG(effect.from, ids, false, "busuan");
    room->clearAG(effect.from);
    ids.removeOne(id);

    const Card *first_card = Sanguosha->getEngineCard(id);
    if (first_card->isKindOf("Slash")) {
        foreach (int id, ids) {
            if (Sanguosha->getEngineCard(id)->isKindOf("Slash"))
                ids.removeOne(id);
        }
    }

    if (!ids.isEmpty()) {
        room->fillAG(ids, effect.from);
        id2 = room->askForAG(effect.from, ids, false, "busuan");
        room->clearAG(effect.from);
    }

    QStringList list;
    QString name = first_card->objectName();
    list << name;
    QString name2 = QString();
    if (id2 >= 0) {
        name2 = Sanguosha->getEngineCard(id2)->objectName();
        list << name2;
    }
    LogMessage log;
    log.type = id2 >= 0 ? "#Busuantwo" : "#Busuanone";
    log.from = effect.from;
    log.arg = name;
    log.arg2 = name2;
    room->sendLog(log);

    if (list.isEmpty()) return;
    room->setPlayerProperty(effect.to, "busuan_names", list);
}

class BusuanVS : public ZeroCardViewAsSkill
{
public:
    BusuanVS() : ZeroCardViewAsSkill("busuan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("BusuanCard");
    }

    const Card *viewAs() const
    {
        return new BusuanCard;
    }
};

class Busuan : public DrawCardsSkill
{
public:
    Busuan() : DrawCardsSkill("busuan")
    {
        view_as_skill = new BusuanVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    int getPriority(TriggerEvent) const
    {
        return 0;
    }

    int getDrawNum(ServerPlayer *player, int n) const
    {
        QStringList list = player->property("busuan_names").toStringList();
        if (list.isEmpty()) return n;

        Room *room = player->getRoom();
        room->setPlayerProperty(player, "busuan_names", QStringList());
        LogMessage log;
        log.type = "#BusuanEffect";
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);

        DummyCard *dummy = new DummyCard();
        QList<int> all = room->getDrawPile() + room->getDiscardPile();
        foreach (QString str, list) {
            QList<int> ids;
            foreach (int id, all) {
                if (Sanguosha->getCard(id)->objectName() == str)
                    ids << id;
            }
            if (ids.isEmpty()) continue;
            int id = ids.at(qrand() % ids.length());
            dummy->addSubcard(id);
        }

        if (dummy->subcardsLength() > 0) {
            room->obtainCard(player, dummy, true);
        }
        delete dummy;
        return 0;
    }
};

class Mingjie : public TriggerSkill
{
public:
    Mingjie() : TriggerSkill("mingjie")
    {
        events << EventPhaseStart << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;
            while (player->isAlive()) {
                if (player->isDead()) break;
                if (player->getMark("mingjie-Clear") > 0) break;
                if (player->getMark("mingjie_num-Clear") > 2) break;
                if (!player->askForSkillInvoke(this)) return false;
                room->broadcastSkillInvoke(objectName());
                player->drawCards(1, objectName(), true, true);
            }
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.reason.m_skillName == objectName() && move.to && move.to == player) {
                if (move.card_ids.length() <= 0) return false;
                room->addPlayerMark(player, "mingjie_num-Clear", move.card_ids.length());
                foreach (int id, move.card_ids) {
                    if (!Sanguosha->getCard(id)->isBlack()) continue;
                    room->addPlayerMark(player, "mingjie-Clear");
                    room->loseHp(player);
                    break;
                }
            }
        }
        return false;
    }
};

SpQianxinCard::SpQianxinCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool SpQianxinCard::targetFilter(const QList<const Player *> &targets, const Player *, const Player *) const
{
    return targets.isEmpty();
}

void SpQianxinCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    int length = room->getDrawPile().length();
    int alive = room->alivePlayerCount();
    if (alive > length)
        room->swapPile();

    QVariantList list = room->getTag("spqianxin_xin").toList();
    foreach (int id, getSubcards()) {
        if (!list.contains(QVariant(id)))
            list << id;
    }
    room->setTag("spqianxin_xin", QVariant::fromValue(list));
    room->addPlayerMark(effect.to, "spspqianxin_target" + effect.from->objectName());
    foreach (ServerPlayer *p, room->getAllPlayers(true))
        room->addPlayerMark(p, "spqianxin_disabled");

    if (room->getDrawPile().length() <= alive)
        room->moveCardsToEndOfDrawpile(effect.from, getSubcards(), "spqianxin");
    else {
        QStringList choices;
        int n = 1;
        int len = room->getDrawPile().length();
        while (n * alive <= len) {
            choices << QString::number(n * alive);
            n++;
        }
        if (choices.isEmpty()) return;
        QString choice = room->askForChoice(effect.from, "spqianxin", choices.join("+"));
        room->moveCardsInToDrawpile(effect.from, this, "spqianxin", choice.toInt());
    }
}

class SpQianxinVS : public ViewAsSkill
{
public:
    SpQianxinVS() : ViewAsSkill("spqianxin")
    {
    }

    bool viewFilter(const QList<const Card *> &, const Card *to_select) const
    {
        return !to_select->isEquipped();
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("SpQianxinCard") && player->getMark("spqianxin_disabled") == 0;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;

        SpQianxinCard *card = new SpQianxinCard;
        card->addSubcards(cards);
        return card;
    }
};

class SpQianxin : public TriggerSkill
{
public:
    SpQianxin() : TriggerSkill("spqianxin")
    {
        events << EventPhaseStart << CardsMoveOneTime << EventPhaseChanging;
        view_as_skill = new SpQianxinVS;
        global = true;
    }

    int getPriority(TriggerEvent event) const
    {
        if (event == EventPhaseChanging)
            return 0;
        else
            return TriggerSkill::getPriority(event);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Discard) return false;
            if (player->getMark("spqianxin-Clear") <= 0) return false;
            room->setPlayerMark(player, "spqianxin-Clear", 0);
            foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
                if (player->isDead()) return false;
                if (p->isDead() || !p->hasSkill(this)) continue;
                if (player->getMark("spspqianxin_target" + p->objectName()) <= 0) continue;
                if (room->getTag("spqianxin_xin").toList().isEmpty())
                    room->setPlayerMark(player, "spspqianxin_target" + p->objectName(), 0);
                QStringList choices;
                choices << "draw";
                if (player->getMaxCards() > 0)
                    choices << "maxcards";
                if (choices.isEmpty()) continue;
                room->sendCompulsoryTriggerLog(p, objectName(), true, true);
                QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(p));

                LogMessage log;
                log.type = "#FumianFirstChoice";
                log.from = player;
                log.arg = "spqianxin:" + choice;
                room->sendLog(log);

                if ( choice == "draw") {
                    if (p->getHandcardNum() < 4)
                        p->drawCards(4 - p->getHandcardNum(), objectName());
                } else
                    room->addMaxCards(player, -2);
            }
        } else if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.from && move.from_places.contains(Player::DrawPile)) {
                QVariantList list = room->getTag("spqianxin_xin").toList();
                QList<int> ids = VariantList2IntList(list);
                foreach (int id, move.card_ids) {
                    if (ids.contains(id)) {
                        ids.removeOne(id);
                        if (move.to && move.to->isAlive() && move.to_place == Player::PlaceHand) {
                            QVariantList get = room->getTag("spqianxin_xin_get").toList();
                            if (!get.contains(QVariant(id))) {
                                get << id;
                            }
                            room->setTag("spqianxin_xin_get", QVariant::fromValue(get));
                            ServerPlayer *to = room->findPlayerByObjectName(move.to->objectName());
                            if (to && !to->isDead())
                                room->addPlayerMark(to, "spqianxin-Clear");
                        }
                    }
                }
                QVariantList new_list = IntList2VariantList(ids);
                room->setTag("spqianxin_xin", QVariant::fromValue(new_list));
                if (ids.isEmpty()) {
                    room->removeTag("spqianxin_xin");
                    foreach (ServerPlayer *p, room->getAllPlayers(true))
                        room->setPlayerMark(p, "spqianxin_disabled", 0);
                }
            }
            if (move.from && move.from->isAlive() && move.from_places.contains(Player::PlaceHand)) {
                QVariantList get = room->getTag("spqianxin_xin_get").toList();
                QList<int> ids = VariantList2IntList(get);
                ServerPlayer *from = room->findPlayerByObjectName(move.from->objectName());
                if (!from || from->isDead()) return false;
                foreach (int id, move.card_ids) {
                    if (ids.contains(id)) {
                        ids.removeOne(id);
                        room->removePlayerMark(from, "spqianxin-Clear");
                    }
                }
                QVariantList new_list = IntList2VariantList(ids);
                room->setTag("spqianxin_xin_get", QVariant::fromValue(new_list));
            }
        } else if (event == EventPhaseChanging){
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            room->removeTag("spqianxin_xin_get");
        }
        return false;
    }
};

class Zhenxing : public TriggerSkill
{
public:
    Zhenxing() : TriggerSkill("zhenxing")
    {
        events << EventPhaseStart << Damaged;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;
        }
        if (!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());

        QStringList choices;
        choices << "1" << "2" << "3";
        QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
        QList<int> views = room->getNCards(choice.toInt(), false);
        room->returnToTopDrawPile(views);

        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = player;
        log.arg = choice;
        log.card_str = IntList2StringList(views).join("+");
        room->sendLog(log, player);
        log.type = "#ViewDrawPile";
        room->sendLog(log, room->getOtherPlayers(player, true));

        QStringList suits, duplication;
        foreach (int id, views) {
            QString suit = Sanguosha->getCard(id)->getSuitString();
            if (!suits.contains(suit))
                suits << suit;
            else
                duplication << suit;
        }

        QList<int> enabled, disabled;
        foreach (int id, views) {
            if (duplication.contains(Sanguosha->getCard(id)->getSuitString()))
                disabled << id;
            else
                enabled << id;
        }

        if (enabled.isEmpty()) {
            room->fillAG(views, player);
            room->askForAG(player, views, true, objectName());
            room->clearAG(player);
            return false;
        }
        room->fillAG(views, player, disabled);
        if (!player->getAI() && enabled.length() == 1)
            room->getThread()->delay(1000);
        int id = room->askForAG(player, enabled, false, objectName());
        room->clearAG(player);
        room->obtainCard(player, id, false);
        return false;
    }
};

MobileSpQianxinCard::MobileSpQianxinCard()
{
    will_throw = false;
    target_fixed = true;
    handling_method = Card::MethodNone;
}

void MobileSpQianxinCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> ids = getSubcards();
    QList<ServerPlayer *> players = room->getOtherPlayers(source);

    int n = 0;
    QList<CardsMoveStruct> moves;
    while (n < 2) {
        if (ids.isEmpty() || players.isEmpty()) break;

        int id = ids.at(qrand() % ids.length());
        ids.removeOne(id);

        ServerPlayer *to = players.at(qrand() % players.length());
        players.removeOne(to);

        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, source->objectName(), to->objectName(), "mobilespqianxin", QString());
        CardsMoveStruct move(QList<int>() << id, to, Player::PlaceHand, reason);
        moves << move;
    }
    if (moves.isEmpty()) return;
    room->moveCardsAtomic(moves, false);
}

class MobileSpQianxinVS : public ViewAsSkill
{
public:
    MobileSpQianxinVS() : ViewAsSkill("mobilespqianxin")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped()) return false;
        int n = Self->getAliveSiblings().length();
        n = qMin(2, n);
        return selected.length() < n;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileSpQianxinCard");
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;

        MobileSpQianxinCard *card = new MobileSpQianxinCard;
        card->addSubcards(cards);
        return card;
    }
};

class MobileSpQianxin : public PhaseChangeSkill
{
public:
    MobileSpQianxin() : PhaseChangeSkill("mobilespqianxin")
    {
        view_as_skill = new MobileSpQianxinVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target && target->isAlive() && target->getPhase() == Player::RoundStart && !target->tag["mobilespqianxin_xin"].toList().isEmpty();
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        player->tag.remove("mobilespqianxin_xin");
        Room *room = player->getRoom();
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (player->isDead()) return false;
            if (p->isDead() || !p->hasSkill(this)) continue;
            QStringList choices;
            choices << "draw";
            if (player->getMaxCards() > 0)
                choices << "maxcards";
            QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(p));
            LogMessage log;
            log.type = "#FumianFirstChoice";
            log.from = player;
            log.arg = "mobilespqianxin:" + choice;
            room->sendLog(log);

            if (choice == "draw")
                p->drawCards(2, objectName());
            else
                room->addMaxCards(player, -2);
        }
        return false;
    }
};

class MobileSpQianxinMove : public TriggerSkill
{
public:
    MobileSpQianxinMove() : TriggerSkill("#mobilespqianxin-move")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.from && move.from_places.contains(Player::PlaceHand) && move.to && move.to_place == Player::PlaceHand
                && move.reason.m_skillName == "mobilespqianxin") {
            QVariantList xin = move.to->tag["mobilespqianxin_xin"].toList();
            foreach (int id, move.card_ids) {
                if (xin.contains(id)) continue;
                xin << id;
            }
            move.to->tag["mobilespqianxin_xin"] = xin;
        } else if (move.from && move.from_places.contains(Player::PlaceHand)) {
            QVariantList xin = move.from->tag["mobilespqianxin_xin"].toList();
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) == Player::PlaceHand) {
                    if (!xin.contains(move.card_ids.at(i))) continue;
                    xin.removeOne(move.card_ids.at(i));
                }
            }
            move.from->tag["mobilespqianxin_xin"] = xin;
        }
        return false;
    }
};

class MobileZhenxing : public TriggerSkill
{
public:
    MobileZhenxing() : TriggerSkill("mobilezhenxing")
    {
        events << EventPhaseStart << Damaged;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;
        }
        if (!player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke(objectName());

        QList<int> views = room->getNCards(3, false);
        room->returnToTopDrawPile(views);

        LogMessage log;
        log.type = "$ViewDrawPile";
        log.from = player;
        log.arg = QString::number(3);
        log.card_str = IntList2StringList(views).join("+");
        room->sendLog(log, player);
        log.type = "#ViewDrawPile";
        room->sendLog(log, room->getOtherPlayers(player, true));

        QStringList suits, duplication;
        foreach (int id, views) {
            QString suit = Sanguosha->getCard(id)->getSuitString();
            if (!suits.contains(suit))
                suits << suit;
            else
                duplication << suit;
        }

        QList<int> enabled, disabled;
        foreach (int id, views) {
            if (duplication.contains(Sanguosha->getCard(id)->getSuitString()))
                disabled << id;
            else
                enabled << id;
        }

        if (enabled.isEmpty()) {
            room->fillAG(views, player);
            room->askForAG(player, views, true, objectName());
            room->clearAG(player);
            return false;
        }
        room->fillAG(views, player, disabled);
        if (!player->getAI() && enabled.length() == 1)
            room->getThread()->delay(1000);
        int id = room->askForAG(player, enabled, false, objectName());
        room->clearAG(player);
        room->obtainCard(player, id, false);
        return false;
    }
};

JijieCard::JijieCard()
{
    target_fixed = true;
}

void JijieCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QList<int> ids = room->getNCards(1, false, false);
    room->returnToEndDrawPile(ids);

    QList<ServerPlayer *> _player;
    _player.append(source);

    CardsMoveStruct move(ids, NULL, source, Player::PlaceTable, Player::PlaceHand,
        CardMoveReason(CardMoveReason::S_REASON_PREVIEW, source->objectName(), "jijie", QString()));
    QList<CardsMoveStruct> moves;
    moves.append(move);
    room->notifyMoveCards(true, moves, false, _player);
    room->notifyMoveCards(false, moves, false, _player);

    QList<int> new_ids = ids;
    if (room->askForYiji(source, ids, "jijie", true, false, true, -1, room->getAlivePlayers())) {
        CardsMoveStruct move1(new_ids, source, NULL, Player::PlaceHand, Player::PlaceTable,
            CardMoveReason(CardMoveReason::S_REASON_PREVIEW, source->objectName(), "jijie", QString()));
        QList<CardsMoveStruct> moves1;
        moves1.append(move1);
        room->notifyMoveCards(true, moves1, false, _player);
        room->notifyMoveCards(false, moves1, false, _player);
        return;
    }
    CardsMoveStruct move1(new_ids, source, NULL, Player::PlaceHand, Player::PlaceTable,
        CardMoveReason(CardMoveReason::S_REASON_PREVIEW, source->objectName(), "jijie", QString()));
    QList<CardsMoveStruct> moves1;
    moves1.append(move1);
    room->notifyMoveCards(true, moves1, false, _player);
    room->notifyMoveCards(false, moves1, false, _player);

    CardMoveReason reason(CardMoveReason::S_REASON_PREVIEW, source->objectName(), "jijie", QString());
    room->obtainCard(source, Sanguosha->getCard(ids.first()), reason, false);
}

class Jijie : public ZeroCardViewAsSkill
{
public:
    Jijie() : ZeroCardViewAsSkill("jijie")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("JijieCard");
    }

    const Card *viewAs() const
    {
        return new JijieCard;
    }
};

class Jiyuan : public TriggerSkill
{
public:
    Jiyuan() : TriggerSkill("jiyuan")
    {
        events << Dying << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Dying) {
            DyingStruct dying = data.value<DyingStruct>();
            if (!dying.who || dying.who->isDead()) return false;
            if (!player->askForSkillInvoke(this, QVariant::fromValue(dying.who))) return false;
            room->broadcastSkillInvoke(objectName());
            dying.who->drawCards(1, objectName());
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.reason.m_playerId != player->objectName()) return false;
            if ((move.to && move.to == player) || !move.to || move.to->isDead()) return false;
            if (move.reason.m_reason != CardMoveReason::S_REASON_GIVE && move.reason.m_reason != CardMoveReason::S_REASON_PREVIEWGIVE) return false;
            if (move.to_place != Player::PlaceHand) return false;
            ServerPlayer *to = room->findPlayerByObjectName(move.to->objectName());
            if (!to || to->isDead()) return false;
            if (!player->askForSkillInvoke(this, QVariant::fromValue(to))) return false;
            room->broadcastSkillInvoke(objectName());
            to->drawCards(1, objectName());
        }
        return false;
    }
};

ZiyuanCard::ZiyuanCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void ZiyuanCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "ziyuan", QString());
    room->obtainCard(effect.to, this, reason, true);

    if (effect.to->isAlive())
        room->recover(effect.to, RecoverStruct(effect.from));
}

class Ziyuan : public ViewAsSkill
{
public:
    Ziyuan() : ViewAsSkill("ziyuan")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (to_select->isEquipped()) return false;
        int n = to_select->getNumber();
        int num = 0;
        foreach (const Card *c, selected) {
            num = num + c->getNumber();
        }
        return num + n <= 13;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("ZiyuanCard");
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;
        int num = 0;
        foreach (const Card *c, cards) {
            num = num + c->getNumber();
        }
        if (num != 13) return NULL;

        ZiyuanCard *card = new ZiyuanCard;
        card->addSubcards(cards);
        return card;
    }
};

class Jugu : public GameStartSkill
{
public:
    Jugu() : GameStartSkill("jugu")
    {
        frequency = Compulsory;
    }

    void onGameStart(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        int n = player->getMaxHp();
        if (n <= 0) return;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        player->drawCards(n, objectName());
    }
};

class JuguMax : public MaxCardsSkill
{
public:
    JuguMax() : MaxCardsSkill("#jugu-max")
    {
    }

    int getExtra(const Player *target) const
    {
        if (target->hasSkill("jugu"))
            return target->getMaxHp();
        else
            return 0;
    }
};

class Bingzheng : public TriggerSkill
{
public:
    Bingzheng() : TriggerSkill("bingzheng")
    {
        events << EventPhaseEnd;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Play) return false;
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHp() != p->getHandcardNum())
                targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@bingzheng-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        QStringList choices;
        if (!target->isKongcheng())
            choices << "discard";
        choices << "draw";
        QString choice = room->askForChoice(player, objectName(), choices.join("+"), QVariant::fromValue(target));
        if (choice == "draw")
            target->drawCards(1, objectName());
        else {
            if (!target->canDiscard(target, "h")) return false;
            room->askForDiscard(target, objectName(), 1, 1);
        }
        if (target->isAlive() && player->isAlive() && target->getHp() == target->getHandcardNum()) {
            player->drawCards(1, objectName());
            if (player->isNude() || player == target) return false;
            QList<ServerPlayer *> players;
            players << target;
            QList<int> give = player->handCards() + player->getEquipsId();
            room->askForYiji(player, give, objectName(), false, false, true, -1, players, CardMoveReason(),
                             "bingzheng-give:" + target->objectName());
        }
        return false;
    }
};

class SheyanVS : public ZeroCardViewAsSkill
{
public:
    SheyanVS() : ZeroCardViewAsSkill("sheyan")
    {
        response_pattern = "@@sheyan!";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        return new ExtraCollateralCard;
    }
};

class Sheyan : public TriggerSkill
{
public:
    Sheyan() : TriggerSkill("sheyan")
    {
        events << TargetConfirming;
        view_as_skill = new SheyanVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isNDTrick() || !use.to.contains(player)) return false;

        room->setCardFlag(use.card, "sheyan_distance");
        QList<ServerPlayer *> ava;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (use.card->isKindOf("AOE") && p == use.from) continue;
            if (use.to.contains(p) || room->isProhibited(use.from, p, use.card)) continue;
            if (use.card->targetFixed())
                ava << p;
            else {
                if (use.card->targetFilter(QList<const Player *>(), p, use.from))
                    ava << p;
            }
        }

        QStringList choices;
        if (!ava.isEmpty())
            choices << "add";
        if (use.to.length() > 1)
            choices << "remove";
        if (choices.isEmpty()) return false;
        choices << "cancel";

        QString choice = room->askForChoice(player, objectName(), choices.join("+"), data);
        if (choice == "cancel") return false;

        if (!use.card->isKindOf("Collateral")) {
            room->setCardFlag(use.card, "-sheyan_distance");
            if (choice == "add") {
                ServerPlayer *target = room->askForPlayerChosen(player, ava, objectName(), "@sheyan-add:" + use.card->objectName());
                LogMessage log;
                log.type = "#QiaoshuiAdd";
                log.from = player;
                log.to << target;
                log.card_str = use.card->toString();
                log.arg = "sheyan";
                room->sendLog(log);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName());
                use.to << target;
                room->sortByActionOrder(use.to);
            } else {
                ServerPlayer *target = room->askForPlayerChosen(player, use.to, objectName(), "@sheyan-remove:" + use.card->objectName());
                LogMessage log;
                log.type = "#QiaoshuiRemove";
                log.from = player;
                log.to << target;
                log.card_str = use.card->toString();
                log.arg = "sheyan";
                room->sendLog(log);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName());
                use.to.removeOne(target);
            }
        } else {
            if (choice == "add") {
                QStringList tos;
                foreach(ServerPlayer *t, use.to)
                    tos.append(t->objectName());

                room->setPlayerProperty(player, "extra_collateral", use.card->toString());
                room->setPlayerProperty(player, "extra_collateral_current_targets", tos);
                room->askForUseCard(player, "@@sheyan!", "@sheyan:" + use.card->objectName());
                room->setPlayerProperty(player, "extra_collateral", QString());
                room->setPlayerProperty(player, "extra_collateral_current_targets", QString("+"));
                room->setCardFlag(use.card, "-sheyan_distance");

                bool extra = false;
                foreach(ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->hasFlag("ExtraCollateralTarget")) {
                        room->setPlayerFlag(p,"-ExtraCollateralTarget");
                        extra = true;
                        LogMessage log;
                        log.type = "#QiaoshuiAdd";
                        log.from = player;
                        log.to << p;
                        log.card_str = use.card->toString();
                        log.arg = "sheyan";
                        room->sendLog(log);
                        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), p->objectName());
                        room->notifySkillInvoked(player, objectName());
                        room->broadcastSkillInvoke(objectName());

                        use.to.append(p);
                        room->sortByActionOrder(use.to);
                        ServerPlayer *victim = p->tag["collateralVictim"].value<ServerPlayer *>();
                        if (victim) {
                            LogMessage log;
                            log.type = "#CollateralSlash";
                            log.from = player;
                            log.to << victim;
                            room->sendLog(log);
                            room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, p->objectName(), victim->objectName());
                        }
                    }
                }

                if (!extra) {
                    ServerPlayer *target = ava.at(qrand() % ava.length());
                    QList<ServerPlayer *> victims;
                    foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                        if (target->canSlash(p)
                            && (!(p == use.from && p->hasSkill("kongcheng") && p->isLastHandCard(use.card, true)))) {
                            victims << p;
                        }
                    }
                    Q_ASSERT(!victims.isEmpty());
                    ServerPlayer *victim = victims.at(qrand() % victims.length());
                    target->tag["collateralVictim"] = QVariant::fromValue(victim);
                    LogMessage log;
                    log.type = "#QiaoshuiAdd";
                    log.from = player;
                    log.to << target;
                    log.card_str = use.card->toString();
                    log.arg = "sheyan";
                    room->sendLog(log);
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                    room->notifySkillInvoked(player, objectName());
                    room->broadcastSkillInvoke(objectName());

                    use.to.append(target);
                    room->sortByActionOrder(use.to);

                    LogMessage newlog;
                    newlog.type = "#CollateralSlash";
                    newlog.from = player;
                    newlog.to << victim;
                    room->sendLog(newlog);
                    room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), victim->objectName());
                }
            } else {
                ServerPlayer *target = room->askForPlayerChosen(player, use.to, objectName(), "@sheyan-remove:" + use.card->objectName());
                LogMessage log;
                log.type = "#QiaoshuiRemove";
                log.from = player;
                log.to << target;
                log.card_str = use.card->toString();
                log.arg = "sheyan";
                room->sendLog(log);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName());
                use.to.removeOne(target);
            }
        }
        data = QVariant::fromValue(use);
        return false;
    }
};

class SheyanTargetMod : public TargetModSkill
{
public:
    SheyanTargetMod() : TargetModSkill("#sheyan-target")
    {
        frequency = NotFrequent;
        pattern = ".";
    }

    int getDistanceLimit(const Player *, const Card *card, const Player *) const
    {
        if (card->hasFlag("sheyan_distance") && card->isNDTrick())
            return 1000;
        else
            return 0;
    }
};

FumanCard::FumanCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

bool FumanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && to_select != Self && to_select->getMark("fuman_target-PlayClear") <= 0;
}

void FumanCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->addPlayerMark(effect.to, "fuman_target-PlayClear");
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "fuman", QString());
    room->obtainCard(effect.to, this, reason, true);
    room->addPlayerMark(effect.to, "fuman_" + QString::number(getSubcards().first()) + effect.from->objectName());
}

class FumanVS : public OneCardViewAsSkill
{
public:
    FumanVS() : OneCardViewAsSkill("fuman")
    {
        filter_pattern = "Slash";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->getMark("fuman_target-PlayClear") <= 0)
                return true;
        }
        return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        FumanCard *c = new FumanCard();
        c->addSubcard(originalCard);
        return c;
    }
};

class Fuman : public TriggerSkill
{
public:
    Fuman() : TriggerSkill("fuman")
    {
        events << CardUsed << CardResponded << EventPhaseChanging;
        view_as_skill = new FumanVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            foreach (QString mark, player->getMarkNames()) {
                if (mark.startsWith("fuman_") && player->getMark(mark) > 0)
                    room->setPlayerMark(player, mark ,0);
            }
        } else {
            const Card *card = NULL;
            if (event == CardUsed) {
                card = data.value<CardUseStruct>().card;
            } else {
                CardResponseStruct res = data.value<CardResponseStruct>();
                if (!res.m_isUse) return false;
                card = res.m_card;
            }
            if (card == NULL || card->isKindOf("SkillCard")) return false;

            QList<int> ids;
            if (card->isVirtualCard())
                ids = card->getSubcards();
            else
                ids << card->getEffectiveId();
            if (ids.isEmpty()) return false;

            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (!p->hasSkill(this) || p->isDead()) continue;
                foreach (int id, ids) {
                    if (player->getMark("fuman_" + QString::number(id) + p->objectName()) > 0) {
                        room->setPlayerMark(player, "fuman_" + QString::number(id) + p->objectName(), 0);
                        room->sendCompulsoryTriggerLog(p, objectName(), true, true);
                        p->drawCards(1, objectName());
                    }
                }
            }
        }
        return false;
    }
};

TunanCard::TunanCard()
{
}

void TunanCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    QList<int> ids = room->getNCards(1, false);
    room->returnToTopDrawPile(ids);

    LogMessage log;
    log.type = "$ViewDrawPile";
    log.from = effect.to;
    log.arg = QString::number(1);
    log.card_str = IntList2StringList(ids).join("+");
    room->sendLog(log, effect.to);

    room->fillAG(ids, effect.to);
    room->askForAG(effect.to, ids, true, "tunan");

    QStringList choices;
    QList<ServerPlayer *> players, slash_to;
    const Card *card = Sanguosha->getCard(ids.first());
    card->setFlags("tunan_distance");
    if (effect.to->canUse(card))
        choices << "use";
    card->setFlags("-tunan_distance");

    Slash *slash = new Slash(card->getSuit(), card->getNumber());
    slash->addSubcard(card);
    slash->setSkillName("_tunan");
    slash->deleteLater();

    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (effect.to->canSlash(p, slash, true))
            slash_to << p;
    }
    if (!slash_to.isEmpty())
        choices << "slash";

    if (choices.isEmpty()) {
        room->clearAG(effect.to);
        return;
    }

    QString choice = room->askForChoice(effect.to, "tunan", choices.join("+"), QVariant::fromValue(card));
    room->clearAG(effect.to);
    room->addPlayerMark(effect.to, "tunan_id-PlayClear", ids.first() + 1);

    ServerPlayer *target = NULL;
    if (choice == "use") {
        if (card->targetFixed())
            room->useCard(CardUseStruct(card, effect.to, effect.to), false);
        else {
            if (!room->askForUseCard(effect.to, "@@tunan1!", "@tunan1:" + card->objectName(), 1)) {
                if (card->targetFixed())
                    target = effect.to;
                else
                    target = players.at(qrand() % players.length());

                if (target != NULL) {
                    if (card->isKindOf("Collateral")) {
                        QList<ServerPlayer *> victims;
                        foreach (ServerPlayer *p, room->getOtherPlayers(target)) {
                            if (target->canSlash(p, NULL, true))
                                victims << p;
                        }
                        Q_ASSERT(!victims.isEmpty());
                        target->tag["collateralVictim"] = QVariant::fromValue((victims.at(qrand() % victims.length())));
                    }
                    room->useCard(CardUseStruct(card, effect.to, target), false);
                }
            }
        }
    } else {
        if (!room->askForUseCard(effect.to, "@@tunan2!", "@tunan2", 2)) {
            target = slash_to.at(qrand() % slash_to.length());
            if (target != NULL)
                room->useCard(CardUseStruct(slash, effect.to, target), false);
        }
    }
    if (card->hasFlag("tunan_distance"))
        card->setFlags("-tunan_distance");
    room->setPlayerMark(effect.to, "tunan_id-PlayClear", 0);
}

class Tunan : public ZeroCardViewAsSkill
{
public:
    Tunan() : ZeroCardViewAsSkill("tunan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("TunanCard");
    }

    bool isEnabledAtResponse(const Player *, const QString &pattern) const
    {
        return pattern == "@@tunan1!" || pattern == "@@tunan2!";
    }

    const Card *viewAs() const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@tunan1!") {
            int id = Self->getMark("tunan_id-PlayClear") - 1;
            if (id < 0) return NULL;
            const Card *card = Sanguosha->getEngineCard(id);
            card->setFlags("tunan_distance");
            return card;
        } else if (pattern == "@@tunan2!") {
            int id = Self->getMark("tunan_id-PlayClear") - 1;
            if (id < 0) return NULL;
            const Card *card = Sanguosha->getEngineCard(id);
            Slash *slash = new Slash(card->getSuit(), card->getNumber());
            slash->addSubcard(card);
            slash->setSkillName("_tunan");
            return slash;
        } else
            return new TunanCard;
    }
};

class TunanTargetMod : public TargetModSkill
{
public:
    TunanTargetMod() : TargetModSkill("#tunan-target")
    {
        frequency = NotFrequent;
        pattern = ".";
    }

    int getDistanceLimit(const Player *, const Card *card, const Player *) const
    {
        if (card->hasFlag("tunan_distance"))
            return 1000;
        else
            return 0;
    }
};

class Bijing : public TriggerSkill
{
public:
    Bijing() : TriggerSkill("bijing")
    {
        events << EventPhaseStart << CardsMoveOneTime << EventLoseSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Finish && player->hasSkill(this)) {
                if (player->isKongcheng()) return false;
                const Card *card = room->askForCard(player, ".|.|.|hand", "bijing-invoke", data, Card::MethodNone);
                if (!card) return false;
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);
                room->notifySkillInvoked(player, objectName());
                room->broadcastSkillInvoke(objectName());

                int id = card->getSubcards().first();
                QVariantList bijing = player->tag["BijingIds"].toList();
                if (!bijing.contains(QVariant(id)))
                    bijing << id;
                player->tag["BijingIds"] = bijing;
            } else if (player->getPhase() == Player::Start && player->hasSkill(this)) {
                QVariantList bijing = player->tag["BijingIds"].toList();
                QList<int> ids = VariantList2IntList(bijing);
                player->tag.remove("BijingIds");
                if (ids.isEmpty()) return false;

                DummyCard *dummy = new DummyCard();
                foreach (int id, player->handCards()) {
                    if (ids.contains(id))
                        dummy->addSubcard(id);
                }
                if (dummy->subcardsLength() > 0) {
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                    room->throwCard(dummy, player, NULL);
                }
                delete dummy;
            } else if (player->getPhase() == Player::Discard) {
                int n = player->getMark("bijing_lose-Clear");
                if (n <= 0) return false;
                room->setPlayerMark(player, "bijing_lose-Clear", 0);
                QList<ServerPlayer *> losers;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->hasSkill(this)) {
                        int n = p->getMark("bijing_lose_from-Clear");
                        if (n > 0) {
                            for (int i = 0; i < n; i++) {
                                losers << p;
                            }
                            room->setPlayerMark(p, "bijing_lose_from-Clear", 0);
                        }
                    }
                }
                if (losers.isEmpty()) return false;

                int num = qMin(n, losers.length());
                for (int i = 0; i < num; i++) {
                    if (player->isDead()) return false;
                    foreach (ServerPlayer *p, losers) {
                        if (p->isDead() || !p->hasSkill(this))
                            losers.removeOne(p);
                    }
                    if (losers.isEmpty()) return false;
                    if (player->isNude()) {
                        LogMessage log;
                        log.type = "#BijingKongcheng";
                        log.from = losers.first();
                        log.to << player;
                        log.arg = objectName();
                        room->sendLog(log);
                        room->notifySkillInvoked(losers.first(), objectName());
                        room->broadcastSkillInvoke(objectName());
                        return false;
                    }
                    room->sendCompulsoryTriggerLog(losers.first(), objectName(), true, true);
                    losers.removeFirst();
                    room->askForDiscard(player, objectName(), 2, 2, false, true);
                    if (!player->canDiscard(player, "he")) return false;
                }
          }
        } else if (event == EventLoseSkill) {
            if (data.toString() != objectName()) return false;
            player->tag.remove("BijingIds");
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from == player && player->hasSkill(this) && player->getPhase() == Player::NotActive &&
                    move.from_places.contains(Player::PlaceHand)) {
                ServerPlayer *current = room->getCurrent();
                if (!current || current->isDead()) return false;
                QVariantList bijing = player->tag["BijingIds"].toList();
                QList<int> ids = VariantList2IntList(bijing);
                foreach (int id, move.card_ids) {
                    if (ids.contains(id)) {
                        ids.removeOne(id);
                        room->addPlayerMark(player, "bijing_lose_from-Clear");
                        room->addPlayerMark(current, "bijing_lose-Clear");
                    }
                }
                QVariantList new_bijing = IntList2VariantList(ids);
                player->tag["BijingIds"] = new_bijing;
            }
        }
        return false;
    }
};

class Dianhu : public TriggerSkill
{
public:
    Dianhu() : TriggerSkill("dianhu")
    {
        events << Damage << HpRecover;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Damage) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.to->isDead() || !damage.from->hasSkill(this) || damage.damage <= 0) return false;
            ServerPlayer *target = damage.from->tag["DianhuTarget"].value<ServerPlayer *>();
            if (target && target->isAlive() && target == damage.to) {
                room->sendCompulsoryTriggerLog(damage.from, objectName(), true, true);
                damage.from->drawCards(1, objectName());
            }
        } else {
            RecoverStruct recover = data.value<RecoverStruct>();
            if (recover.recover <= 0) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->isDead()) return false;
                if (!p->hasSkill(this) || p->isDead()) continue;
                ServerPlayer *target = p->tag["DianhuTarget"].value<ServerPlayer *>();
                if (target && target->isAlive() && target == player) {
                    room->sendCompulsoryTriggerLog(p, objectName(), true, true);
                    p->drawCards(1, objectName());
                }
            }
        }
        return false;
    }
};

class DianhuTarget : public GameStartSkill
{
public:
    DianhuTarget() : GameStartSkill("#dianhu-target")
    {
        frequency = Compulsory;
    }

    void onGameStart(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), "dianhu", "@dianhu-choose", false, true);
        room->broadcastSkillInvoke("dianhu");
        player->tag["DianhuTarget"] = QVariant::fromValue(target);
        room->addPlayerMark(target, "&dianhu");
    }
};

JianjiCard::JianjiCard()
{
}

void JianjiCard::onEffect(const CardEffectStruct &effect) const
{
    QList<int> ids = effect.to->drawCardsList(1, "jianji");
    if (ids.isEmpty()) return;
    if (effect.to->isDead()) return;

    Room *room = effect.from->getRoom();
    int id = ids.first();
    if (room->getCardPlace(id) != Player::PlaceHand || room->getCardOwner(id) != effect.to) return;

    const Card *card = Sanguosha->getCard(id);
    if (!effect.to->canUse(card)) return;

    room->addPlayerMark(effect.to, "jianji_id-PlayClear", id + 1);
    room->askForUseCard(effect.to, "@@jianji", "@jianji:" + card->objectName());
    room->setPlayerMark(effect.to, "jianji_id-PlayClear", 0);
}

class Jianji : public ZeroCardViewAsSkill
{
public:
    Jianji() : ZeroCardViewAsSkill("jianji")
    {
        response_pattern = "@@jianji";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("JianjiCard");
    }

    const Card *viewAs() const
    {
        QString pattern = Sanguosha->currentRoomState()->getCurrentCardUsePattern();
        if (pattern == "@@jianji") {
            int id = Self->getMark("jianji_id-PlayClear") - 1;
            if (id < 0) return NULL;
            const Card *card = Sanguosha->getEngineCard(id);
            return card;
        }
        return new JianjiCard;
    }
};

class Jili : public TriggerSkill
{
public:
    Jili() : TriggerSkill("jili")
    {
        events << CardUsed << CardResponded;
        global = true;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = NULL;
        if (event == CardUsed)
            card = data.value<CardUseStruct>().card;
        else
            card = data.value<CardResponseStruct>().m_card;
        if (card == NULL || card->isKindOf("SkillCard")) return false;

        room->addPlayerMark(player, "jili-Clear");
        int attackrange = player->getAttackRange();
        if (player->getMark("jili-Clear") == attackrange && player->hasSkill(this) && player->askForSkillInvoke(this, data)) {
            room->broadcastSkillInvoke(objectName());
            player->drawCards(attackrange, objectName());
        }
        return false;
    }
};

YizanCard::YizanCard()
{
    target_fixed = true;
    will_throw = false;
    mute = true;
    handling_method = Card::MethodNone;
}

bool YizanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = NULL;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return false;
    }

    const Card *card = Self->tag.value("yizan").value<const Card *>();
    return card && card->targetFilter(targets, to_select, Self) && !Self->isProhibited(to_select, card, targets);
}

bool YizanCard::targetFixed() const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = NULL;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetFixed();
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *card = Self->tag.value("yizan").value<const Card *>();
    return card && card->targetFixed();
}

bool YizanCard::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        const Card *card = NULL;
        if (!user_string.isEmpty())
            card = Sanguosha->cloneCard(user_string.split("+").first());
        return card && card->targetsFeasible(targets, Self);
    } else if (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
        return true;
    }

    const Card *card = Self->tag.value("yizan").value<const Card *>();
    return card && card->targetsFeasible(targets, Self);
}

const Card *YizanCard::validate(CardUseStruct &card_use) const
{
    ServerPlayer *player = card_use.from;
    Room *room = player->getRoom();

    QString to_yizan = user_string;
    if (user_string == "slash" && Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Config.BanPackages.contains("maneuvering"))
            guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
        to_yizan = room->askForChoice(player, "yizan_slash", guhuo_list.join("+"));
    }

    const Card *card = Sanguosha->getCard(subcards.first());
    QString user_str;
    if (to_yizan == "slash") {
        if (card->isKindOf("Slash"))
            user_str = card->objectName();
        else
            user_str = "slash";
    } else if (to_yizan == "normal_slash")
        user_str = "slash";
    else
        user_str = to_yizan;
    Card *use_card = Sanguosha->cloneCard(user_str, card->getSuit(), card->getNumber());
    use_card->setSkillName("yizan");
    use_card->addSubcards(getSubcards());
    use_card->deleteLater();
    return use_card;
}

const Card *YizanCard::validateInResponse(ServerPlayer *player) const
{
    Room *room = player->getRoom();

    QString to_yizan;
    if (user_string == "peach+analeptic") {
        QStringList guhuo_list;
        guhuo_list << "peach";
        if (!Config.BanPackages.contains("maneuvering"))
            guhuo_list << "analeptic";
        to_yizan = room->askForChoice(player, "yizan_saveself", guhuo_list.join("+"));
    } else if (user_string == "slash") {
        QStringList guhuo_list;
        guhuo_list << "slash";
        if (!Config.BanPackages.contains("maneuvering"))
            guhuo_list << "normal_slash" << "thunder_slash" << "fire_slash";
        to_yizan = room->askForChoice(player, "yizan_slash", guhuo_list.join("+"));
    } else
        to_yizan = user_string;

    const Card *card = Sanguosha->getCard(subcards.first());
    QString user_str;
    if (to_yizan == "slash") {
        if (card->isKindOf("Slash"))
            user_str = card->objectName();
        else
            user_str = "slash";
    } else if (to_yizan == "normal_slash")
        user_str = "slash";
    else
        user_str = to_yizan;
    Card *use_card = Sanguosha->cloneCard(user_str, card->getSuit(), card->getNumber());
    use_card->setSkillName("yizan");
    use_card->addSubcards(getSubcards());
    use_card->deleteLater();
    return use_card;
}

class YizanVS : public ViewAsSkill
{
public:
    YizanVS() : ViewAsSkill("yizan")
    {
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return true;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        if (pattern.startsWith(".") || pattern.startsWith("@")) return false;
        if (pattern == "peach" && player->getMark("Global_PreventPeach") > 0) return false;
        for (int i = 0; i < pattern.length(); i++) {
            QChar ch = pattern[i];
            if (ch.isUpper() || ch.isDigit()) return false; // This is an extremely dirty hack!! For we need to prevent patterns like 'BasicCard'
        }
        return !(pattern == "nullification");
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE) {
            if (Self->isCardLimited(to_select, Card::MethodResponse))
                return false;
        } else {
            if (Self->isLocked(to_select))
                return false;
        }
        int level = Self->property("yizan_level").toInt();
        if (level <= 0) {
            if (selected.length() >= 2) return false;
            if (selected.isEmpty()) return true;
            if (selected.first()->isKindOf("BasicCard"))
                return true;
            else
                return to_select->isKindOf("BasicCard");
        } else if (level >= 1) {
            return selected.isEmpty() && to_select->isKindOf("BasicCard");
        }
        return false;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        int level = Self->property("yizan_level").toInt();
        int n = 2;
        if (level >= 1)
            n = 1;
        if (cards.length() != n) return NULL;
        if (Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE
            || Sanguosha->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE) {
            YizanCard *card = new YizanCard;
            card->setUserString(Sanguosha->getCurrentCardUsePattern());
            card->addSubcards(cards);
            return card;
        }

        const Card *c = Self->tag.value("yizan").value<const Card *>();
        if (c && c->isAvailable(Self)) {
            YizanCard *card = new YizanCard;
            card->setUserString(c->objectName());
            card->addSubcards(cards);
            return card;
        } else
            return NULL;
    }
};

class Yizan : public TriggerSkill
{
public:
    Yizan() : TriggerSkill("yizan")
    {
        events << PreCardResponded << PreCardUsed;
        view_as_skill = new YizanVS;
    }

    QDialog *getDialog() const
    {
        return GuhuoDialog::getInstance("yizan", true, false);
    }

    int getPriority() {
        return 5;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        const Card *card = NULL;
        if (event == PreCardResponded)
            card = data.value<CardResponseStruct>().m_card;
        else
            card = data.value<CardUseStruct>().card;
        if (card == NULL || card->isKindOf("SkillCard") || card->getSkillName() != "yizan") return false;
        room->addPlayerMark(player, "&yizan");
        return false;
    }
};

class Longyuan : public PhaseChangeSkill
{
public:
    Longyuan() : PhaseChangeSkill("longyuan")
    {
        frequency = Wake;
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Start) return false;
        if (player->getMark(objectName()) > 0 || player->getMark("&yizan") < 3) return false;
        Room *room = player->getRoom();
        LogMessage log;
        log.type = "#LongyuanWake";
        log.from = player;
        log.arg = QString::number(player->getMark("&yizan"));
        log.arg2 = objectName();
        room->sendLog(log);
        room->broadcastSkillInvoke(objectName());
        room->notifySkillInvoked(player, objectName());
        room->doSuperLightbox("zhaotongzhaoguang", "longyuan");
        room->setPlayerMark(player, "longyuan", 1);
        room->setPlayerProperty(player, "yizan_level", 1);
        if (room->changeMaxHpForAwakenSkill(player, 0)) {
            QString translate = Sanguosha->translate(":yizan2");
            room->changeTranslation(player, "yizan", translate);
        }
        return false;
    }
};

class Renshi : public TriggerSkill
{
public:
    Renshi() : TriggerSkill("renshi")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.card || !damage.card->isKindOf("Slash") || damage.damage <= 0) return false;
        if (!player->isWounded()) return false;
        LogMessage log;
        log.type = "#RenshiPrevent";
        log.from = player;
        log.to << damage.from;
        log.arg = objectName();
        log.arg2 = QString::number(damage.damage);
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        room->broadcastSkillInvoke(objectName());
        if (room->CardInTable(damage.card))
            player->obtainCard(damage.card, true);
        room->loseMaxHp(player);
        return true;
    }
};

class Huaizi : public MaxCardsSkill
{
public:
    Huaizi() : MaxCardsSkill("huaizi")
    {
    }

    int getFixed(const Player *target) const
    {
        if (target->hasSkill("huaizi"))
            return target->getMaxHp();
        else
            return -1;
    }
};

WuyuanCard::WuyuanCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void WuyuanCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "wuyuan", QString());
    room->obtainCard(effect.to, this, reason, true);

    const Card *card = Sanguosha->getCard(getSubcards().first());
    bool red = card->isRed() ? true : false;
    bool nature = (card->isKindOf("Slash") && card->objectName() != "slash") ? true : false;

    room->recover(effect.from, RecoverStruct(effect.from));
    int n = 1;
    if (nature)
        n = 2;
    effect.to->drawCards(n, "wuyuan");
    if (red)
        room->recover(effect.to, RecoverStruct(effect.from));
}

class Wuyuan : public OneCardViewAsSkill
{
public:
    Wuyuan() : OneCardViewAsSkill("wuyuan")
    {
        filter_pattern = "Slash";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("WuyuanCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        WuyuanCard *card = new WuyuanCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Yuxu : public TriggerSkill
{
public:
    Yuxu() : TriggerSkill("yuxu")
    {
        events << PreCardUsed << PreCardResponded << CardFinished;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play) return false;
        if (event == CardFinished) {
            if (!player->hasSkill(this)) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            if (use.card->hasFlag("yuxu_ji")) {
                room->setCardFlag(use.card, "-yuxu_ji");
                if (!player->askForSkillInvoke(this, data)) return false;
                room->broadcastSkillInvoke(objectName());
                player->drawCards(1, objectName());
            } else if (use.card->hasFlag("yuxu_ou")) {
                room->setCardFlag(use.card, "-yuxu_ou");
                if (player->isNude()) return false;
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                room->askForDiscard(player, objectName(), 1, 1, false, true);
            }
        } else {
            const Card *card = NULL;
            if (event == PreCardUsed)
                card = data.value<CardUseStruct>().card;
            else {
                CardResponseStruct resp = data.value<CardResponseStruct>();
                if (!resp.m_isUse) return false;
                card = data.value<CardResponseStruct>().m_card;
            }
            if (card == NULL || card->isKindOf("SkillCard")) return false;
            room->addPlayerMark(player, "yuxu_jiou-PlayClear");
            int n = player->getMark("yuxu_jiou-PlayClear") % 2;
            if (n == 0)
                room->setCardFlag(card, "yuxu_ou");
            else
                room->setCardFlag(card, "yuxu_ji");
        }
        return false;
    }
};

class Shijian : public TriggerSkill
{
public:
    Shijian() : TriggerSkill("shijian")
    {
        events << PreCardUsed << PreCardResponded << CardFinished;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play) return false;
        if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            if (use.card->hasFlag("shijian_second")) {
                room->setCardFlag(use.card, "-shijian_second");
                foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
                    if (p->isAlive() && p->hasSkill(this) && p != player) {
                        if (room->askForCard(p, "..", "@shijian-discard:" + player->objectName(), QVariant::fromValue(player), objectName())) {
                            room->broadcastSkillInvoke(objectName());
                            room->acquireOneTurnSkills(player, QString(), "yuxu");
                        }
                    }
                }
            }
        } else {
            const Card *card = NULL;
            if (event == PreCardUsed)
                card = data.value<CardUseStruct>().card;
            else {
                CardResponseStruct resp = data.value<CardResponseStruct>();
                if (!resp.m_isUse) return false;
                card = data.value<CardResponseStruct>().m_card;
            }
            if (card == NULL || card->isKindOf("SkillCard")) return false;
            room->addPlayerMark(player, "shijian-PlayClear");
            int n = player->getMark("shijian-PlayClear");
            if (n == 2)
                room->setCardFlag(card, "shijian_second");
        }
        return false;
    }
};

class SpManyi : public TriggerSkill
{
public:
    SpManyi() : TriggerSkill("spmanyi")
    {
        events << CardEffected;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.card->isKindOf("SavageAssault")) {
            room->broadcastSkillInvoke(objectName());
            room->notifySkillInvoked(player, objectName());
            LogMessage log;
            log.type = "#SkillNullify";
            log.from = player;
            log.arg = objectName();
            log.arg2 = "savage_assault";
            room->sendLog(log);
            return true;
        }
        return false;
    }
};

class Mansi : public TriggerSkill
{
public:
    Mansi() : TriggerSkill("mansi")
    {
        events << CardFinished << PreDamageDone;
        frequency = Frequent;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == CardFinished) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("SavageAssault")) return false;
            //QStringList names = use.card->tag["MansiDamage"].toStringList();  AI无法获取到card的tag
            //use.card->removeTag("MansiDamage");
            QStringList names = room->getTag("MansiDamage" + use.card->toString()).toStringList();
            room->removeTag("MansiDamage" + use.card->toString());
            if (names.isEmpty()) return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || !p->hasSkill(this)) continue;
                if (!p->askForSkillInvoke(objectName())) continue;
                room->broadcastSkillInvoke(objectName());
                p->drawCards(names.length(), objectName());
            }
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("SavageAssault")) return false;
            //QStringList names = damage.card->tag["MansiDamage"].toStringList();
            QStringList names = room->getTag("MansiDamage" + damage.card->toString()).toStringList();
            if (names.contains(damage.to->objectName())) return false;
            names << damage.to->objectName();
            //damage.card->tag["MansiDamage"] = names;
            room->setTag("MansiDamage" + damage.card->toString(), names);
        }
        return false;
    }
};

class Souying : public TriggerSkill
{
public:
    Souying() : TriggerSkill("souying")
    {
        events << DamageCaused;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        ServerPlayer *from = damage.from;
        ServerPlayer *to = damage.to;
        if (!from || !to || from->isDead() || to->isDead()) return false;
        room->addPlayerMark(from, "souying_damage_ " + to->objectName() + "-Clear");
        QList<ServerPlayer *> players;
        if (from->hasSkill(this) && to->isMale() && from->getMark("souying_damage_ " + to->objectName() + "-Clear") == 2)
            players << from;
        if (from->isMale() && to->hasSkill(this) && from->getMark("souying_damage_ " + to->objectName() + "-Clear") == 2 && !players.contains(to))
            players << to;
        if (players.isEmpty()) return false;
        room->sortByActionOrder(players);

        foreach (ServerPlayer *p, players) {
            if (p->isDead() || !p->hasSkill(this)) continue;
            if (!p->canDiscard(p, "h") || p->getMark("souying_used-Clear") > 0) continue;
            if (!room->askForCard(p, ".", "souying-invoke:" + to->objectName(), data, objectName())) continue;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(p, "souying_used-Clear");
            if (p == from) {
                LogMessage log;
                log.type = "#SouyingAdd";
                log.from = from;
                log.to << to;
                log.arg = QString::number(damage.damage);
                log.arg2 = QString::number(++damage.damage);
                room->sendLog(log);
            } else if (p == to) {
                LogMessage log;
                log.type = (damage.damage > 1) ? "#SouyingReduce" : "#SouyingPrevent";
                log.from = from;
                log.to << to;
                log.arg = QString::number(damage.damage);
                log.arg2 = QString::number(--damage.damage);
                room->sendLog(log);
                if (damage.damage <= 0)
                    return true;
            }
        }
        data = QVariant::fromValue(damage);
        return false;
    }
};

class Zhanyuan : public TriggerSkill
{
public:
    Zhanyuan() : TriggerSkill("zhanyuan")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        frequency = Wake;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to && move.to->isAlive() && move.to == player && move.to_place == Player::PlaceHand && move.reason.m_skillName == "mansi") {
                if (player->hasSkill(this, true))
                    room->addPlayerMark(player, "&zhanyuan_num", move.card_ids.length());
                else
                    room->addPlayerMark(player, "zhanyuan_num", move.card_ids.length());
            }
        } else {
            if (!player->hasSkill(this) || player->getPhase() != Player::Start) return false;
            int mark = player->getMark("&zhanyuan_num") + player->getMark("zhanyuan_num");
            if (player->getMark(objectName()) > 0 || mark <= 7) return false;
            LogMessage log;
            log.type = "#ZhanyuanWake";
            log.from = player;
            log.arg = objectName();
            log.arg2 = QString::number(mark);
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName());
            room->doSuperLightbox("huaman", objectName());
            room->addPlayerMark(player, objectName());
            if (room->changeMaxHpForAwakenSkill(player, 1)) {
                QList<ServerPlayer *> males;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->isMale())
                        males << p;
                }
                if (males.isEmpty()) return false;
                ServerPlayer *male = room->askForPlayerChosen(player, males, objectName(), "@zhanyuan-invoke", true);
                if (!male) return false;
                room->doAnimate(1, player->objectName(), male->objectName());
                QList<ServerPlayer *> players;
                players << player;
                if (!players.contains(male))
                    players << male;
                if (players.isEmpty()) return false;
                room->sortByActionOrder(players);
                foreach (ServerPlayer *p, players) {
                    if (p->hasSkill("xili", true)) continue;
                    room->handleAcquireDetachSkills(p, "xili");
                }
                if (player->hasSkill("mansi", true))
                    room->handleAcquireDetachSkills(player, "-mansi");
            }
        }
        return false;
    }
};

class Xili : public TriggerSkill
{
public:
    Xili() : TriggerSkill("xili")
    {
        events << TargetSpecified << DamageCaused << CardFinished;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (event == TargetSpecified) {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.from->isDead() || !use.from->hasSkill(this, true) || use.from->getPhase() == Player::NotActive || !use.card->isKindOf("Slash")) return false;
            foreach (ServerPlayer *p, room->getOtherPlayers(use.from)) {
                if (p->isDead() || !p->hasSkill(this, true) || !p->canDiscard(p, "h") || p->getPhase() != Player::NotActive) continue;
                if (!room->askForCard(p, ".", "xili-invoke", data, objectName())) continue;
                room->broadcastSkillInvoke(objectName());
                //int n = use.card->tag["XiliDamage"].toInt();
                //use.card->tag["XiliDamage"] = n + 1;
                int n = room->getTag("XiliDamage" + use.card->toString()).toInt();
                room->setTag("XiliDamage" + use.card->toString(), n + 1);
            }
        } else if (event == CardFinished) {
              CardUseStruct use = data.value<CardUseStruct>();
              if (use.card->isKindOf("SkillCard")) return false;
              //use.card->removeTag("XiliDamage");
              room->removeTag("XiliDamage" + use.card->toString());
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (!damage.card || !damage.card->isKindOf("Slash") || damage.to->isDead()) return false;
            //int n = damage.card->tag["XiliDamage"].toInt(); //ai操作时无法获取这个tag
            int n = room->getTag("XiliDamage" + damage.card->toString()).toInt();
            if (n <= 0) return false;
            damage.damage += n;
            data = QVariant::fromValue(damage);
        }
        return false;
    }
};

SecondMansiCard::SecondMansiCard()
{
    mute = true;
    target_fixed = true;
}

void SecondMansiCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    if (card_use.from->isKongcheng()) return;
    SavageAssault *sa = new SavageAssault(Card::SuitToBeDecided, 0);
    foreach (const Card *c, card_use.from->getHandcards())
        sa->addSubcard(c);
    sa->setSkillName("secondmansi");
    sa->deleteLater();
    if (!sa->isAvailable(card_use.from)) return;
    room->useCard(CardUseStruct(sa, card_use.from, QList<ServerPlayer *>()), true);
}

class SecondMansiVS : public ZeroCardViewAsSkill
{
public:
    SecondMansiVS() : ZeroCardViewAsSkill("secondmansi")
    {
    }

    const Card *viewAs() const
    {
        if (Self->isKongcheng()) return NULL;
        SavageAssault *sa = new SavageAssault(Card::SuitToBeDecided, 0);
        foreach (const Card *c, Self->getHandcards())
            sa->addSubcard(c);
        sa->setSkillName("secondmansi");
        sa->deleteLater();
        if (!sa->isAvailable(Self)) return NULL;
        //return sa;
        return new SecondMansiCard;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        SavageAssault *sa = new SavageAssault(Card::SuitToBeDecided, 0);
        foreach (const Card *c, player->getHandcards())
            sa->addSubcard(c);
        sa->setSkillName("secondmansi");
        sa->deleteLater();
        if (!sa->isAvailable(player)) return false;
        return !player->isKongcheng() && !player->hasUsed("SecondMansiCard");
    }
};

class SecondMansi : public MasochismSkill
{
public:
    SecondMansi() : MasochismSkill("secondmansi")
    {
        view_as_skill = new SecondMansiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &damage) const
    {
        if (!damage.card || !damage.card->isKindOf("SavageAssault")) return;
        Room *room = player->getRoom();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(this)) continue;
            room->sendCompulsoryTriggerLog(p, objectName(), true, true);
            p->drawCards(1, objectName());
        }
    }
};

class SecondSouying : public TriggerSkill
{
public:
    SecondSouying() : TriggerSkill("secondsouying")
    {
        events << TargetConfirmed;
    }

    bool triggerable(const ServerPlayer *target, Room *room) const
    {
        return target != NULL && room->hasCurrent();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard")) return false;
        if (use.from != player) return false;
        if (use.to.length() != 1 || use.from->isDead()) return false;

        ServerPlayer *first = use.to.first();
        if (first == use.from || first->getMark("secondsouying_num_" + use.from->objectName() + first->objectName() + "-Clear") == 1) return false;

        QList<ServerPlayer *> huamans;
        if (use.from->hasSkill(this))
            huamans << use.from;
        if (first->hasSkill(this))
            huamans << first;
        if (huamans.isEmpty()) return false;
        room->sortByActionOrder(huamans);

        foreach (ServerPlayer *p, huamans) {
            if (p->isDead() || !p->hasSkill(this)) continue;
            if (p->getMark("secondsouying-Clear") > 0 || !p->canDiscard(p, "he")) return false;

            QString prompt = "@secondsouying-dis:" + use.card->objectName();
            if (p == first)
                prompt = "@secondsouying-dis2:" + use.card->objectName();

            if (!room->askForCard(p, "..", prompt, data, objectName())) continue;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(p, "secondsouying-Clear");

            if (p == use.from) {
                if (!room->CardInTable(use.card)) return false;
                room->obtainCard(use.from, use.card, true);
            } else {
                use.nullified_list << p->objectName();
                data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

class SecondSouyingRecord : public TriggerSkill
{
public:
    SecondSouyingRecord() : TriggerSkill("#secondsouying-record")
    {
        events << PreCardUsed;
        global = true;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard")) return false;
        foreach (ServerPlayer *p, use.to)
            room->addPlayerMark(p, "secondsouying_num_" + use.from->objectName() + p->objectName() + "-Clear");
        return false;
    }
};

class SecondZhanyuan : public TriggerSkill
{
public:
    SecondZhanyuan() : TriggerSkill("secondzhanyuan")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        frequency = Wake;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to && move.to->isAlive() && move.to == player && move.to_place == Player::PlaceHand && move.reason.m_skillName == "secondmansi") {
                if (player->hasSkill(this, true))
                    room->addPlayerMark(player, "&secondzhanyuan_num", move.card_ids.length());
                else
                    room->addPlayerMark(player, "secondzhanyuan_num", move.card_ids.length());
            }
        } else {
            if (!player->hasSkill(this) || player->getPhase() != Player::Start) return false;
            int mark = player->getMark("&secondzhanyuan_num") + player->getMark("secondzhanyuan_num");
            if (player->getMark(objectName()) > 0 || mark <= 7) return false;
            LogMessage log;
            log.type = "#ZhanyuanWake";
            log.from = player;
            log.arg = objectName();
            log.arg2 = QString::number(mark);
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName());
            room->doSuperLightbox("second_huaman", objectName());
            room->addPlayerMark(player, objectName());
            if (room->changeMaxHpForAwakenSkill(player, 1)) {
                QList<ServerPlayer *> males, geters;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->isMale())
                        males << p;
                }
                if (!males.isEmpty()) {
                    ServerPlayer *male = room->askForPlayerChosen(player, males, objectName(), "@secondzhanyuan-invoke", true);
                    if (male) {
                        room->doAnimate(1, player->objectName(), male->objectName());
                        geters << male;
                    }
                }
                if (!geters.contains(player))
                    geters << player;
                if (geters.isEmpty()) return false;
                room->sortByActionOrder(geters);

                foreach (ServerPlayer *p, geters) {
                    if (p->hasSkill("secondxili", true)) continue;
                    room->handleAcquireDetachSkills(p, "secondxili");
                }
                if (player->hasSkill("secondmansi", true))
                    room->handleAcquireDetachSkills(player, "-secondmansi");
            }
        }
        return false;
    }
};

class SecondXili : public TriggerSkill
{
public:
    SecondXili() : TriggerSkill("secondxili")
    {
        events << DamageCaused;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive() && target->getPhase() != Player::NotActive && target->hasSkill(this, true);
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to->hasSkill(this, true)) return false;

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p == damage.from || p->getMark("secondxili-Clear") > 0) continue;
            if (!damage.from || damage.from->isDead() || !damage.from->hasSkill(this, true)) return false;
            if (damage.to->isDead() || damage.to->hasSkill(this, true)) return false;

            if (p->isDead() || !p->hasSkill(this) || p->getPhase() != Player::NotActive) continue;
            if (!p->canDiscard(p, "he")) continue;
            if (!room->askForCard(p, "..", "@secondxili-dis:" + damage.to->objectName(), data, objectName())) continue;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(p, "secondxili-Clear");

            LogMessage log;
            log.type = "#SecondxiliDamage";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            data = QVariant::fromValue(damage);

            QList<ServerPlayer *> drawers;
            drawers << p << damage.from;
            room->sortByActionOrder(drawers);
            room->drawCards(drawers, 2, objectName());
        }
        return false;
    }
};

class Hongde : public TriggerSkill
{
public:
    Hongde() : TriggerSkill("hongde")
    {
        events << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        int n = 0;
        if (!room->getTag("FirstRound").toBool() && move.to && move.to == player && move.to_place == Player::PlaceHand) {
            if (move.card_ids.length() > 1)
                n++;
        }
        if (move.from && move.from == player && (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))) {
            int lose = 0;
            for (int i = 0; i < move.card_ids.length(); i++) {
                if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip) {
                    lose++;
                    if (lose > 1)
                        break;
                }
            }
            if (lose > 1)
                n++;
        }

        if (n <= 0) return false;
        for (int i = 0; i < n; i++) {
            if (player->isDead() || !player->hasSkill(this)) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@hongde-invoke", true, true);
            if (!target) break;
            room->broadcastSkillInvoke(objectName());
            target->drawCards(1, objectName());
        }
        return false;
    }
};

DingpanCard::DingpanCard()
{
}

bool DingpanCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *) const
{
    return targets.isEmpty() && !to_select->getEquips().isEmpty();
}

void DingpanCard::onEffect(const CardEffectStruct &effect) const
{
    effect.to->drawCards(1, "dingpan");
    if (effect.to->getEquips().isEmpty() || effect.from->isDead()) return;

    Room *room = effect.from->getRoom();
    QStringList choices;
    if (effect.from->canDiscard(effect.to, "e"))
        choices << "discard";
    choices << "get";

    QString choice = room->askForChoice(effect.to, "dingpan", choices.join("+"), QVariant::fromValue(effect.from));
    if (choice == "discard") {
        if (!effect.from->canDiscard(effect.to, "e")) return;
        int id = room->askForCardChosen(effect.from, effect.to, "e", "dingpan", false, Card::MethodDiscard);
        room->throwCard(id, effect.to, effect.from);
    } else {
        DummyCard *dummy = new DummyCard;
        dummy->addSubcards(effect.to->getEquips());
        room->obtainCard(effect.to, dummy);
        delete dummy;
        room->damage(DamageStruct("dingpan", effect.from, effect.to));
    }
}

class DingpanVS : public ZeroCardViewAsSkill
{
public:
    DingpanVS() : ZeroCardViewAsSkill("dingpan")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        /*int n = 0;
        QList<const Player *> as = player->getAliveSiblings();
        as << player;
        foreach (const Player *p, as) {
            if (p->getRole() == "rebel")
                n++;
        }*/
        int n = player->getMark("dingpan-PlayClear");
        return player->usedTimes("DingpanCard") < n;
    }

    const Card *viewAs() const
    {
        return new DingpanCard;
    }
};

class Dingpan : public TriggerSkill
{
public:
    Dingpan() : TriggerSkill("dingpan")
    {
        events << EventPhaseStart << CardFinished << EventAcquireSkill << Death;
        view_as_skill = new DingpanVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Play) return false;
        int n = 0;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getRole() == "rebel")
                n++;
        }
        room->setPlayerMark(player, "dingpan-PlayClear", n);
        return false;
    }
};

class DingpanRevived : public TriggerSkill
{
public:
    DingpanRevived() : TriggerSkill("#dingpan-revived")
    {
        events << Revived;
        view_as_skill = new DingpanVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &) const
    {
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->isDead() || !p->hasSkill("dingpan") || p->getPhase() != Player::Play) return false;
            int n = 0;
            foreach (ServerPlayer *q, room->getAlivePlayers()) {
                if (q->getRole() == "rebel")
                    n++;
            }
            room->setPlayerMark(p, "dingpan-PlayClear", n);
        }
        return false;
    }
};

class Qizhou : public TriggerSkill
{
public:
    Qizhou() : TriggerSkill("qizhou")
    {
        events << CardsMoveOneTime << EventAcquireSkill;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        bool flag = false;
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from == player && move.from_places.contains(Player::PlaceEquip))
                flag = true;
            if (move.to && move.to == player && move.to_place == Player::PlaceEquip)
                flag = true;
        } else {
            if (data.toString() == objectName())
                flag = true;
        }

        if (flag == true) {
            QStringList skills = player->property("qizhou_skills").toStringList();
            QStringList get_or_lose;
            if (QizhouNum(player) >= 1 && !player->hasSkill("mashu") && !skills.contains("mashu")) {
                skills << "mashu";
                room->setPlayerProperty(player, "qizhou_skills", skills);
                get_or_lose << "mashu";
            }
            if (QizhouNum(player) < 1 && player->hasSkill("mashu") && skills.contains("mashu")) {
                skills.removeOne("mashu");
                room->setPlayerProperty(player, "qizhou_skills", skills);
                get_or_lose << "-mashu";
            }

            if (QizhouNum(player) >= 2 && !player->hasSkill("yingzi") && !skills.contains("yingzi")) {
                skills << "yingzi";
                room->setPlayerProperty(player, "qizhou_skills", skills);
                get_or_lose << "yingzi";
            }
            if (QizhouNum(player) < 2 && player->hasSkill("yingzi") && skills.contains("yingzi")) {
                skills.removeOne("yingzi");
                room->setPlayerProperty(player, "qizhou_skills", skills);
                get_or_lose << "-yingzi";
            }

            if (QizhouNum(player) >= 3 && !player->hasSkill("duanbing") && !skills.contains("duanbing")) {
                skills << "duanbing";
                room->setPlayerProperty(player, "qizhou_skills", skills);
                get_or_lose << "duanbing";
            }
            if (QizhouNum(player) < 3 && player->hasSkill("duanbing") && skills.contains("duanbing")) {
                skills.removeOne("duanbing");
                room->setPlayerProperty(player, "qizhou_skills", skills);
                get_or_lose << "-duanbing";
            }

            if (QizhouNum(player) >= 4 && !player->hasSkill("fenwei") && !skills.contains("fenwei")) {
                skills << "fenwei";
                room->setPlayerProperty(player, "qizhou_skills", skills);
                int n = player->property("qizhou_fenwei_got").toInt();
                room->setPlayerProperty(player, "qizhou_fenwei_got", n + 1);
                get_or_lose << "fenwei";
            }
            if (QizhouNum(player) < 4 && player->hasSkill("fenwei") && skills.contains("fenwei")) {
                skills.removeOne("fenwei");
                room->setPlayerProperty(player, "qizhou_skills", skills);
                get_or_lose << "-fenwei";
            }

            if (!get_or_lose.isEmpty()) {
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                bool flag = true;
                if (player->property("qizhou_fenwei_got").toInt() > 1)
                    flag = false;
                room->handleAcquireDetachSkills(player, get_or_lose, false, flag);
            }
        }
        return false;
    }
private:
    int QizhouNum(ServerPlayer *player) const
    {
        QList<const Card *>equips = player->getEquips();
        QStringList suits;
        foreach (const Card *c, equips) {
            if (!suits.contains(c->getSuitString()))
                suits << c->getSuitString();
        }
        return suits.length();
    }
};

class QizhouLose : public TriggerSkill
{
public:
    QizhouLose() : TriggerSkill("#qizhou-lose")
    {
        events << EventLoseSkill;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (data.toString() != "qizhou") return false;
        QStringList skills = player->property("qizhou_skills").toStringList();
        room->setPlayerProperty(player, "qizhou_skills", QStringList());
        if (skills.isEmpty()) return false;
        QStringList new_list;
        foreach (QString str, skills) {
            if (player->hasSkill(str))
                new_list << "-" + str;
        }
        if (new_list.isEmpty()) return false;
        room->handleAcquireDetachSkills(player, new_list);
        return false;
    }
};

ShanxiCard::ShanxiCard()
{
    target_fixed = true;
}

void ShanxiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    if (source->isDead()) return;
    QList<ServerPlayer *> players;
    foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
        if (source->inMyAttackRange(p) && source->canDiscard(p, "he"))
            players << p;
    }
    if (players.isEmpty()) return;

    ServerPlayer *target = room->askForPlayerChosen(source, players, "shanxi", "@shanxi-choose");
    room->doAnimate(1, source->objectName(), target->objectName());
    int card_id = room->askForCardChosen(source, target, "he", "shanxi", false, Card::MethodDiscard);
    room->throwCard(card_id, target, source);

    ServerPlayer *watcher = NULL;
    ServerPlayer *watched = NULL;
    if (Sanguosha->getCard(card_id)->isKindOf("Jink")) {
        watcher = source;
        watched = target;
    } else {
        watcher = target;
        watched = source;
    }
    if (!watcher || !watched || watcher->isDead() || watched->isDead()) return;
    if (watched->isKongcheng()) return;
    room->doGongxin(watcher, watched, QList<int>(), "shanxi");
}

class Shanxi : public OneCardViewAsSkill
{
public:
    Shanxi() : OneCardViewAsSkill("shanxi")
    {
        filter_pattern = "BasicCard|red";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("ShanxiCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        ShanxiCard *card = new ShanxiCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Xiashu : public PhaseChangeSkill
{
public:
    Xiashu() : PhaseChangeSkill("xiashu")
    {
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Play || player->isKongcheng()) return false;
        Room *room = player->getRoom();
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@xiashu-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());

        DummyCard *handcards = player->wholeHandCards();
        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "xiashu", QString());
        room->obtainCard(target, handcards, reason, false);
        delete handcards;

        if (target->isKongcheng()) return false;
        int hand = target->getHandcardNum();
        const Card *show = room->askForExchange(target, objectName(), hand, 1, false, "xiashu-show");
        QList<int> ids = show->getSubcards();
        LogMessage log;
        log.type = "$ShowCard";
        log.from = target;
        log.card_str = IntList2StringList(ids).join("+");
        room->sendLog(log);
        room->fillAG(ids);
        room->getThread()->delay();

        QStringList choices;
        choices << "getshow";
        if (ids.length() != hand)
            choices << "getnotshow";
        QString choice = room->askForChoice(player, objectName(), choices.join("+"), IntList2VariantList(ids));
        room->clearAG();
        if (choice == "getshow") {
            DummyCard *dummy = new DummyCard(ids);
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, dummy, reason, false);
            delete dummy;
        } else {
            QList<int> get;
            foreach (int id, target->handCards()) {
                if (!ids.contains(id))
                    get << id;
            }
            if (get.isEmpty()) return false;
            DummyCard *dummy = new DummyCard(get);
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, dummy, reason, false);
            delete dummy;
        }
        return false;
    }
};

class Kuanshi : public TriggerSkill
{
public:
    Kuanshi() : TriggerSkill("kuanshi")
    {
        events << EventPhaseStart << EventPhaseChanging;
    }

    int getPriority(TriggerEvent event) const
    {
        if (event == EventPhaseChanging)
            return 6;
        else
            return TriggerSkill::getPriority(event);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Finish) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@kuanshi-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            QStringList names = player->property("kuanshi_targets").toStringList();
            if (names.contains(target->objectName())) return false;
            names << target->objectName();
            room->setPlayerProperty(player, "kuanshi_targets", names);
            room->addPlayerMark(target, "&kuanshi");
        } else {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to == Player::RoundStart) {
                QStringList names = player->property("kuanshi_targets").toStringList();
                room->setPlayerProperty(player, "kuanshi_targets", QStringList());
                foreach (ServerPlayer *p, room->getAllPlayers()) {
                    if (!names.contains(p->objectName()) || p->getMark("&kuanshi") <= 0) continue;
                    room->removePlayerMark(p, "&kuanshi");
                }
            } else if (change.to == Player::Draw) {
                if (player->isSkipped(Player::Draw) || player->getMark("kuanshi_skip") <= 0) return false;
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                room->setPlayerMark(player, "kuanshi_skip", 0);
                player->skip(Player::Draw);
            }
        }
        return false;
    }
};

class KuanshiEffect : public TriggerSkill
{
public:
    KuanshiEffect() : TriggerSkill("#kuanshi-effect")
    {
        events << EventLoseSkill << DamageInflicted << Death;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventLoseSkill) {
            if (data.toString() != "kuanshi") return false;
            QStringList names = player->property("kuanshi_targets").toStringList();
            room->setPlayerProperty(player, "kuanshi_targets", QStringList());
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (!names.contains(p->objectName()) || p->getMark("&kuanshi") <= 0) continue;
                room->removePlayerMark(p, "&kuanshi");
            }
        } else if (event == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player) return false;
            if (!player->hasSkill("kuanshi")) return false;
            QStringList names = player->property("kuanshi_targets").toStringList();
            room->setPlayerProperty(player, "kuanshi_targets", QStringList());
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (!names.contains(p->objectName()) || p->getMark("&kuanshi") <= 0) continue;
                room->removePlayerMark(p, "&kuanshi");
            }
        } else {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.damage <= 1) return false;
            if (damage.to->isDead()) return false;
            foreach (ServerPlayer *p, room->findPlayersBySkillName("kuanshi")) {
                if (p->isDead() || !p->hasSkill("kuanshi")) continue;
                QStringList names = p->property("kuanshi_targets").toStringList();
                if (!names.contains(damage.to->objectName())) continue;
                LogMessage log;
                log.type = damage.from != NULL ? "#KuanshiEffect" : "#KuanshiNoFromEffect";
                log.from = damage.to;
                if (damage.from)
                    log.to << damage.from;
                log.arg = "kuanshi";
                log.arg2 = QString::number(damage.damage);
                room->sendLog(log);
                room->notifySkillInvoked(p, "kuanshi");
                room->broadcastSkillInvoke("kuanshi");
                names.removeOne(damage.to->objectName());
                room->setPlayerProperty(player, "kuanshi_targets", names);
                if (damage.to->getMark("&kuanshi") > 0)
                    room->removePlayerMark(damage.to, "&kuanshi");
                room->addPlayerMark(p, "kuanshi_skip");
                return true;
            }
        }
        return false;
    }
};

GuolunCard::GuolunCard()
{
}

bool GuolunCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && !to_select->isKongcheng() && to_select != Self;
}

void GuolunCard::onEffect(const CardEffectStruct &effect) const
{
    if (effect.to->isDead() || effect.to->isKongcheng()) return;
    int id = effect.to->getRandomHandCardId();
    Room *room = effect.from->getRoom();
    room->showCard(effect.to, id);
    if (effect.from->isDead() || effect.from->isNude()) return;
    const Card *card = room->askForCard(effect.from, "..", "guolun-show", id, Card::MethodNone, effect.to);
    if (!card) return;
    int card_id = card->getEffectiveId();
    room->showCard(effect.from, card_id);

    int to_num = Sanguosha->getCard(id)->getNumber();
    int from_num = Sanguosha->getCard(card_id)->getNumber();
    if (to_num == from_num) return;

    foreach (ServerPlayer *p, room->getAlivePlayers()) {
        if (p != effect.from && p != effect.to) {
            JsonArray arr;
            arr << effect.from->objectName() << effect.to->objectName();
            room->doNotify(p, QSanProtocol::S_COMMAND_EXCHANGE_KNOWN_CARDS, arr);
        }
    }
    QList<CardsMoveStruct> exchangeMove;
    CardsMoveStruct move1(QList<int>(), effect.to, Player::PlaceHand,
        CardMoveReason(CardMoveReason::S_REASON_SWAP, effect.from->objectName(), effect.to->objectName(), "guolun", QString()));
    move1.card_ids << card_id;
    CardsMoveStruct move2(QList<int>(), effect.from, Player::PlaceHand,
        CardMoveReason(CardMoveReason::S_REASON_SWAP, effect.to->objectName(), effect.from->objectName(), "guolun", QString()));
    move2.card_ids << id;
    exchangeMove.push_back(move1);
    exchangeMove.push_back(move2);
    room->moveCardsAtomic(exchangeMove, true);

    ServerPlayer *drawer = NULL;
    if (to_num < from_num)
        drawer = effect.to;
    else if (to_num > from_num)
        drawer = effect.from;
    if (drawer == NULL) return;
    drawer->drawCards(1, "guolun");
}

class Guolun : public ZeroCardViewAsSkill
{
public:
    Guolun() : ZeroCardViewAsSkill("guolun")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("GuolunCard");
    }

    const Card *viewAs() const
    {
        return new GuolunCard;
    }
};

class Songsang : public TriggerSkill
{
public:
    Songsang() : TriggerSkill("songsang")
    {
        events << Death;
        frequency = Limited;
        limit_mark = "@songsangMark";
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DeathStruct death = data.value<DeathStruct>();
        if (death.who == player || !player->hasSkill(this)) return false;
        if (player->getMark("@songsangMark") <= 0 || !player->askForSkillInvoke(this)) return false;
        room->broadcastSkillInvoke("songsang");
        room->doSuperLightbox("sp_pangtong", "songsang");
        room->removePlayerMark(player, "@songsangMark");
        if (player->isWounded())
            room->recover(player, RecoverStruct(player));
        else
            room->gainMaxHp(player);
        if (player->hasSkill("zhanji")) return false;
        room->acquireSkill(player, "zhanji");
        return false;
    }
};

class Zhanji : public TriggerSkill
{
public:
    Zhanji() : TriggerSkill("zhanji")
    {
        events << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to && move.to == player && move.to_place == Player::PlaceHand && move.from_places.contains(Player::DrawPile)) {
            if (move.reason.m_reason == CardMoveReason::S_REASON_DRAW && move.reason.m_skillName != objectName()) {
                if (player->getPhase() == Player::Play) {
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                    player->drawCards(1, objectName());
                }
            }
        }
        return false;
    }
};

class Bizheng : public TriggerSkill
{
public:
    Bizheng() : TriggerSkill("bizheng")
    {
        events << EventPhaseEnd;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (player->getPhase() != Player::Draw) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, room->getOtherPlayers(player), objectName(), "@bizheng-invoke", true, true);
        if (!target) return false;
        room->broadcastSkillInvoke(objectName());
        target->drawCards(2, objectName());
        QList<ServerPlayer *> players;
        players << player << target;
        room->sortByActionOrder(players);
        foreach (ServerPlayer *p, players) {
            if (p->isAlive() && p->getHandcardNum() > p->getMaxHp()) {
                room->askForDiscard(p, objectName(), 2, 2, false, true);
            }
        }
        return false;
    }
};

class YidianVS : public ZeroCardViewAsSkill
{
public:
    YidianVS() : ZeroCardViewAsSkill("yidian")
    {
        response_pattern = "@@yidian";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        return new ExtraCollateralCard;
    }
};

class Yidian : public TriggerSkill
{
public:
    Yidian() : TriggerSkill("yidian")
    {
        events << PreCardUsed;
        view_as_skill = new YidianVS;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isNDTrick() && !use.card->isKindOf("BasicCard")) return false;

        room->setCardFlag(use.card, "yidian_distance");
        QList<ServerPlayer *> ava;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (use.card->isKindOf("AOE") && p == use.from) continue;
            if (use.to.contains(p) || room->isProhibited(use.from, p, use.card)) continue;
            if (use.card->targetFixed())
                ava << p;
            else {
                if (use.card->targetFilter(QList<const Player *>(), p, use.from))
                    ava << p;
            }
        }
        room->setCardFlag(use.card, "-yidian_distance");
        if (ava.isEmpty()) return false;

        QString name = use.card->objectName();
        if (use.card->isKindOf("Slash"))
            name = "slash";
        bool has = false;
        foreach (int id, room->getDiscardPile()) {
            const Card *card = Sanguosha->getCard(id);
            QString card_name = card->objectName();
            if (card->isKindOf("Slash"))
                card_name = "slash";
            if (card_name == name) {
                has = true;
                break;
            }
        }
        if (has) return false;
        ServerPlayer *target = NULL;
        if (!use.card->isKindOf("Collateral")) {
            player->tag["YidianData"] = data;
            target = room->askForPlayerChosen(player, ava, objectName(), "@yidian-invoke:" + name, true);
            player->tag.remove("YidianData");
        } else {
            QStringList tos;
            foreach(ServerPlayer *t, use.to)
                tos.append(t->objectName());

            room->setPlayerProperty(player, "extra_collateral", use.card->toString());
            room->setPlayerProperty(player, "extra_collateral_current_targets", tos.join("+"));
            room->askForUseCard(player, "@@yidian", "@yidian:" + name);
            room->setPlayerProperty(player, "extra_collateral", QString());
            room->setPlayerProperty(player, "extra_collateral_current_targets", QString("+"));

            foreach(ServerPlayer *p, room->getAlivePlayers()) {
                if (p->hasFlag("ExtraCollateralTarget")) {
                    room->setPlayerFlag(p,"-ExtraCollateralTarget");
                    target = p;
                    break;
                }
            }
        }
        if (!target) return false;
        LogMessage log;
        log.type = "#QiaoshuiAdd";
        log.from = player;
        log.to << target;
        log.card_str = use.card->toString();
        log.arg = "yidian";
        room->sendLog(log);
        room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, player->objectName(), target->objectName());
        room->notifySkillInvoked(player, objectName());
        room->broadcastSkillInvoke(objectName());
        use.to.append(target);
        room->sortByActionOrder(use.to);
        if (use.card->isKindOf("Collateral")) {
            ServerPlayer *victim = target->tag["collateralVictim"].value<ServerPlayer *>();
            if (victim) {
                LogMessage log;
                log.type = "#CollateralSlash";
                log.from = player;
                log.to << victim;
                room->sendLog(log);
                room->doAnimate(QSanProtocol::S_ANIMATE_INDICATE, target->objectName(), victim->objectName());
            }
        }
        data = QVariant::fromValue(use);
        return false;
    }
};

class YidianTargetMod : public TargetModSkill
{
public:
    YidianTargetMod() : TargetModSkill("#yidian-target")
    {
        frequency = NotFrequent;
        pattern = ".";
    }

    int getDistanceLimit(const Player *, const Card *card, const Player *) const
    {
        if (card->hasFlag("yidian_distance") && (card->isNDTrick() || card->isKindOf("BasicCard")))
            return 1000;
        else
            return 0;
    }
};

class Lianpian : public TriggerSkill
{
public:
    Lianpian() : TriggerSkill("lianpian")
    {
        events << TargetSpecifying <<  CardFinished << EventPhaseChanging;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.from != Player::Play) return false;
            room->setPlayerProperty(player, "lianpian_targets", QStringList());
        } else {
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            if (event == TargetSpecifying) {
                if (!player->hasSkill(this) || player->getMark("lianpian-PlayClear") >= 3) return false;
                if (!room->CardInTable(use.card)) return false;
                QList<ServerPlayer *> targets;
                QStringList names = player->property("lianpian_targets").toStringList();
                foreach (ServerPlayer *p, use.to) {
                    if (p->isAlive() && names.contains(p->objectName())) {
                        targets << p;
                    }
                }
                if (targets.isEmpty()) return false;
                if (!player->askForSkillInvoke(this)) return false;
                room->broadcastSkillInvoke(objectName());
                room->addPlayerMark(player, "lianpian-PlayClear");
                player->drawCards(1, objectName());
                if (targets.contains(player))
                    targets.removeOne(player);
                if (targets.isEmpty()) return false;
                ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@lianpian-give:" + use.card->objectName(), true, true);
                if (!target) return false;
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "lianpian", QString());
                room->obtainCard(target, use.card, reason, true);
            } else {
                if (!use.from || use.from->isDead()) return false;
                QStringList names;
                foreach (ServerPlayer *p, use.to) {
                    if (p->isAlive() && !names.contains(p->objectName()))
                        names << p->objectName();
                }
                room->setPlayerProperty(use.from, "lianpian_targets", names);
            }
        }
        return false;
    }
};

class Guanchao : public TriggerSkill
{
public:
    Guanchao() : TriggerSkill("guanchao")
    {
        events << EventPhaseStart << CardUsed << CardResponded << EventPhaseChanging;
        frequency = Frequent;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::Play) return false;
            if (!player->askForSkillInvoke(this)) return false;
            room->broadcastSkillInvoke(objectName());
            QString choice = room->askForChoice(player, objectName(), "up+down");
            LogMessage log;
            log.type = "#FumianFirstChoice";
            log.from = player;
            log.arg = "guanchao:" + choice;
            room->sendLog(log);
            room->setPlayerFlag(player, "guanchao_" + choice);
            room->setPlayerMark(player, "guanchao-PlayClear", -1); //避免第一张使用的牌点数为0的问题
        } else if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.from != Player::Play) return false;
            if (player->hasFlag("guanchao_up"))
                room->setPlayerFlag(player, "-guanchao_up");
            if (player->hasFlag("guanchao_down"))
                room->setPlayerFlag(player, "-guanchao_down");
        } else {
            const Card *card = NULL;
            if (event == CardUsed) {
                CardUseStruct use = data.value<CardUseStruct>();
                if (use.card->isKindOf("SkillCard")) return false;
                card = use.card;
            } else {
                CardResponseStruct res = data.value<CardResponseStruct>();
                if (!res.m_isUse || res.m_card->isKindOf("SkillCard")) return false;
                card = res.m_card;
            }
            if (!card) return false;

            int mark = player->getMark("guanchao-PlayClear");
            int num = card->getNumber();
            if (mark < 0) {
                room->setPlayerMark(player, "guanchao-PlayClear", num);
                return false;
            }

            if (player->hasFlag("guanchao_up")) {
                if (num > mark) {
                    room->setPlayerMark(player, "guanchao-PlayClear", num);
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                    player->drawCards(1, objectName());
                } else {
                    room->setPlayerFlag(player, "-guanchao_up");
                }
            } else if (player->hasFlag("guanchao_down")) {
                if (num < mark) {
                    room->setPlayerMark(player, "guanchao-PlayClear", num);
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                    player->drawCards(1, objectName());
                } else {
                    room->setPlayerFlag(player, "-guanchao_down");
                }
            }
        }
        return false;
    }
};

class Xunxian : public TriggerSkill
{
public:
    Xunxian() : TriggerSkill("xunxian")
    {
        events << BeforeCardsMove;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        if (!room->hasCurrent()) return false;
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if (move.to_place != Player::DiscardPile) return false;
        if ((move.from_places.contains(Player::PlaceTable) && (move.reason.m_reason == CardMoveReason::S_REASON_USE ||
             move.reason.m_reason == CardMoveReason::S_REASON_LETUSE)) || move.reason.m_reason == CardMoveReason::S_REASON_RESPONSE) {
            const Card *card = move.reason.m_extraData.value<const Card *>();
            if (!card || card->isKindOf("SkillCard")) return false;
            ServerPlayer *from = room->findPlayerByObjectName(move.reason.m_playerId);
            if (!from || from->isDead() || !from->hasSkill(this)) return false;
            if (from->getMark("xunxian-Clear") > 0 || (room->getCurrent() && room->getCurrent() == from)) return false;
            if (move.reason.m_reason == CardMoveReason::S_REASON_USE || move.reason.m_reason == CardMoveReason::S_REASON_LETUSE) {
                if (!room->CardInPlace(card, Player::PlaceTable)) return false;
            }

            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getOtherPlayers(from)) {
                if (p->getHandcardNum() > from->getHandcardNum())
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(from, targets, objectName(), "@xunxian-give:" + card->objectName(), true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(from, "xunxian-Clear");
            CardMoveReason reason(CardMoveReason::S_REASON_GIVE, from->objectName(), target->objectName(), "xunxian", QString());
            room->obtainCard(target, card, reason, true);

            QList<int> ids;
            if (card->isVirtualCard())
                ids = card->getSubcards();
            else
                ids << card->getEffectiveId();
            move.removeCardIds(ids);
            data = QVariant::fromValue(move);
        }
        return false;
    }
};

class SpYoudi : public PhaseChangeSkill
{
public:
    SpYoudi() : PhaseChangeSkill("spyoudi")
    {
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Finish || player->isKongcheng()) return false;
        Room *room = player->getRoom();
        QList<ServerPlayer *> targets;
        foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
            if (p->canDiscard(player, "h"))
                targets << p;
        }
        if (targets.isEmpty()) return false;
        ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@spyoudi-invoke", true, true);
        if (!target) return false;

        room->broadcastSkillInvoke(objectName());
        if (!target->canDiscard(player, "h")) return false;
        int card_id = room->askForCardChosen(target, player, "h", objectName(), false, Card::MethodDiscard);
        room->throwCard(card_id, player, target);
        const Card *card = Sanguosha->getCard(card_id);
        if (!card->isKindOf("Slash")) {
            if (target->isAlive() && !target->isNude()) {
                int card_id = room->askForCardChosen(player, target, "he", objectName());
                CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
                room->obtainCard(player, Sanguosha->getCard(card_id), reason, room->getCardPlace(card_id) != Player::PlaceHand);
            }
        }
        if (!card->isBlack())
            player->drawCards(1, objectName());
        return false;
    }
};

DuanfaCard::DuanfaCard()
{
    target_fixed = true;
}

void DuanfaCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    int n = subcardsLength();
    room->addPlayerMark(source, "duanfa_num-PlayClear", n);
    source->drawCards(n, "duanfa");
}

class Duanfa : public ViewAsSkill
{
public:
    Duanfa() : ViewAsSkill("duanfa")
    {
    }

    bool viewFilter(const QList<const Card *> &selected, const Card *to_select) const
    {
        int n = Self->getMaxHp() - Self->getMark("duanfa_num-PlayClear");
        return !Self->isJilei(to_select) && to_select->isBlack() && selected.length() < n;
    }

    const Card *viewAs(const QList<const Card *> &cards) const
    {
        if (cards.isEmpty())
            return NULL;

        DuanfaCard *c = new DuanfaCard;
        c->addSubcards(cards);
        return c;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMaxHp() > player->getMark("duanfa_num-PlayClear");
    }
};

QinguoCard::QinguoCard()
{
    mute = true;
}

bool QinguoCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("qinguo");
    slash->deleteLater();
    return slash->targetFilter(targets, to_select, Self);
}

void QinguoCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    Slash *slash = new Slash(Card::NoSuit, 0);
    slash->setSkillName("qinguo");
    slash->deleteLater();
    room->useCard(CardUseStruct(slash, card_use.from, card_use.to), false);
}

class QinguoVS : public ZeroCardViewAsSkill
{
public:
    QinguoVS() : ZeroCardViewAsSkill("qinguo")
    {
        response_pattern = "@@qinguo";
    }

    const Card *viewAs() const
    {
        return new QinguoCard;
    }
};

class Qinguo : public TriggerSkill
{
public:
    Qinguo() : TriggerSkill("qinguo")
    {
        events << CardFinished << BeforeCardsMove << CardsMoveOneTime;
        view_as_skill = new QinguoVS;
    }

    int getPriority(TriggerEvent event) const
    {
        if (event == BeforeCardsMove)
            return -1;
        else
            return TriggerSkill::getPriority(event);
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardFinished) {
            if (player->getPhase() == Player::NotActive) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (!use.card->isKindOf("EquipCard")) return false;
            bool can_slash = false;
            Slash *slash = new Slash(Card::NoSuit, 0);
            slash->setSkillName(objectName());
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (player->canSlash(p, slash, true)) {
                    can_slash = true;
                    break;
                }
            }
            if (!can_slash) return false;
            room->askForUseCard(player, "@@qinguo", "@qinguo");
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if ((move.to && move.to == player && move.to_place == Player::PlaceEquip) ||
                    (move.from && move.from == player && move.from_places.contains(Player::PlaceEquip))) {
                if (event == BeforeCardsMove) {
                    room->setPlayerMark(player, "qinguo_equip_num", player->getEquips().length());
                } else {
                    int mark = player->getMark("qinguo_equip_num");
                    int num = player->getEquips().length();
                    if (num == player->getHp() && num != mark && player->getLostHp() > 0) {
                        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                        room->recover(player, RecoverStruct(player));
                    }
                }
            }
        }
        return false;
    }
};

class Lianhua : public PhaseChangeSkill
{
public:
    Lianhua() : PhaseChangeSkill("lianhua")
    {
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        Room *room = player->getRoom();
        if (player->getPhase() == Player::Play) {
            room->setPlayerProperty(player, "danxue_red", 0);
            room->setPlayerProperty(player, "danxue_black", 0);
            player->loseAllMarks("&danxue");
        } else if (player->getPhase() == Player::Start) {
            int red = player->property("danxue_red").toInt();
            int black = player->property("danxue_black").toInt();
            int all = red + black;
            if (all <= 3) {
                room->acquireOneTurnSkills(player, "lianhua", "yingzi");
                LianhuaCard(player, "peach");
            } else {
                if (red > black) {
                    room->acquireOneTurnSkills(player, "lianhua", "tenyearguanxing");
                    LianhuaCard(player, "ex_nihilo");
                } else if (black > red) {
                    room->acquireOneTurnSkills(player, "lianhua", "zhiyan");
                    LianhuaCard(player, "snatch");
                } else if (black == red) {
                    room->acquireOneTurnSkills(player, "lianhua", "gongxin");
                    QList<int> slash, duel, get;
                    QList<int> cards = room->getDiscardPile() + room->getDrawPile();
                    foreach (int id, cards) {
                        const Card *card = Sanguosha->getCard(id);
                        if (card->isKindOf("Slash"))
                            slash << id;
                        else if (card->objectName() == "duel")
                            duel << id;
                    }
                    if (!slash.isEmpty())
                        get << slash.at(qrand() % slash.length());
                    if (!duel.isEmpty())
                        get << duel.at(qrand() % duel.length());
                    if (get.isEmpty()) return false;
                    DummyCard *dummy = new DummyCard(get);
                    room->obtainCard(player, dummy, true);
                    delete dummy;
                }
            }
        }
        return false;
    }

private:
    void LianhuaCard(ServerPlayer *player, const QString &name) const
    {
        Room *room = player->getRoom();
        QList<int> cards = room->getDiscardPile() + room->getDrawPile();
        QList<int> ids;
        foreach (int id, cards) {
            if (Sanguosha->getCard(id)->objectName() == name)
                ids << id;
        }
        if (ids.isEmpty()) return;
        int id = ids.at(qrand() % ids.length());
        room->obtainCard(player, id, true);
    }
};

class LianhuaEffect : public TriggerSkill
{
public:
    LianhuaEffect() : TriggerSkill("#lianhua-effect")
    {
        events << Damaged << EventLoseSkill;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Damaged) {
            DamageStruct damage = data.value<DamageStruct>();
            foreach (ServerPlayer *p, room->getOtherPlayers(damage.to)) {
                if (!damage.to || damage.to->isDead()) return false;
                if (p->isDead() || !p->hasSkill("lianhua") || p->getPhase() != Player::NotActive) continue;
                room->sendCompulsoryTriggerLog(p, "lianhua", true, true);
                if (damage.to->isYourFriend(p)) {
                    int n = p->property("danxue_red").toInt();
                    room->setPlayerProperty(p, "danxue_red", n + 1);
                } else {
                    int n = p->property("danxue_black").toInt();
                    room->setPlayerProperty(p, "danxue_black", n + 1);
                }
                p->gainMark("&danxue");
            }
        } else {
            if (data.toString() != "lianhua") return false;
            room->setPlayerProperty(player, "danxue_red", 0);
            room->setPlayerProperty(player, "danxue_black", 0);
            player->loseAllMarks("&danxue");
        }
        return false;
    }
};

ZhafuCard::ZhafuCard()
{
}

void ZhafuCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    room->doSuperLightbox("gexuan", "zhafu");
    room->removePlayerMark(effect.from, "@zhafuMark");

    QStringList names = effect.to->property("zhafu_from").toStringList();
    if (names.contains(effect.from->objectName())) return;
    names << effect.from->objectName();
    room->setPlayerProperty(effect.to, "zhafu_from", names);
}

class ZhafuVS : public ZeroCardViewAsSkill
{
public:
    ZhafuVS() : ZeroCardViewAsSkill("zhafu")
    {
        frequency = Limited;
        limit_mark = "@zhafuMark";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return player->getMark("@zhafuMark") > 0;
    }

    const Card *viewAs() const
    {
        return new ZhafuCard;
    }
};

class Zhafu : public PhaseChangeSkill
{
public:
    Zhafu() : PhaseChangeSkill("zhafu")
    {
        frequency = Limited;
        limit_mark = "@zhafuMark";
        view_as_skill = new ZhafuVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Discard) return false;
        Room *room = player->getRoom();
        QStringList names = player->property("zhafu_from").toStringList();
        if (names.isEmpty()) return false;
        room->setPlayerProperty(player, "zhafu_from", QStringList());

        LogMessage log;
        log.type = (player->getHandcardNum() > 1) ? "#ZhafuEffect" : (!player->isKongcheng() ? "#ZhafuOne" : "#ZhafuZero");
        log.from = player;
        log.arg = objectName();
        room->sendLog(log);
        if (player->getHandcardNum() <= 1) return false;

        QList<ServerPlayer *> players;
        foreach (QString name, names) {
            ServerPlayer *from = room->findPlayerByObjectName(name);
            if (from && from->isAlive() && from->hasSkill(this))
                players << from;
        }
        if (players.isEmpty()) return false;
        room->sortByActionOrder(players);

        foreach (ServerPlayer *p, players) {
            if (p->isDead() || !p->hasSkill(this)) continue;
            if (player->getHandcardNum() <= 1) return false;
            int id = -1;
            const Card *card = room->askForCard(player, ".!", "zhafu-keep:" + p->objectName(), QVariant::fromValue(p), Card::MethodNone, p);
            if (!card) {
                card = player->getRandomHandCard();
                id = card->getEffectiveId();
            } else
                id = card->getSubcards().first();

            DummyCard *dummy = new DummyCard;
            foreach (const Card *c, player->getCards("h")) {
                if (c->getEffectiveId() != id)
                    dummy->addSubcard(c);
            }
            if (dummy->subcardsLength() > 0) {
                CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), p->objectName(), "zhafu", QString());
                room->obtainCard(p, dummy, reason, false);
            }
            delete dummy;
        }
        return false;
    }
};

SongshuCard::SongshuCard()
{
}

bool SongshuCard::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    return targets.isEmpty() && Self->canPindian(to_select);
}

void SongshuCard::onEffect(const CardEffectStruct &effect) const
{
    if (!effect.from->canPindian(effect.to, false)) return;
    bool pindian = effect.from->pindian(effect.to, "songshu");
    if (pindian) {
        int n = effect.from->usedTimes("SongshuCard");
        effect.from->getRoom()->addPlayerHistory(effect.from, "SongshuCard", -n);
    } else
        effect.to->drawCards(2, "songshu");
}


class Songshu : public ZeroCardViewAsSkill
{
public:
    Songshu() : ZeroCardViewAsSkill("songshu")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("SongshuCard") && player->canPindian();
    }

    const Card *viewAs() const
    {
        return new SongshuCard;
    }
};

class Sibian : public PhaseChangeSkill
{
public:
    Sibian() : PhaseChangeSkill("sibian")
    {
    }

    bool onPhaseChange(ServerPlayer *player) const
    {
        if (player->getPhase() != Player::Draw) return false;
        if (!player->askForSkillInvoke(this)) return false;
        Room *room = player->getRoom();
        room->broadcastSkillInvoke(objectName());
        QList<int> show = room->showDrawPile(player, 4, objectName());

        int max = Sanguosha->getCard(show.first())->getNumber();
        int min = Sanguosha->getCard(show.first())->getNumber();
        foreach (int id, show) {
            int num = Sanguosha->getCard(id)->getNumber();
            if (num > max)
                max = num;
            if (num < min)
                min = num;
        }
        DummyCard *dummy = new DummyCard;
        foreach (int id, show) {
            int num = Sanguosha->getCard(id)->getNumber();
            if (num == min || num == max) {
                dummy->addSubcard(id);
                show.removeOne(id);
            }
        }
        int length = dummy->subcardsLength();
        if (length > 0)
            room->obtainCard(player, dummy, true);
        delete dummy;

        DummyCard *dum = new DummyCard;
        foreach (int id, show) {
            if (room->getCardPlace(id) == Player::PlaceTable)
                dum->addSubcard(id);
        }
        if (length == 2 && qAbs(max - min) < room->alivePlayerCount()) {
            if (dum->subcardsLength() > 0) {
                room->fillAG(dum->getSubcards(), player);
                int hand = player->getHandcardNum();
                foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                    if (p->getHandcardNum() < hand)
                        hand = p->getHandcardNum();
                }
                QList<ServerPlayer *> targets;
                foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                    if (p->getHandcardNum() == hand)
                        targets << p;
                }
                if (!targets.isEmpty()) {
                    ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@sibian-give", true, true);
                    if (target) {
                        CardMoveReason reason(CardMoveReason::S_REASON_GIVE, player->objectName(), target->objectName(), "sibian", QString());
                        room->obtainCard(target, dum, reason, true);
                    } else {
                        CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "sibian", QString());
                        room->throwCard(dum, reason, NULL);
                    }
                } else {
                    CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "sibian", QString());
                    room->throwCard(dum, reason, NULL);
                }
                room->clearAG(player);
            }

        } else {
            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, player->objectName(), "sibian", QString());
            room->throwCard(dum, reason, NULL);
        }
        delete dum;
        return true;
    }
};

class Biaozhao : public TriggerSkill
{
public:
    Biaozhao() : TriggerSkill("biaozhao")
    {
        events << EventPhaseStart << CardsMoveOneTime;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() == Player::Finish) {
                if (player->isNude() || !player->getPile("bzbiao").isEmpty()) return false;
                const Card *card = room->askForCard(player, "..", "biaozhao-put", QVariant(), Card::MethodNone);
                if (!card) return false;
                LogMessage log;
                log.type = "#InvokeSkill";
                log.from = player;
                log.arg = objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName());
                room->notifySkillInvoked(player, objectName());
                player->addToPile("bzbiao", card);
            } else if (player->getPhase() == Player::Start) {
                if (player->getPile("bzbiao").isEmpty()) return false;
                room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                player->clearOnePrivatePile("bzbiao");
                if (player->isDead()) return false;
                ServerPlayer *target = room->askForPlayerChosen(player, room->getAlivePlayers(), objectName(), "@biaozhao-invoke");
                room->doAnimate(1, player->objectName(), target->objectName());
                room->recover(target, RecoverStruct(player));
                int n = target->getHandcardNum();
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->getHandcardNum() > n)
                        n = p->getHandcardNum();
                }
                int draw = qMin(5, n - target->getHandcardNum());
                if (draw <= 0) return false;
                target->drawCards(draw, objectName());
            }
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (!move.to && move.to_place == Player::DiscardPile) {
                foreach (int id, move.card_ids) {
                    const Card *card = Sanguosha->getCard(id);
                    foreach (ServerPlayer *p, room->getAllPlayers()) {
                        if (p->isDead() || !p->hasSkill(this) || p->getPile("bzbiao").isEmpty()) continue;
                        foreach (int idd, p->getPile("bzbiao")) {
                            if (p->isDead() || !p->hasSkill(this)) continue;
                            const Card *c = Sanguosha->getCard(idd);
                            if (card->getSuit() == c->getSuit() && card->getNumber() == c->getNumber()) {
                                room->sendCompulsoryTriggerLog(p, objectName() ,true, true);
                                if (((move.reason.m_reason & CardMoveReason::S_MASK_BASIC_REASON) == CardMoveReason::S_REASON_DISCARD) &&
                                        move.from && move.from->isAlive()) {
                                    ServerPlayer *from = room->findPlayerByObjectName(move.from->objectName());
                                    if (!from || from->isDead()) {
                                        CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), p->objectName(), "biaozhao", QString());
                                        room->throwCard(c, reason, NULL);
                                    } else {
                                        room->obtainCard(from, c, true);
                                    }
                                } else {
                                    CardMoveReason reason(CardMoveReason::S_REASON_REMOVE_FROM_PILE, QString(), p->objectName(), "biaozhao", QString());
                                    room->throwCard(c, reason, NULL);
                                }
                                room->loseHp(p);
                            }
                        }
                    }
                }
            }
        }
        return false;
    }
};

class Yechou : public TriggerSkill
{
public:
    Yechou() : TriggerSkill("yechou")
    {
        events << Death << EventPhaseChanging << EventPhaseStart;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == Death) {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who != player || !death.who->hasSkill(this)) return false;
            QList<ServerPlayer *> targets;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if (p->getLostHp() > 1)
                    targets << p;
            }
            if (targets.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, targets, objectName(), "@yechou-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            room->addPlayerMark(target, "&yechou");
        } else if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->isDead() || p->getMark("&yechou") <= 0) continue;
                for (int i = 0; i < p->getMark("&yechou"); i++) {
                    if (p->isDead()) break;
                    LogMessage log;
                    log.type = "#YechouEffect";
                    log.from = p;
                    log.arg = objectName();
                    room->sendLog(log);
                    room->loseHp(p);
               }
            }
        } else {
            if (player->isDead()) return false;
            if (player->getPhase() != Player::RoundStart || player->getMark("&yechou") <= 0) return false;
            room->setPlayerMark(player, "&yechou", 0);
        }
        return false;
    }
};

class Guanwei : public TriggerSkill
{
public:
    Guanwei() : TriggerSkill("guanwei")
    {
        events << EventPhaseEnd << CardFinished << EventPhaseChanging;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseEnd) {
            if (player->getPhase() != Player::Play) return false;
            foreach (ServerPlayer *p, room->findPlayersBySkillName(objectName())) {
                if (player->isDead()) return false;
                if (player->getMark("guanwei_disable-Clear") > 0 || player->getMark("guanwei_num-Clear") < 2) return false;
                if (p->isDead() || !p->hasSkill(this) || p->getMark("guanwei_used-Clear") > 0 || !p->canDiscard(p, "he")) continue;
                const Card *card = room->askForCard(p, "..", "guanwei-invoke:" + player->objectName(), QVariant::fromValue(player), objectName());
                if (!card) continue;
                room->broadcastSkillInvoke(objectName());
                room->addPlayerMark(p, "guanwei_used-Clear");
                player->drawCards(2, objectName());
                if (player->isDead()) return false;

                room->addPlayerHistory(player, ".");
                foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                    foreach (QString mark, p->getMarkNames()) {
                        if (mark.endsWith("-PlayClear") && p->getMark(mark) > 0)
                            room->setPlayerMark(p, mark, 0);
                    }
                }

                RoomThread *thread = room->getThread();
                if (!thread->trigger(EventPhaseStart, room, player)) {
                    thread->trigger(EventPhaseProceeding, room, player);
                }
                thread->trigger(EventPhaseEnd, room, player);
            }
        } else if (event == EventPhaseChanging) {
            PhaseChangeStruct change = data.value<PhaseChangeStruct>();
            if (change.to != Player::NotActive) return false;
            room->setPlayerProperty(player, "guanwei_used_suit", QString());
        } else {
            if (player->isDead()) return false;
            //if (player->getPhase() == Player::NotActive) return false;
            CardUseStruct use = data.value<CardUseStruct>();
            if (use.card->isKindOf("SkillCard")) return false;
            room->addPlayerMark(player, "guanwei_num-Clear");
            QString suit = use.card->getSuitString();
            QString used_suit = player->property("guanwei_used_suit").toString();
            if (used_suit != QString() && used_suit != suit)
                room->addPlayerMark(player, "guanwei_disable-Clear");
            else if (used_suit == QString())
                room->setPlayerProperty(player, "guanwei_used_suit", suit);
        }
        return false;
    }
};

class Gongqing : public TriggerSkill
{
public:
    Gongqing() : TriggerSkill("gongqing")
    {
        events << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (!damage.from || damage.from->isDead()) return false;
        int n = damage.from->getAttackRange();
        LogMessage log;
        log.from = player;
        log.to << damage.from;
        log.arg = QString::number(damage.damage);
        if (n < 3) {
            if (damage.damage <= 1) return false;
            log.type = "#GongqingReduce";
            log.arg2 = QString::number(1);
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName());
            damage.damage = 1;
        } else {
            log.type = "#GongqingAdd";
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName());
        }
        data = QVariant::fromValue(damage);
        return false;
    }
};

FuhaiCard::FuhaiCard()
{
    target_fixed = true;
    will_throw = false;
    handling_method = Card::MethodNone;
}

void FuhaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    int sid = getSubcards().first();
    int tid = -1;
    ServerPlayer *now = source;
    int times = 0;

    while (!source->isKongcheng()) {
        if (source->isDead() || source->getMark("fuhai_disable-PlayClear") > 0) return;

        QStringList choices;
        ServerPlayer *next = now->getNextAlive();
        ServerPlayer *last = now->getNextAlive(room->alivePlayerCount() - 1);

        if (last->isAlive() && !last->isKongcheng() && last->getMark("fuhai-PlayClear") <= 0 && last != source)
            choices << "last";
        if (next != last && next->isAlive() && !next->isKongcheng() && next->getMark("fuhai-PlayClear") <= 0 && next != source)
            choices << "next";
        if (choices.isEmpty()) return;

        if (sid < 0) {
            source->tag["FuhaiNow"] = QVariant::fromValue(now);
            sid = room->askForCardShow(source, source, "fuhai")->getEffectiveId();
            source->tag.remove("FuhaiNow");
            if (sid < 0) return;
        }
        room->showCard(source, sid);

        QString  choice = room->askForChoice(source, "fuhai", choices.join("+"), QVariant::fromValue(now));
        if (choice == "last")
            now = last;
        else
            now = next;
        room->addPlayerMark(now, "fuhai-PlayClear");
        times++;

        if (now->isDead() || now->isKongcheng()) return;
        room->doAnimate(1, source->objectName(), now->objectName());
        now->tag["FuhaiID"] = sid + 1;
        tid = room->askForCardShow(now, source, "fuhai")->getEffectiveId();
        now->tag.remove("FuhaiID");
        if (tid < 0) return;
        room->showCard(now, tid);

        int snum = Sanguosha->getCard(sid)->getNumber();
        int tnum = Sanguosha->getCard(tid)->getNumber();
        if (snum >= tnum) {
            room->throwCard(sid, source, NULL);
        } else {
            room->throwCard(tid, now, NULL);
            QList<ServerPlayer *> players;
            players << source << now;
            room->sortByActionOrder(players);
            room->drawCards(players, times, "fuhai");
            room->addPlayerMark(source, "fuhai_disable-PlayClear");
        }
        sid = -1;
    }
}

class Fuhai : public OneCardViewAsSkill
{
public:
    Fuhai() : OneCardViewAsSkill("fuhai")
    {
        filter_pattern = ".|.|.|hand";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        if (player->getMark("fuhai_disable-PlayClear") > 0) return false;
        foreach (const Player *p, player->getAliveSiblings()) {
            if (p->getMark("fuhai-PlayClear") <= 0)
                return true;
        }
        return false;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        FuhaiCard *c = new FuhaiCard;
        c->addSubcard(originalCard);
        return c;
    }
};

MobileFuhaiCard::MobileFuhaiCard()
{
    target_fixed = true;
}

void MobileFuhaiCard::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &) const
{
    QStringList choices;
    foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
        if (p->isDead()) continue;
        QString choice = room->askForChoice(p, "mobilefuhai", "up+down");
        if (p->isAlive())
            choices << choice;
    }
    if (choices.isEmpty()) return;

    int i = 0;
    foreach (ServerPlayer *p, room->getOtherPlayers(source)) {
        if  (i > choices.length()) break;
        if (p->isDead()) continue;
        LogMessage log;
        log.type = "#ShouxiChoice";
        log.from = p;
        log.arg = "mobilefuhai:" + choices.at(i);
        room->sendLog(log);
        i++;
    }

    int draw = 1;
    while (draw < choices.length()) {
        if (choices.at(draw - 1) != choices.at(draw)) break;
        draw++;
    }
    if (draw <= 1) return;
    source->drawCards(draw, "mobilefuhai");
}

class MobileFuhai : public ZeroCardViewAsSkill
{
public:
    MobileFuhai() : ZeroCardViewAsSkill("mobilefuhai")
    {
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("MobileFuhaiCard");
    }

    const Card *viewAs() const
    {
        MobileFuhaiCard *c = new MobileFuhaiCard;
        return c;
    }
};

class Zhidao : public TriggerSkill
{
public:
    Zhidao() : TriggerSkill("zhidao")
    {
        events << Damage;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (player->getPhase() != Player::Play || player->getMark("zhidao-PlayClear") > 0) return false;
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.to == player) return false;
        room->addPlayerMark(player, "zhidao-PlayClear");

        if (damage.to->isDead() || damage.to->isAllNude()) return false;
        room->sendCompulsoryTriggerLog(player, objectName(), true, true);
        DummyCard get;
        if (!damage.to->isKongcheng()) {
            int id = room->askForCardChosen(player, damage.to, "h", objectName());
            get.addSubcard(id);
        }
        if (!damage.to->getEquips().isEmpty()) {
            int id = room->askForCardChosen(player, damage.to, "e", objectName());
            get.addSubcard(id);
        }
        if (!damage.to->getJudgingArea().isEmpty()) {
            int id = room->askForCardChosen(player, damage.to, "j", objectName());
            get.addSubcard(id);
        }
        if (get.subcardsLength() > 0) {
            CardMoveReason reason(CardMoveReason::S_REASON_EXTRACTION, player->objectName());
            room->obtainCard(player, &get, reason, false);
            room->addPlayerMark(player, "zhidao-Clear");
        }
        return false;
    }
};

class ZhidaoPro : public ProhibitSkill
{
public:
    ZhidaoPro() : ProhibitSkill("#zhidao-pro")
    {
    }

    bool isProhibited(const Player *from, const Player *to, const Card *card, const QList<const Player *> &) const
    {
        return from->getMark("zhidao-Clear") > 0 && from != to && !card->isKindOf("SkillCard");
    }
};

class SpJili : public TriggerSkill
{
public:
    SpJili() : TriggerSkill("spjili")
    {
        events << TargetConfirming;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard") || !use.card->isRed()) return false;
        if (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) return false;

        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || use.to.contains(p) || use.from == p || !p->hasSkill(this)) continue;
            if (player->distanceTo(p) != 1) return false;
            if (use.from && use.from->isAlive() && use.from->isProhibited(p, use.card)) continue;
            int n = 1;
            if (use.card->isKindOf("Peach") || use.card->isKindOf("ExNihilo") || use.card->isKindOf("Analeptic"))
                n = 2;
            //room->sendCompulsoryTriggerLog(p, objectName(), true, true, n);
            LogMessage log;
            log.type = "#SPJiliAdd";
            log.from = p;
            log.to << use.from;
            log.arg = objectName();
            log.arg2 = use.card->objectName();
            room->sendLog(log);
            room->notifySkillInvoked(p, objectName());
            room->broadcastSkillInvoke(objectName(), n);
            room->doAnimate(1, use.from->objectName(), p->objectName());

            use.to.append(p);
            room->sortByActionOrder(use.to);
            data = QVariant::fromValue(use);
            room->getThread()->trigger(TargetConfirming, room, p, data);
        }
        return false;
    }
};

LianzhuCard::LianzhuCard()
{
    will_throw = false;
    handling_method = Card::MethodNone;
}

void LianzhuCard::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    int id = getSubcards().first();
    room->showCard(effect.from, id);
    CardMoveReason reason(CardMoveReason::S_REASON_GIVE, effect.from->objectName(), effect.to->objectName(), "lianzhu", QString());
    room->obtainCard(effect.to, this, reason, true);
    if (effect.to->isDead()) return;
    if (!Sanguosha->getCard(id)->isBlack()) return;
    effect.to->tag["LianzhuFrom"] = QVariant::fromValue(effect.from);
    bool dis = room->askForDiscard(effect.to, "lianzhu", 2, 2, true, true, "lianzhu-discard:" + effect.from->objectName());
    effect.to->tag.remove("LianzhuFrom");
    if (dis) return;
    effect.from->drawCards(2, "lianzhu");
}

class Lianzhu : public OneCardViewAsSkill
{
public:
    Lianzhu() : OneCardViewAsSkill("lianzhu")
    {
        filter_pattern = ".";
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return !player->hasUsed("LianzhuCard");
    }

    const Card *viewAs(const Card *originalCard) const
    {
        LianzhuCard *card = new LianzhuCard;
        card->addSubcard(originalCard);
        return card;
    }
};

class Xiahui : public TriggerSkill
{
public:
    Xiahui() : TriggerSkill("xiahui")
    {
        events << EventPhaseProceeding << CardsMoveOneTime;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventPhaseProceeding) {
            if (player->getPhase() != Player::Discard) return false;
            QList<int> blacks;
            foreach (int id, player->handCards()) {
                if (Sanguosha->getCard(id)->isBlack())
                    blacks << id;
            }
            if (blacks.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(player, "xiahui", true, true);
            room->ignoreCards(player, blacks);
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from && move.from == player && player->isAlive() &&
                    (move.from_places.contains(Player::PlaceHand) || move.from_places.contains(Player::PlaceEquip))) {
                if (move.to && move.to->isAlive() && move.to_place == Player::PlaceHand) {
                    ServerPlayer *to = room->findPlayerByObjectName(move.to->objectName());
                    if (!to || to->isDead()) return false;
                    QString string = "xiahui_limited" + player->objectName();
                    QVariantList limited = to->tag[string].toList();
                    for (int i = 0; i < move.card_ids.length(); i++) {
                        if (!Sanguosha->getCard(move.card_ids.at(i))->isBlack()) continue;
                        if (move.from_places.at(i) == Player::PlaceHand || move.from_places.at(i) == Player::PlaceEquip) {
                            if (!limited.contains(QVariant(move.card_ids.at(i))))
                                limited << move.card_ids.at(i);
                        }
                    }
                    if (limited.isEmpty()) return false;
                    room->sendCompulsoryTriggerLog(player, objectName(), true, true);
                    to->tag[string] = limited;

                    QList<int> limit_ids = VariantList2IntList(limited);
                    foreach (int id, limit_ids) {
                        room->setPlayerCardLimitation(to, "use,response,discard", QString::number(id), false);
                    }
                }
            }
        }
        return false;
    }
};

class XiahuiClear : public TriggerSkill
{
public:
    XiahuiClear() : TriggerSkill("#xiahui-clear")
    {
        events << EventLoseSkill << HpChanged << Death;
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == EventLoseSkill) {
            if (player->isDead() || data.toString() != "xiahui") return false;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                QString string = "xiahui_limited" + player->objectName();
                QVariantList limited = p->tag[string].toList();
                if (limited.isEmpty()) continue;
                QList<int> limit_ids = VariantList2IntList(limited);
                p->tag.remove(string);

                foreach (int id, limit_ids) {
                    room->removePlayerCardLimitation(p, "use,response,discard", QString::number(id) + "$0");
                }
            }
        } else if (event == HpChanged) {
            if (player->isDead() || data.isNull() || data.canConvert<RecoverStruct>()) return false;
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                QString string = "xiahui_limited" + p->objectName();
                QVariantList limited = player->tag[string].toList();
                if (limited.isEmpty()) continue;
                QList<int> limit_ids = VariantList2IntList(limited);
                player->tag.remove(string);

                foreach (int id, limit_ids) {
                    room->removePlayerCardLimitation(player, "use,response,discard", QString::number(id) + "$0");
                }
            }
        } else {
            DeathStruct death = data.value<DeathStruct>();
            if (death.who == player && player->hasSkill("xiahui")) {
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    QString string = "xiahui_limited" + player->objectName();
                    QVariantList limited = p->tag[string].toList();
                    if (limited.isEmpty()) continue;
                    QList<int> limit_ids = VariantList2IntList(limited);
                    p->tag.remove(string);

                    foreach (int id, limit_ids) {
                        room->removePlayerCardLimitation(p, "use,response,discard", QString::number(id) + "$0");
                    }
                }
            } else {
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    QString string = "xiahui_limited" + p->objectName();
                    QVariantList limited = player->tag[string].toList();
                    if (limited.isEmpty()) continue;
                    QList<int> limit_ids = VariantList2IntList(limited);
                    player->tag.remove(string);

                    foreach (int id, limit_ids) {
                        room->removePlayerCardLimitation(player, "use,response,discard", QString::number(id) + "$0");
                    }
                }
            }
        }
        return false;
    }
};

class Fuqi : public TriggerSkill
{
public:
    Fuqi() : TriggerSkill("fuqi")
    {
        events << CardUsed;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("SkillCard")) return false;

        QList<ServerPlayer *> tos;
        foreach (ServerPlayer *p, room->getOtherPlayers(use.from)) {
            if (p->distanceTo(use.from) != 1) continue;
            tos << p;
            use.no_respond_list << p->objectName();
            room->setCardFlag(use.card, "no_respond_" + use.from->objectName() + p->objectName());
        }
        if (tos.isEmpty()) return false;

        LogMessage log;
        log.type = "#FuqiNoResponse";
        log.from = use.from;
        log.arg = objectName();
        log.card_str = use.card->toString();
        log.to = tos;
        room->sendLog(log);
        room->notifySkillInvoked(use.from, objectName());
        room->broadcastSkillInvoke(objectName());

        data = QVariant::fromValue(use);
        return false;
    }
};

class Jiaozi : public TriggerSkill
{
public:
    Jiaozi() : TriggerSkill("jiaozi")
    {
        events << DamageCaused << DamageInflicted;
        frequency = Compulsory;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        int hand = player->getHandcardNum();
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->getHandcardNum() >= hand && p != player)
                return false;
        }

        LogMessage log;
        log.from = player;
        log.arg = objectName();
        if (event == DamageCaused) {
            if (damage.to->isDead()) return false;
            log.type = "#JiaoziDoDamage";
            log.to << damage.to;
        } else {
            log.type = "#JiaoziSufferDamage";
        }
        log.arg2 = QString::number(++damage.damage);
        room->sendLog(log);
        room->notifySkillInvoked(player, objectName());
        room->broadcastSkillInvoke(objectName());

        data = QVariant::fromValue(damage);
        return false;
    }
};

class Zongkui : public TriggerSkill
{
public:
    Zongkui() : TriggerSkill("zongkui")
    {
        events << EventPhaseStart << RoundStart;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &) const
    {
        if (event == EventPhaseStart) {
            if (player->getPhase() != Player::RoundStart) return false;
            QList<ServerPlayer *> players;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getMark("&kui") <= 0)
                    players << p;
            }
            if (players.isEmpty()) return false;
            ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@zongkui-invoke", true, true);
            if (!target) return false;
            room->broadcastSkillInvoke(objectName());
            target->gainMark("&kui");
        } else {
            int hp = player->getHp();
            QList<ServerPlayer *> players;
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHp() < hp)
                    hp = p->getHp();
            }
            foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                if (p->getHp() == hp && p->getMark("&kui") <= 0)
                    players << p;
            }
            if (players.isEmpty()) return false;
            room->sendCompulsoryTriggerLog(player, objectName(), true, true);
            ServerPlayer *target = room->askForPlayerChosen(player, players, objectName(), "@zongkui-trigger");
            room->doAnimate(1, player->objectName(), target->objectName());
            target->gainMark("&kui");
        }
        return false;
    }
};

class Guju : public MasochismSkill
{
public:
    Guju() : MasochismSkill("guju")
    {
        frequency = Compulsory;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    void onDamaged(ServerPlayer *player, const DamageStruct &) const
    {
        if (player->getMark("&kui") <= 0) return;
        Room *room = player->getRoom();
        foreach (ServerPlayer *p, room->getAllPlayers()) {
            if (p->isDead() || !p->hasSkill(this)) continue;
            room->sendCompulsoryTriggerLog(p, objectName(), true, true);
            p->drawCards(1, objectName());
        }
    }
};

class Baijia : public TriggerSkill
{
public:
    Baijia() : TriggerSkill("baijia")
    {
        events << CardsMoveOneTime << EventPhaseStart;
        frequency = Wake;
        global = true;
    }

    bool trigger(TriggerEvent event, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (event == CardsMoveOneTime) {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.to && move.to->isAlive() && move.to == player && move.to_place == Player::PlaceHand && move.reason.m_skillName == "guju") {
                if (player->hasSkill(this, true))
                    room->addPlayerMark(player, "&baijia_num", move.card_ids.length());
                else
                    room->addPlayerMark(player, "baijia_num", move.card_ids.length());
            }
        } else {
            if (!player->hasSkill(this) || player->getPhase() != Player::Start) return false;
            int mark = player->getMark("&baijia_num") + player->getMark("baijia_num");
            if (player->getMark(objectName()) > 0 || mark < 7) return false;
            LogMessage log;
            log.type = "#BaijiaWake";
            log.from = player;
            log.arg = objectName();
            log.arg2 = QString::number(mark);
            room->sendLog(log);
            room->notifySkillInvoked(player, objectName());
            room->broadcastSkillInvoke(objectName());
            room->doSuperLightbox("beimihu", objectName());
            room->addPlayerMark(player, objectName());
            if (room->changeMaxHpForAwakenSkill(player, 1)) {
                room->recover(player, RecoverStruct(player));
                foreach (ServerPlayer *p, room->getOtherPlayers(player)) {
                    if (p->getMark("&kui") <= 0)
                        p->gainMark("&kui");
                }
                QStringList skills;
                if (player->hasSkill("guju"))
                    skills << "-guju";
                if (!player->hasSkill("spcanshi"))
                    skills << "spcanshi";
                if (skills.isEmpty()) return false;
                room->handleAcquireDetachSkills(player, skills);
            }
        }
        return false;
    }
};

SpCanshiCard::SpCanshiCard()
{
}

bool SpCanshiCard::targetFilter(const QList<const Player *> &, const Player *to_select, const Player *Self) const
{
    QStringList names = Self->property("spcanshi_ava").toString().split("+");
    return to_select->getMark("&kui") > 0 && names.contains(to_select->objectName());
}

void SpCanshiCard::onUse(Room *room, const CardUseStruct &card_use) const
{
    foreach (ServerPlayer *p, card_use.to) {
        room->doAnimate(1, card_use.from->objectName(), p->objectName());
        room->setPlayerFlag(p, "spcanshi_extra");
    }
}

class SpCanshiVS : public ZeroCardViewAsSkill
{
public:
    SpCanshiVS() : ZeroCardViewAsSkill("spcanshi")
    {
        response_pattern = "@@spcanshi";
    }

    bool isEnabledAtPlay(const Player *) const
    {
        return false;
    }

    const Card *viewAs() const
    {
        return new SpCanshiCard;
    }
};

class SpCanshi : public TriggerSkill
{
public:
    SpCanshi() : TriggerSkill("spcanshi")
    {
        events << TargetSpecifying;
        view_as_skill = new SpCanshiVS;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        CardUseStruct use = data.value<CardUseStruct>();
        if (!use.card->isKindOf("BasicCard") && !use.card->isNDTrick()) return false;
        if (use.card->isKindOf("Collateral")) return false;
        if (use.to.length() != 1) return false;
        if (use.from->hasSkill(this)) {
            QStringList ava;
            room->setCardFlag(use.card, "spcanshi_distance");
            foreach (ServerPlayer *p, room->getAlivePlayers()) {
                if ((use.card->isKindOf("AOE") && p == use.from) || p->getMark("&kui") <= 0) continue;
                if (use.to.contains(p) || room->isProhibited(use.from, p, use.card)) continue;
                if (use.card->targetFixed()) {
                    if (!use.card->isKindOf("Peach") || p->isWounded())
                        ava << p->objectName();
                } else {
                    if (use.card->targetFilter(QList<const Player *>(), p, use.from))
                        ava << p->objectName();
                }
            }
            room->setCardFlag(use.card, "-spcanshi_distance");
            if (ava.isEmpty()) return false;
            QString names = ava.join("+");
            room->setPlayerProperty(player, "spcanshi_ava", names);
            player->tag["SPCanshiForAI"] = data;
            if (!room->askForUseCard(player, "@@spcanshi", "@spcanshi:" + use.card->objectName())) {
                room->setPlayerProperty(player, "spcanshi_ava", QString());
                player->tag.remove("SPCanshiForAI");
            } else {
                room->setPlayerProperty(player, "spcanshi_ava", QString());
                player->tag.remove("SPCanshiForAI");
                QList<ServerPlayer *> tos;
                foreach (ServerPlayer *p, room->getAlivePlayers()) {
                    if (p->hasFlag("spcanshi_extra")) {
                        room->setPlayerFlag(p, "-spcanshi_extra");
                        use.to.append(p);
                        tos << p;
                    }
                }
                if (tos.isEmpty()) return false;
                room->sortByActionOrder(tos);
                room->sortByActionOrder(use.to);
                data = QVariant::fromValue(use);
                LogMessage log;
                log.type = "#QiaoshuiAdd";
                log.from = player;
                log.to = tos;
                log.card_str = use.card->toString();
                log.arg = objectName();
                room->sendLog(log);
                room->broadcastSkillInvoke(objectName());
                room->notifySkillInvoked(player, objectName());
                foreach (ServerPlayer *p, tos) {
                    if (p->getMark("&kui") > 0)
                        p->loseAllMarks("&kui");
                }
            }
        } else {
            if (!use.to.first()->hasSkill(this) || use.from->getMark("&kui") <= 0) return false;
            use.to.first()->tag["SPCanshi"] = data;
            bool invoke = use.to.first()->askForSkillInvoke(this, QVariant::fromValue(use.from));
            use.to.first()->tag.remove("SPCanshi");
            if (!invoke) return false;
            room->broadcastSkillInvoke(objectName());
            use.to.removeOne(use.to.first());
            data = QVariant::fromValue(use);
            use.from->loseAllMarks("&kui");
        }
        return false;
    }
};

class SpCanshiMod : public TargetModSkill
{
public:
    SpCanshiMod() : TargetModSkill("#spcanshi-target")
    {
        frequency = NotFrequent;
        pattern = ".";
    }

    int getDistanceLimit(const Player *from, const Card *card, const Player *) const
    {
        if (card->hasFlag("spcanshi_distance") && from->hasSkill("spcanshi"))
            return 1000;
        else
            return 0;
    }
};

SPPackage::SPPackage()
: Package("sp")
{
    General *yangxiu = new General(this, "yangxiu", "wei", 3); // SP 001
    yangxiu->addSkill(new Jilei);
    yangxiu->addSkill(new JileiClear);
    yangxiu->addSkill(new Danlao);
    related_skills.insertMulti("jilei", "#jilei-clear");

    General *sp_diaochan = new General(this, "sp_diaochan", "qun", 3, false, true); // SP 002
    sp_diaochan->addSkill("noslijian");
    sp_diaochan->addSkill("biyue");

    General *gongsunzan = new General(this, "gongsunzan", "qun"); // SP 003
    gongsunzan->addSkill(new Yicong);
    gongsunzan->addSkill(new YicongEffect);
    related_skills.insertMulti("yicong", "#yicong-effect");

    General *ol_sp_gongsunzan = new General(this, "ol_sp_gongsunzan", "qun");
    ol_sp_gongsunzan->addSkill("olyicong");

    General *yuanshu = new General(this, "yuanshu", "qun"); // SP 004
    yuanshu->addSkill(new Yongsi);
    yuanshu->addSkill(new Weidi);

    General *sp_sunshangxiang = new General(this, "sp_sunshangxiang", "shu", 3, false, true); // SP 005
    sp_sunshangxiang->addSkill("jieyin");
    sp_sunshangxiang->addSkill("xiaoji");

    General *sp_pangde = new General(this, "sp_pangde", "wei", 4, true, true); // SP 006
    sp_pangde->addSkill("mashu");
    sp_pangde->addSkill("mengjin");

    General *sp_guanyu = new General(this, "sp_guanyu", "wei", 4); // SP 007
    sp_guanyu->addSkill("wusheng");
    sp_guanyu->addSkill(new Danji);

    General *shenlvbu1 = new General(this, "shenlvbu1", "god", 8, true, true); // SP 008 (2-1)
    shenlvbu1->addSkill("mashu");
    shenlvbu1->addSkill("wushuang");

    General *shenlvbu2 = new General(this, "shenlvbu2", "god", 4, true, true); // SP 008 (2-2)
    shenlvbu2->addSkill("mashu");
    shenlvbu2->addSkill("wushuang");
    shenlvbu2->addSkill(new Xiuluo);
    shenlvbu2->addSkill(new ShenweiKeep);
    shenlvbu2->addSkill(new Shenwei);
    shenlvbu2->addSkill(new Shenji);
    related_skills.insertMulti("shenwei", "#shenwei-draw");

    General *sp_caiwenji = new General(this, "sp_caiwenji", "wei", 3, false, true); // SP 009
    sp_caiwenji->addSkill("beige");
    sp_caiwenji->addSkill("duanchang");

    General *sp_machao = new General(this, "sp_machao", "qun", 4, true, true); // SP 011
    sp_machao->addSkill("mashu");
    sp_machao->addSkill("nostieji");

    General *sp_jiaxu = new General(this, "sp_jiaxu", "wei", 3, true, true); // SP 012
    sp_jiaxu->addSkill("wansha");
    sp_jiaxu->addSkill("luanwu");
    sp_jiaxu->addSkill("weimu");

    General *caohong = new General(this, "caohong", "wei"); // SP 013
    caohong->addSkill(new Yuanhu);

    General *guanyinping = new General(this, "guanyinping", "shu", 3, false); // SP 014
    guanyinping->addSkill(new Xueji);
    guanyinping->addSkill(new Huxiao);
    guanyinping->addSkill(new HuxiaoCount);
    guanyinping->addSkill(new HuxiaoClear);
    guanyinping->addSkill(new Wuji);
    related_skills.insertMulti("huxiao", "#huxiao-count");
    related_skills.insertMulti("huxiao", "#huxiao-clear");

    General *sp_zhenji = new General(this, "sp_zhenji", "wei", 3, false, true); // SP 015
    sp_zhenji->addSkill("qingguo");
    sp_zhenji->addSkill("luoshen");

    General *liuxie = new General(this, "liuxie", "qun", 3);
    liuxie->addSkill("tianming");
    liuxie->addSkill("mizhao");

    General *lingju = new General(this, "lingju", "qun", 3, false);
    lingju->addSkill("jieyuan");
    lingju->addSkill("fenxin");

    General *fuwan = new General(this, "fuwan", "qun", 4);
    fuwan->addSkill("moukui");

    General *xiahouba = new General(this, "xiahouba", "shu"); // SP 019
    xiahouba->addSkill(new Baobian);

    General *chenlin = new General(this, "chenlin", "wei", 3); // SP 020
    chenlin->addSkill(new Bifa);
    chenlin->addSkill(new Songci);

    General *erqiao = new General(this, "erqiao", "wu", 3, false); // SP 021
    erqiao->addSkill(new Xingwu);
    erqiao->addSkill(new Luoyan);

    General *sp_shenlvbu = new General(this, "sp_shenlvbu", "god", 5, true, true); // SP 022
    sp_shenlvbu->addSkill("kuangbao");
    sp_shenlvbu->addSkill("wumou");
    sp_shenlvbu->addSkill("wuqian");
    sp_shenlvbu->addSkill("shenfen");

    General *xiahoushi = new General(this, "xiahoushi", "shu", 3, false); // SP 023
    xiahoushi->addSkill(new Yanyu);
    xiahoushi->addSkill(new Xiaode);
    xiahoushi->addSkill(new XiaodeEx);
    related_skills.insertMulti("xiaode", "#xiaode");

    General *sp_yuejin = new General(this, "sp_yuejin", "wei", 4, true); // SP 024
    sp_yuejin->addSkill("xiaoguo");

    General *zhangbao = new General(this, "zhangbao", "qun", 3); // SP 025
    zhangbao->addSkill(new Zhoufu);
    zhangbao->addSkill(new Yingbing);

    General *caoang = new General(this, "caoang", "wei"); // SP 026
    caoang->addSkill(new Kangkai);

    General *sp_zhugejin = new General(this, "sp_zhugejin", "wu", 3, true, true); // SP 027
    sp_zhugejin->addSkill("hongyuan");
    sp_zhugejin->addSkill("huanshi");
    sp_zhugejin->addSkill("mingzhe");

    General *xingcai = new General(this, "xingcai", "shu", 3, false); // SP 028
    xingcai->addSkill(new Shenxian);
    xingcai->addSkill(new Qiangwu);
    xingcai->addSkill(new QiangwuTargetMod);
    related_skills.insertMulti("qiangwu", "#qiangwu-target");

    General *sp_panfeng = new General(this, "sp_panfeng", "qun", 4, true); // SP 029
    sp_panfeng->addSkill("kuangfu");

    General *zumao = new General(this, "zumao", "wu"); // SP 030
    zumao->addSkill(new Yinbing);
    zumao->addSkill(new Juedi);

    General *sp_dingfeng = new General(this, "sp_dingfeng", "wu", 4, true); // SP 031
    sp_dingfeng->addSkill("duanbing");
    sp_dingfeng->addSkill("fenxun");

    General *zhugedan = new General(this, "zhugedan", "wei", 4); // SP 032
    zhugedan->addSkill(new Gongao);
    zhugedan->addSkill(new Juyi);
    zhugedan->addRelateSkill("weizhong");

    General *sp_hetaihou = new General(this, "sp_hetaihou", "qun", 3, false); // SP 033
    sp_hetaihou->addSkill("zhendu");
    sp_hetaihou->addSkill("qiluan");

    General *sunluyu = new General(this, "sunluyu", "wu", 3, false); // SP 034
    sunluyu->addSkill(new Meibu);
    sunluyu->addSkill(new Mumu);

    General *maliang = new General(this, "maliang", "shu", 3); // SP 035
    maliang->addSkill(new Xiemu);
    maliang->addSkill(new Naman);

    General *chengyu = new General(this, "chengyu", "wei", 3);
    chengyu->addSkill(new Shefu);
    chengyu->addSkill(new ShefuCancel);
    chengyu->addSkill(new Benyu);
    related_skills.insertMulti("shefu", "#shefu-cancel");

    General *sp_ganfuren = new General(this, "sp_ganfuren", "shu", 3, false); // SP 037
    sp_ganfuren->addSkill("shushen");
    sp_ganfuren->addSkill("shenzhi");

    General *huangjinleishi = new General(this, "huangjinleishi", "qun", 3, false); // SP 038
    huangjinleishi->addSkill(new Fulu);
    huangjinleishi->addSkill(new Zhuji);

    General *sp_wenpin = new General(this, "sp_wenpin", "wei"); // SP 039
    sp_wenpin->addSkill(new SpZhenwei);

    General *simalang = new General(this, "simalang", "wei", 3); // SP 040
    simalang->addSkill(new Quji);
    simalang->addSkill(new Junbing);

    General *sunhao = new General(this, "sunhao$", "wu", 5); // SP 041, SE god god god god god god god god god god god god god god god god god god god god god god god god god god god god god god god god
    sunhao->addSkill(new Canshi);
    sunhao->addSkill(new Chouhai);
    sunhao->addSkill(new Guiming);

    General *ol_sunhao = new General(this, "ol_sunhao$", "wu", 5);
    ol_sunhao->addSkill("tenyearcanshi");
    ol_sunhao->addSkill("chouhai");
    ol_sunhao->addSkill("guiming");

    General *guansuo = new General(this, "guansuo", "shu", 4);
    guansuo->addSkill(new Xiefang);
    guansuo->addSkill(new Zhengnan);

    General *ol_guansuo = new General(this, "ol_guansuo", "shu", 4);
    ol_guansuo->addSkill("xiefang");
    ol_guansuo->addSkill(new OLZhengnan);

    General *tenyear_guansuo = new General(this, "tenyear_guansuo", "shu", 4);
    tenyear_guansuo->addSkill("xiefang");
    tenyear_guansuo->addSkill(new TenyearZhengnan);

    General *zhaoxiang = new General(this, "zhaoxiang", "shu", 4, false);
    zhaoxiang->addSkill(new Fanghun);
    zhaoxiang->addSkill(new Fuhan);

    General *ol_zhaoxiang = new General(this, "ol_zhaoxiang", "shu", 4, false);
    ol_zhaoxiang->addSkill(new OLFanghun);
    ol_zhaoxiang->addSkill(new OLFuhan);

    General *mobile_zhaoxiang = new General(this, "mobile_zhaoxiang", "shu", 4, false);
    mobile_zhaoxiang->addSkill(new MobileFanghun);
    mobile_zhaoxiang->addSkill(new MobileFuhan);

    General *baosanniang = new General(this, "baosanniang", "shu", 3, false);
    baosanniang->addSkill(new Wuniang);
    baosanniang->addSkill(new Xushen);
    baosanniang->addRelateSkill("zhennan");

    General *mobile_baosanniang = new General(this, "mobile_baosanniang", "shu", 3, false);
    mobile_baosanniang->addSkill(new Shuyong);
    mobile_baosanniang->addSkill(new MobileXushen);
    mobile_baosanniang->addSkill(new MoboleZhennan);

    General *tenyear_baosanniang = new General(this, "tenyear_baosanniang", "shu", 3, false);
    tenyear_baosanniang->addSkill("wuniang");
    tenyear_baosanniang->addSkill(new TenyearXushen);
    tenyear_baosanniang->addRelateSkill("tenyearzhennan");

    General *second_tenyear_baosanniang = new General(this, "second_tenyear_baosanniang", "shu", 3, false);
    second_tenyear_baosanniang->addSkill(new SecondWuniang);
    second_tenyear_baosanniang->addSkill(new SecondXushen);
    second_tenyear_baosanniang->addRelateSkill("secondzhennan");

    General *xurong = new General(this, "xurong", "qun", 4);
    xurong->addSkill(new Shajue);
    xurong->addSkill(new Xionghuo);
    xurong->addSkill(new XionghuoMark);
    xurong->addSkill(new XionghuoPro);
    related_skills.insertMulti("xionghuo", "#xionghuomark");
    related_skills.insertMulti("xionghuo", "#xionghuopro");

    General *caoying = new General(this, "caoying", "wei", 4, false);
    caoying->addSkill(new Lingren);
    caoying->addSkill(new LingrenEffect);
    caoying->addSkill(new Fujian);
    related_skills.insertMulti("lingren", "#lingreneffect");

    General *caochun = new General(this, "caochun", "wei", 4);
    caochun->addSkill(new Shanjia);

    General *zhangqiying = new General(this, "zhangqiying", "qun", 3, false);
    zhangqiying->addSkill(new Falu);
    zhangqiying->addSkill(new Zhenyi);
    zhangqiying->addSkill(new Dianhua);

    General *tenyear_zhangqiying = new General(this, "tenyear_zhangqiying", "qun", 3, false);
    tenyear_zhangqiying->addSkill("falu");
    tenyear_zhangqiying->addSkill(new TenyearZhenyi);
    tenyear_zhangqiying->addSkill("dianhua");

    General *xizhicai = new General(this, "xizhicai", "wei", 3);
    xizhicai->addSkill("tiandu");
    xizhicai->addSkill(new Xianfu);
    xizhicai->addSkill(new XianfuTarget);
    xizhicai->addSkill(new Chouce);
    related_skills.insertMulti("xianfu", "#xianfu-target");

    General *sunqian = new General(this, "sunqian", "shu", 3);
    sunqian->addSkill(new Qianya);
    sunqian->addSkill(new Shuomeng);

    General *litong = new General(this, "litong", "wei", 4);
    litong->addSkill(new Tuifeng);

    General *duji = new General(this, "duji", "wei", 3);
    duji->addSkill(new Andong);
    duji->addSkill(new Yingshi);
    duji->addSkill(new YingshiDeath);
    related_skills.insertMulti("yingshi", "#yingshi-death");

    General *lvqian = new General(this, "lvqian", "wei", 4);
    lvqian->addSkill(new Weilu);
    lvqian->addSkill(new Zengdao);

    General *wanglang = new General(this, "wanglang", "wei", 3);
    wanglang->addSkill(new Gushe);
    wanglang->addSkill(new Jici);

    General *sp_luzhi = new General(this, "sp_luzhi", "wei", 3);
    sp_luzhi->addSkill(new Qingzhong);
    sp_luzhi->addSkill(new Weijing);

    General *wenyang = new General(this, "wenyang", "wei", 5);
    wenyang->addSkill(new Lvli);
    wenyang->addSkill(new Choujue);
    wenyang->addRelateSkill("beishui");
    wenyang->addRelateSkill("qingjiao");

    General *jianggan = new General(this, "jianggan", "wei", 3);
    jianggan->addSkill(new Weicheng);
    jianggan->addSkill(new Daoshu);

    General *tangzi = new General(this, "tangzi", "wei", 4);
    tangzi->addSkill(new Xingzhao);
    tangzi->addSkill(new XingzhaoXunxun);
    tangzi->addRelateSkill("xunxun");
    related_skills.insertMulti("xingzhao", "#xingzhao-xunxun");

    General *simazhao = new General(this, "simazhao", "wei", 3);
    simazhao->addSkill(new Daigong);
    simazhao->addSkill(new SpZhaoxin);

    General *jiakui = new General(this, "jiakui", "wei", 3);
    jiakui->addSkill(new Zhongzuo);
    jiakui->addSkill(new ZhongzuoRecord);
    jiakui->addSkill(new Wanlan);
    jiakui->addSkill(new WanlanDamage);
    related_skills.insertMulti("zhongzuo", "#zhongzuo-record");
    related_skills.insertMulti("wanlan", "#wanlan-damage");

    General *new_jiakui = new General(this, "new_jiakui", "wei", 4);
    new_jiakui->addSkill(new Tongqu);
    new_jiakui->addSkill(new TongquTrigger);
    new_jiakui->addSkill(new NewWanlan);
    related_skills.insertMulti("tongqu", "#tongqu-trigger");

    General *wangyuanji = new General(this, "wangyuanji", "wei", 3, false);
    wangyuanji->addSkill(new Qianchong);
    wangyuanji->addSkill(new QianchongTargetMod);
    wangyuanji->addSkill(new QianchongLose);
    wangyuanji->addSkill(new Shangjian);
    wangyuanji->addSkill(new ShangjianMark);
    related_skills.insertMulti("qianchong", "#qianchong-target");
    related_skills.insertMulti("qianchong", "#qianchong-lose");
    related_skills.insertMulti("shangjian", "#shangjian-mark");

    General *xinpi = new General(this, "xinpi", "wei", 3);
    xinpi->addSkill(new Chijie);
    xinpi->addSkill(new Yinju);

    General *wangshuang = new General(this, "wangshuang", "wei", 8);
    wangshuang->addSkill(new Zhuilie);
    wangshuang->addSkill(new ZhuilieSlash);
    related_skills.insertMulti("zhuilie", "#zhuilie-slash");

    General *guanlu = new General(this, "guanlu", "wei", 3);
    guanlu->addSkill(new Tuiyan);
    guanlu->addSkill(new Busuan);
    guanlu->addSkill(new Mingjie);

    General *zhanggong = new General(this, "zhanggong", "wei", 3);
    zhanggong->addSkill(new SpQianxin);
    zhanggong->addSkill(new Zhenxing);

    General *mobile_zhanggong = new General(this, "mobile_zhanggong", "wei", 3);
    mobile_zhanggong->addSkill(new MobileSpQianxin);
    mobile_zhanggong->addSkill(new MobileSpQianxinMove);
    mobile_zhanggong->addSkill(new MobileZhenxing);
    related_skills.insertMulti("mobilespqianxin", "#mobilespqianxin-move");

    General *sp_yiji = new General(this, "sp_yiji", "shu", 3);
    sp_yiji->addSkill(new Jijie);
    sp_yiji->addSkill(new Jiyuan);

    General *mizhu = new General(this, "mizhu", "shu", 3);
    mizhu->addSkill(new Ziyuan);
    mizhu->addSkill(new Jugu);
    mizhu->addSkill(new JuguMax);
    related_skills.insertMulti("jugu", "#jugu-max");

    General *dongyun = new General(this, "dongyun", "shu", 3);
    dongyun->addSkill(new Bingzheng);
    dongyun->addSkill(new Sheyan);
    dongyun->addSkill(new SheyanTargetMod);
    related_skills.insertMulti("sheyan", "#sheyan-target");

    General *mazhong = new General(this, "mazhong", "shu", 4);
    mazhong->addSkill(new Fuman);

    General *lvkai = new General(this, "lvkai", "shu", 3);
    lvkai->addSkill(new Tunan);
    lvkai->addSkill(new TunanTargetMod);
    lvkai->addSkill(new Bijing);
    related_skills.insertMulti("tunan", "#tunan-target");

    General *huangquan = new General(this, "huangquan", "shu", 3);
    huangquan->addSkill(new Dianhu);
    huangquan->addSkill(new DianhuTarget);
    huangquan->addSkill(new Jianji);
    related_skills.insertMulti("dianhu", "#dianhu-target");

    General *shamoke = new General(this, "shamoke", "shu", 4);
    shamoke->addSkill(new Jili);

    General *zhaotongzhaoguang = new General(this, "zhaotongzhaoguang", "shu", 4);
    zhaotongzhaoguang->addSkill(new Yizan);
    zhaotongzhaoguang->addSkill(new Longyuan);

    General *hujinding = new General(this, "hujinding", "shu", 6, false, false, false, 2);
    hujinding->addSkill(new Renshi);
    hujinding->addSkill(new Wuyuan);
    hujinding->addSkill(new Huaizi);

    General *xujing = new General(this, "xujing", "shu", 3);
    xujing->addSkill(new Yuxu);
    xujing->addSkill(new Shijian);

    General *huaman = new General(this, "huaman", "shu", 3, false);
    huaman->addSkill(new SpManyi);
    huaman->addSkill(new Mansi);
    huaman->addSkill(new Souying);
    huaman->addSkill(new Zhanyuan);
    huaman->addRelateSkill("xili");

    General *second_huaman = new General(this, "second_huaman", "shu", 3, false);
    second_huaman->addSkill("spmanyi");
    second_huaman->addSkill(new SecondMansi);
    second_huaman->addSkill(new SecondSouying);
    second_huaman->addSkill(new SecondSouyingRecord);
    second_huaman->addSkill(new SecondZhanyuan);
    second_huaman->addRelateSkill("secondxili");
    related_skills.insertMulti("dingpan", "#secondsouying-record");

    General *buzhi = new General(this, "buzhi", "wu", 3);
    buzhi->addSkill(new Hongde);
    buzhi->addSkill(new Dingpan);
    buzhi->addSkill(new DingpanRevived);
    related_skills.insertMulti("dingpan", "#dingpan-revived");

    General *heqi = new General(this, "heqi", "wu", 4);
    heqi->addSkill(new Qizhou);
    heqi->addSkill(new QizhouLose);
    heqi->addSkill(new Shanxi);
    related_skills.insertMulti("qizhou", "#qizhou-lose");

    General *kanze = new General(this, "kanze", "wu", 3);
    kanze->addSkill(new Xiashu);
    kanze->addSkill(new Kuanshi);
    kanze->addSkill(new KuanshiEffect);
    related_skills.insertMulti("kuanshi", "#kuanshi-effect");

    General *sp_pangtong = new General(this, "sp_pangtong", "wu", 3);
    sp_pangtong->addSkill(new Guolun);
    sp_pangtong->addSkill(new Songsang);
    sp_pangtong->addRelateSkill("zhanji");

    General *sunshao = new General(this, "sunshao", "wu", 3);
    sunshao->addSkill(new Bizheng);
    sunshao->addSkill(new Yidian);
    sunshao->addSkill(new YidianTargetMod);
    related_skills.insertMulti("yidian", "#yidian-target");

    General *sufei = new General(this, "sufei", "wu", 4);
    sufei->addSkill(new Lianpian);

    General *yanjun = new General(this, "yanjun", "wu", 3);
    yanjun->addSkill(new Guanchao);
    yanjun->addSkill(new Xunxian);

    General *zhoufang = new General(this, "zhoufang", "wu", 3);
    zhoufang->addSkill(new SpYoudi);
    zhoufang->addSkill(new Duanfa);

    General *lvdai = new General(this, "lvdai", "wu", 4);
    lvdai->addSkill(new Qinguo);

    General *gexuan = new General(this, "gexuan", "wu", 3);
    gexuan->addSkill(new Lianhua);
    gexuan->addSkill(new LianhuaEffect);
    gexuan->addSkill(new Zhafu);
    related_skills.insertMulti("lianhua", "#lianhua-effect");

    General *zhangwen = new General(this, "zhangwen", "wu", 3);
    zhangwen->addSkill(new Songshu);
    zhangwen->addSkill(new Sibian);

    General *xugong = new General(this, "xugong", "wu", 3);
    xugong->addSkill(new Biaozhao);
    xugong->addSkill(new Yechou);

    General *panjun = new General(this, "panjun", "wu", 3);
    panjun->addSkill(new Guanwei);
    panjun->addSkill(new Gongqing);

    General *weiwenzhugezhi = new General(this, "weiwenzhugezhi", "wu", 4);
    weiwenzhugezhi->addSkill(new Fuhai);

    General *mobile_weiwenzhugezhi = new General(this, "mobile_weiwenzhugezhi", "wu", 4);
    mobile_weiwenzhugezhi->addSkill(new MobileFuhai);

    General *yanbaihu = new General(this, "yanbaihu", "qun", 4);
    yanbaihu->addSkill(new Zhidao);
    yanbaihu->addSkill(new ZhidaoPro);
    yanbaihu->addSkill(new SpJili);
    related_skills.insertMulti("zhidao", "#zhidao-pro");

    General *dongbai = new General(this, "dongbai", "qun", 3, false);
    dongbai->addSkill(new Lianzhu);
    dongbai->addSkill(new Xiahui);
    dongbai->addSkill(new XiahuiClear);
    related_skills.insertMulti("xiahui", "#xiahui-clear");

    General *quyi = new General(this, "quyi", "qun", 4);
    quyi->addSkill(new Fuqi);
    quyi->addSkill(new Jiaozi);

    General *beimihu = new General(this, "beimihu", "qun", 3, false);
    beimihu->addSkill(new Zongkui);
    beimihu->addSkill(new Guju);
    beimihu->addSkill(new Baijia);
    beimihu->addRelateSkill("spcanshi");
    related_skills.insertMulti("spcanshi", "#spcanshi-target");

    addMetaObject<YuanhuCard>();
    addMetaObject<XuejiCard>();
    addMetaObject<BifaCard>();
    addMetaObject<SongciCard>();
    addMetaObject<ZhoufuCard>();
    addMetaObject<QiangwuCard>();
    addMetaObject<YinbingCard>();
    addMetaObject<XiemuCard>();
    addMetaObject<ShefuCard>();
    addMetaObject<QujiCard>();
    addMetaObject<FanghunCard>();
    addMetaObject<OLFanghunCard>();
    addMetaObject<MobileFanghunCard>();
    addMetaObject<MobileXushenCard>();
    addMetaObject<XionghuoCard>();
    addMetaObject<ShanjiaCard>();
    addMetaObject<YingshiCard>();
    addMetaObject<ZengdaoCard>();
    addMetaObject<ZengdaoRemoveCard>();
    addMetaObject<GusheCard>();
    addMetaObject<DaoshuCard>();
    addMetaObject<SpZhaoxinCard>();
    addMetaObject<SpZhaoxinChooseCard>();
    addMetaObject<TongquCard>();
    addMetaObject<YinjuCard>();
    addMetaObject<BusuanCard>();
    addMetaObject<SpQianxinCard>();
    addMetaObject<MobileSpQianxinCard>();
    addMetaObject<JijieCard>();
    addMetaObject<ZiyuanCard>();
    addMetaObject<FumanCard>();
    addMetaObject<TunanCard>();
    addMetaObject<JianjiCard>();
    addMetaObject<YizanCard>();
    addMetaObject<WuyuanCard>();
    addMetaObject<SecondMansiCard>();
    addMetaObject<DingpanCard>();
    addMetaObject<ShanxiCard>();
    addMetaObject<GuolunCard>();
    addMetaObject<DuanfaCard>();
    addMetaObject<QinguoCard>();
    addMetaObject<ZhafuCard>();
    addMetaObject<SongshuCard>();
    addMetaObject<FuhaiCard>();
    addMetaObject<MobileFuhaiCard>();
    addMetaObject<LianzhuCard>();
    addMetaObject<SpCanshiCard>();

    skills << new Weizhong << new MeibuFilter("meibu") << new MeiyingMark << new Zhennan << new TenyearZhennan << new SecondZhennan << new Beishui
           << new Qingjiao << new Zhanji << new Xili << new SecondXili << new SpCanshi << new SpCanshiMod;
}

ADD_PACKAGE(SP)

MiscellaneousPackage::MiscellaneousPackage()
: Package("miscellaneous")
{
    General *wz_daqiao = new General(this, "wz_nos_daqiao", "wu", 3, false, true); // WZ 001
    wz_daqiao->addSkill("nosguose");
    wz_daqiao->addSkill("liuli");

    General *wz_xiaoqiao = new General(this, "wz_xiaoqiao", "wu", 3, false, true); // WZ 002
    wz_xiaoqiao->addSkill("tianxiang");
    wz_xiaoqiao->addSkill("hongyan");

    General *pr_shencaocao = new General(this, "pr_shencaocao", "god", 3, true, true); // PR LE 005
    pr_shencaocao->addSkill("guixin");
    pr_shencaocao->addSkill("feiying");

    General *pr_nos_simayi = new General(this, "pr_nos_simayi", "wei", 3, true, true); // PR WEI 002
    pr_nos_simayi->addSkill("nosfankui");
    pr_nos_simayi->addSkill("nosguicai");

    General *Caesar = new General(this, "caesar", "god", 4); // E.SP 001
    Caesar->addSkill(new Conqueror);

    General *hanba = new General(this, "hanba", "qun", 4, false);
    hanba->addSkill(new Fentian);
    hanba->addSkill(new Zhiri);
    hanba->addSkill(new FentianRange);
    related_skills.insertMulti("fentian", "#fentian");
    hanba->addRelateSkill("xintan");

    skills << new Xintan;

    addMetaObject<XintanCard>();
}

ADD_PACKAGE(Miscellaneous)
