#include "maneuvering.h"
#include "client.h"
#include "engine.h"
#include "general.h"
#include "room.h"
#include "wrapped-card.h"
#include "roomthread.h"

NatureSlash::NatureSlash(Suit suit, int number, DamageStruct::Nature nature)
    : Slash(suit, number)
{
    this->nature = nature;
    damage_card = true;
}

bool NatureSlash::match(const QString &pattern) const
{
    QStringList patterns = pattern.split("+");
    if (patterns.contains("slash"))
        return true;
    else
        return Slash::match(pattern);
}

ThunderSlash::ThunderSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::Thunder)
{
    setObjectName("thunder_slash");
    damage_card = true;
}

FireSlash::FireSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::Fire)
{
    setObjectName("fire_slash");
    nature = DamageStruct::Fire;
    damage_card = true;
}

IceSlash::IceSlash(Suit suit, int number)
    : NatureSlash(suit, number, DamageStruct::Ice)
{
    setObjectName("ice_slash");
    nature = DamageStruct::Ice;
    damage_card = true;
}

Analeptic::Analeptic(Card::Suit suit, int number)
    : BasicCard(suit, number)
{
    setObjectName("analeptic");
    target_fixed = true;
}

QString Analeptic::getSubtype() const
{
    return "buff_card";
}

bool Analeptic::IsAvailable(const Player *player, const Card *analeptic)
{
    Analeptic *newanaleptic = new Analeptic(Card::NoSuit, 0);
    newanaleptic->deleteLater();
#define THIS_ANALEPTIC (analeptic == NULL ? newanaleptic : analeptic)
    if (player->isCardLimited(THIS_ANALEPTIC, Card::MethodUse) || player->isProhibited(player, THIS_ANALEPTIC))
        return false;

    return player->getMark("Analeptic_used_times") <= Sanguosha->correctCardTarget(TargetModSkill::Residue, player, THIS_ANALEPTIC, player);
#undef THIS_ANALEPTIC
}

bool Analeptic::isAvailable(const Player *player) const
{
    return IsAvailable(player, this) && BasicCard::isAvailable(player);
}

void Analeptic::onUse(Room *room, const CardUseStruct &card_use) const
{
    CardUseStruct use = card_use;
    if (use.to.isEmpty())
        use.to << use.from;
    BasicCard::onUse(room, use);
}

void Analeptic::use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const
{
    room->addPlayerMark(source, "Analeptic_used_times", 1);
    if (targets.isEmpty())
        targets << source;
    BasicCard::use(room, source, targets);
}

void Analeptic::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->setEmotion(effect.to, "analeptic");

    if (effect.to->hasFlag("Global_Dying") && Sanguosha->currentRoomState()->getCurrentCardUseReason() != CardUseStruct::CARD_USE_REASON_PLAY)
        room->recover(effect.to, RecoverStruct(effect.from, this));
    else
        room->addPlayerMark(effect.to, "drank");
}

class FanVSSkill : public OneCardViewAsSkill
{
public:
    FanVSSkill() : OneCardViewAsSkill("fan")
    {
        filter_pattern = "%slash";
        response_or_use = true;
    }

    bool isEnabledAtPlay(const Player *player) const
    {
        return Slash::IsAvailable(player) && player->getMark("Equips_Nullified_to_Yourself") == 0;
    }

    bool isEnabledAtResponse(const Player *player, const QString &pattern) const
    {
        return Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_RESPONSE_USE
            && pattern == "slash" && player->getMark("Equips_Nullified_to_Yourself") == 0;
    }

    const Card *viewAs(const Card *originalCard) const
    {
        Card *acard = new FireSlash(originalCard->getSuit(), originalCard->getNumber());
        acard->addSubcard(originalCard->getId());
        acard->setSkillName(objectName());
        return acard;
    }
};

class FanSkill : public WeaponSkill
{
public:
    FanSkill() : WeaponSkill("fan")
    {
        events << ChangeSlash;
        view_as_skill = new FanVSSkill;
    }

    bool trigger(TriggerEvent, Room *, ServerPlayer *player, QVariant &data) const
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
            FireSlash *fire_slash = new FireSlash(use.card->getSuit(), use.card->getNumber());
            if (!use.card->isVirtualCard() || use.card->subcardsLength() > 0)
                fire_slash->addSubcard(use.card);
            fire_slash->setSkillName("fan");
            bool can_use = true;
            foreach (ServerPlayer *p, use.to) {
                if (!player->canSlash(p, fire_slash, false)) {
                    can_use = false;
                    break;
                }
            }
            if (can_use && player->askForSkillInvoke(this, data, false)) {
                use.card = fire_slash;
                data = QVariant::fromValue(use);
            }
        }
        return false;
    }
};

Fan::Fan(Suit suit, int number)
    : Weapon(suit, number, 4)
{
    setObjectName("fan");
}

class GudingBladeSkill : public WeaponSkill
{
public:
    GudingBladeSkill() : WeaponSkill("guding_blade")
    {
        events << DamageCaused;
    }

    bool trigger(TriggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.card->isKindOf("Slash")
            && damage.to->getMark("Equips_of_Others_Nullified_to_You") == 0
            && damage.to->isKongcheng() && damage.by_user && !damage.chain && !damage.transfer) {
            room->setEmotion(player, "weapon/guding_blade");
            room->getThread()->delay(600);
            Sanguosha->playAudioEffect(QString("audio/equip/GudingBlade.ogg")); //fix bug: guding_blade effect is muted

            LogMessage log;
            log.type = "#GudingBladeEffect";
            log.from = player;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);
            log.arg2 = QString::number(++damage.damage);
            room->sendLog(log);

            room->getThread()->delay(400);

            data = QVariant::fromValue(damage);
        }

        return false;
    }
};

GudingBlade::GudingBlade(Suit suit, int number)
    : Weapon(suit, number, 2)
{
    setObjectName("guding_blade");
}

class VineSkill : public ArmorSkill
{
public:
    VineSkill() : ArmorSkill("vine")
    {
        events << DamageInflicted << SlashEffected << CardEffected;
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == SlashEffected) {
            SlashEffectStruct effect = data.value<SlashEffectStruct>();
            if (effect.nature == DamageStruct::Normal) {
                room->setEmotion(player, "armor/vine");
                LogMessage log;
                log.from = player;
                log.type = "#ArmorNullify";
                log.arg = objectName();
                log.arg2 = effect.slash->objectName();
                room->sendLog(log);

                effect.to->setFlags("Global_NonSkillNullify");
                return true;
            }
        } else if (triggerEvent == CardEffected) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (effect.card->isKindOf("AOE")) {
                room->setEmotion(player, "armor/vine");
                LogMessage log;
                log.from = player;
                log.type = "#ArmorNullify";
                log.arg = objectName();
                log.arg2 = effect.card->objectName();
                room->sendLog(log);

                effect.to->setFlags("Global_NonSkillNullify");
                return true;
            }
        } else if (triggerEvent == DamageInflicted) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.nature == DamageStruct::Fire) {
                room->setEmotion(player, "armor/vineburn");
                LogMessage log;
                log.type = "#VineDamage";
                log.from = player;
                log.arg = QString::number(damage.damage);
                log.arg2 = QString::number(++damage.damage);
                room->sendLog(log);

                data = QVariant::fromValue(damage);
            }
        }

        return false;
    }
};

Vine::Vine(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("vine");
}

class SilverLionSkill : public ArmorSkill
{
public:
    SilverLionSkill() : ArmorSkill("silver_lion")
    {
        events << DamageInflicted << CardsMoveOneTime;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted && ArmorSkill::triggerable(player)) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.damage > 1) {
                room->setEmotion(player, "armor/silver_lion");
                LogMessage log;
                log.type = "#SilverLion";
                log.from = player;
                log.arg = QString::number(damage.damage);
                log.arg2 = objectName();
                room->sendLog(log);

                damage.damage = 1;
                data = QVariant::fromValue(damage);
            }
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || !move.from_places.contains(Player::PlaceEquip))
                return false;
            if (player->hasFlag("SilverLionRecover") || move.reason.m_skillName == "THROW_EQUIP_AREA") {
                for (int i = 0; i < move.card_ids.size(); i++) {
                    if (move.from_places[i] != Player::PlaceEquip) continue;
                    const Card *card = Sanguosha->getEngineCard(move.card_ids[i]);
                    if (card->objectName() == objectName()) {
                        player->setFlags("-SilverLionRecover");
                        //if (player->isWounded()) {
                            room->setEmotion(player, "armor/silver_lion");
                            room->recover(player, RecoverStruct(NULL, card));
                        //}
                        return false;
                    }
                }
            }
        }
        return false;
    }
};

SilverLion::SilverLion(Suit suit, int number)
    : Armor(suit, number)
{
    setObjectName("silver_lion");
}

void SilverLion::onUninstall(ServerPlayer *player) const
{
    if (player->isAlive() && player->hasArmorEffect(objectName()))
        player->setFlags("SilverLionRecover");
}

class SmallSilverLionSkill : public ArmorSkill
{
public:
    SmallSilverLionSkill() : ArmorSkill("small_silver_lion")
    {
        events << DamageInflicted << CardsMoveOneTime;
    }

    bool triggerable(const ServerPlayer *target) const
    {
        return target != NULL && target->isAlive();
    }

    bool trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
    {
        if (triggerEvent == DamageInflicted && ArmorSkill::triggerable(player)) {
            DamageStruct damage = data.value<DamageStruct>();
            if (damage.damage > 1) {
                room->setEmotion(player, "armor/silver_lion");
                LogMessage log;
                log.type = "#SilverLion";
                log.from = player;
                log.arg = QString::number(damage.damage);
                log.arg2 = objectName();
                room->sendLog(log);

                damage.damage = 1;
                data = QVariant::fromValue(damage);
            }
        } else {
            CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
            if (move.from != player || !move.from_places.contains(Player::PlaceEquip))
                return false;
            if (player->hasFlag("SilverLionRecover") || move.reason.m_skillName == "THROW_EQUIP_AREA") {
                for (int i = 0; i < move.card_ids.size(); i++) {
                    if (move.from_places[i] != Player::PlaceEquip) continue;
                    const Card *card = Sanguosha->getEngineCard(move.card_ids[i]);
                    if (card->objectName() == objectName()) {
                        player->setFlags("-SilverLionRecover");
                        //if (player->isWounded()) {
                            room->setEmotion(player, "armor/silver_lion");
                            room->recover(player, RecoverStruct(NULL, card));
                        //}
                        return false;
                    }
                }
            }
        }
        return false;
    }
};

FireAttack::FireAttack(Card::Suit suit, int number)
    : SingleTargetTrick(suit, number)
{
    setObjectName("fire_attack");
    damage_card = true;
}

bool FireAttack::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    int total_num = 1 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    return targets.length() < total_num && !to_select->isKongcheng() && (to_select != Self || !Self->isLastHandCard(this, true));
}

void FireAttack::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.from->getRoom();
    if (effect.to->isKongcheng())
        return;

    const Card *card = room->askForCardShow(effect.to, effect.from, objectName());
    room->showCard(effect.to, card->getEffectiveId());

    QString suit_str = card->getSuitString();
    QString pattern = QString(".%1").arg(suit_str.at(0).toUpper());
    QString prompt = QString("@fire-attack:%1::%2").arg(effect.to->objectName()).arg(suit_str);
    if (effect.from->isAlive()) {
        const Card *card_to_throw = room->askForCard(effect.from, pattern, prompt);
        if (card_to_throw) {
            int damage = 1;
            if (effect.from->hasSkill("chiling") && card_to_throw->hasFlag("chiling_select_card")) {
                room->setCardFlag(card_to_throw, "-chiling_select_card");
                room->sendCompulsoryTriggerLog(effect.from, "chiling");
                room->setEmotion(effect.from, "chiling_"+QString::number((qrand() % 2) + 1));
                damage = damage + 1;
            }
            room->damage(DamageStruct(this, effect.from, effect.to, damage, DamageStruct::Fire));
        } else {
            effect.from->setFlags("FireAttackFailed_" + effect.to->objectName()); // For AI
        }
    }

    if (card->isVirtualCard())
        delete card;
}

IronChain::IronChain(Card::Suit suit, int number)
    : TrickCard(suit, number)
{
    setObjectName("iron_chain");
    can_recast = true;
}

QString IronChain::getSubtype() const
{
    return "damage_spread";
}

bool IronChain::targetFilter(const QList<const Player *> &targets, const Player *, const Player *Self) const
{
    int total_num = 2 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    return targets.length() < total_num && !Self->isCardLimited(this, Card::MethodUse);
}

bool IronChain::targetsFeasible(const QList<const Player *> &targets, const Player *Self) const
{
    bool rec = (Sanguosha->currentRoomState()->getCurrentCardUseReason() == CardUseStruct::CARD_USE_REASON_PLAY) && can_recast;
    QList<int> sub;
    if (isVirtualCard())
        sub = subcards;
    else
        sub << getEffectiveId();
    foreach (int id, sub) {
        if (Self->getHandPile().contains(id)) {
            rec = false;
            break;
        }
    }

    if (rec && Self->isCardLimited(this, Card::MethodUse))
        return targets.length() == 0;
    int total_num = 2 + Sanguosha->correctCardTarget(TargetModSkill::ExtraTarget, Self, this);
    if (!rec)
        return targets.length() > 0 && targets.length() <= total_num;
    else
        return targets.length() <= total_num;
}

void IronChain::onUse(Room *room, const CardUseStruct &card_use) const
{
    if (card_use.to.isEmpty()) {
        CardMoveReason reason(CardMoveReason::S_REASON_RECAST, card_use.from->objectName());
        reason.m_skillName = this->getSkillName();
        QList<int> ids;
        if (isVirtualCard())
            ids = subcards;
        else
            ids << getId();
        QList<CardsMoveStruct> moves;
        foreach (int id, ids) {
            CardsMoveStruct move(id, NULL, Player::DiscardPile, reason);
            moves << move;
        }
        room->moveCardsAtomic(moves, true);
        card_use.from->broadcastSkillInvoke("@recast");

        LogMessage log;
        log.type = "#UseCard_Recast";
        log.from = card_use.from;
        log.card_str = card_use.card->toString();
        room->sendLog(log);

        card_use.from->drawCards(1, "recast");
    } else
        TrickCard::onUse(room, card_use);
}

void IronChain::onEffect(const CardEffectStruct &effect) const
{
    Room *room = effect.to->getRoom();
    room->setPlayerChained(effect.to);
}

SupplyShortage::SupplyShortage(Card::Suit suit, int number)
    : DelayedTrick(suit, number)
{
    setObjectName("supply_shortage");

    judge.pattern = ".|club";
    judge.good = true;
    judge.reason = objectName();
}

bool SupplyShortage::targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const
{
    if (!targets.isEmpty() || to_select->containsTrick(objectName()) || to_select == Self || !to_select->hasJudgeArea())
        return false;

    int distance_limit = 1 + Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, Self, this, to_select);
    int rangefix = 0;
    if (Self->getOffensiveHorse() && subcards.contains(Self->getOffensiveHorse()->getId()))
        rangefix += 1;
    if (getSkillName() == "lianglunche" || (Self->getTreasure() && subcards.contains(Self->getTreasure()->getId()) && Self->getTreasure()->isKindOf("Lianglunche")))
        rangefix += 1;

    if (Self->distanceTo(to_select, rangefix) > distance_limit)
        return false;

    return true;
}

void SupplyShortage::takeEffect(ServerPlayer *target) const
{
    target->skip(Player::Draw);
}

ManeuveringPackage::ManeuveringPackage()
    : Package("maneuvering", Package::CardPack)
{
    QList<Card *> cards;

    // spade
    cards << new GudingBlade(Card::Spade, 1)
        << new Vine(Card::Spade, 2)
        << new Analeptic(Card::Spade, 3)
        << new ThunderSlash(Card::Spade, 4)
        << new ThunderSlash(Card::Spade, 5)
        << new ThunderSlash(Card::Spade, 6)
        << new ThunderSlash(Card::Spade, 7)
        << new ThunderSlash(Card::Spade, 8)
        << new Analeptic(Card::Spade, 9)
        << new SupplyShortage(Card::Spade, 10)
        << new IronChain(Card::Spade, 11)
        << new IronChain(Card::Spade, 12)
        << new Nullification(Card::Spade, 13);
    // club
    cards << new SilverLion(Card::Club, 1)
        << new Vine(Card::Club, 2)
        << new Analeptic(Card::Club, 3)
        << new SupplyShortage(Card::Club, 4)
        << new ThunderSlash(Card::Club, 5)
        << new ThunderSlash(Card::Club, 6)
        << new IceSlash(Card::Club, 7)
        << new IceSlash(Card::Club, 8)
        << new Analeptic(Card::Club, 9)
        << new IronChain(Card::Club, 10)
        << new IronChain(Card::Club, 11)
        << new IronChain(Card::Club, 12)
        << new IronChain(Card::Club, 13);

    // heart
    cards << new Nullification(Card::Heart, 1)
        << new FireAttack(Card::Heart, 2)
        << new FireAttack(Card::Heart, 3)
        << new FireSlash(Card::Heart, 4)
        << new Peach(Card::Heart, 5)
        << new Peach(Card::Heart, 6)
        << new FireSlash(Card::Heart, 7)
        << new Jink(Card::Heart, 8)
        << new Jink(Card::Heart, 9)
        << new FireSlash(Card::Heart, 10)
        << new Jink(Card::Heart, 11)
        << new Jink(Card::Heart, 12)
        << new Nullification(Card::Heart, 13);

    // diamond
    cards << new Fan(Card::Diamond, 1)
        << new Peach(Card::Diamond, 2)
        << new Peach(Card::Diamond, 3)
        << new FireSlash(Card::Diamond, 4)
        << new FireSlash(Card::Diamond, 5)
        << new Jink(Card::Diamond, 6)
        << new Jink(Card::Diamond, 7)
        << new Jink(Card::Diamond, 8)
        << new Analeptic(Card::Diamond, 9)
        << new Jink(Card::Diamond, 10)
        << new Jink(Card::Diamond, 11)
        << new FireAttack(Card::Diamond, 12);

    DefensiveHorse *hualiu = new DefensiveHorse(Card::Diamond, 13);
    hualiu->setObjectName("hualiu");

    cards << hualiu;

    //for (int i=2;i<=4;i++){
    //    SilverLion *small_silver_lion = new SilverLion(Card::Club, i);
    //    small_silver_lion->setObjectName("small_silver_lion");
    //    cards << small_silver_lion;
    //}

    foreach(Card *card, cards)
        card->setParent(this);

    skills << new GudingBladeSkill << new FanSkill
        << new VineSkill << new SilverLionSkill << new SmallSilverLionSkill;
}

ADD_PACKAGE(Maneuvering)

