#include "player.h"
#include "engine.h"
#include "room.h"
#include "client.h"
#include "standard.h"
#include "settings.h"
#include "clientstruct.h"
#include "exppattern.h"
#include "wrapped-card.h"

Player::Player(QObject *parent)
    : QObject(parent), owner(false), general(NULL), general2(NULL),
    m_gender(General::Sexless), hp(-1), max_hp(-1), state("online"), seat(0), alive(true),
    phase(NotActive),
    weapon(NULL), armor(NULL), defensive_horse(NULL), offensive_horse(NULL), treasure(NULL),
    face_up(true), chained(false),
    hasweaponarea(true), hasarmorarea(true), hasdefensivehorsearea(true), hasoffensivehorsearea(true), hastreasurearea(true),
    hasjudgearea(true),
    role_shown(false), pile_open(QMap<QString, QStringList>())
{
}

void Player::setScreenName(const QString &screen_name)
{
    this->screen_name = screen_name;
}

QString Player::screenName() const
{
    return screen_name;
}

bool Player::isOwner() const
{
    return owner;
}

void Player::setOwner(bool owner)
{
    if (this->owner != owner) {
        this->owner = owner;
        emit owner_changed(owner);
    }
}

bool Player::hasShownRole() const
{
    return role_shown;
}

void Player::setShownRole(bool shown)
{
    this->role_shown = shown;
}

void Player::setHp(int hp)
{
    if (this->hp != hp) {
        this->hp = hp;
        emit hp_changed();
    }
}

int Player::getHp() const
{
    return hp;
}

int Player::getMaxHp() const
{
    return max_hp;
}

void Player::setMaxHp(int max_hp, bool keep_hp)
{
    int value = max_hp - this->max_hp;
    if (value == 0)
        return;
    bool is_overflow = this->hp > this->max_hp;
    this->max_hp = max_hp;
    if (!keep_hp && value < 0)
        if (!is_overflow)
            hp = qMin(max_hp, hp);
        else
            hp += value;
    emit hp_changed();
}

int Player::getLostHp() const
{
    return qMax(max_hp - qMax(hp, 0), 0);
}

bool Player::isWounded() const
{

    foreach (const Player *p, getAliveSiblings()) {
        if (p->phase != NotActive && p->hasLordSkill("guiming") && getKingdom() == "wu")
            return true;
    }

    if (isChained()){
        foreach (const Player *p, getAliveSiblings()) {
            if (p->hasSkill("bingfu") && p->canEffect(this ,"bingfu"))
                return true;
        }
    }

    if (hp < 0)
        return true;
    else
        return hp < max_hp;
}

General::Gender Player::getGender() const
{
    return m_gender;
}

void Player::setGender(General::Gender gender)
{
    m_gender = gender;
}

bool Player::isMale() const
{
    return m_gender == General::Male;
}

bool Player::isFemale() const
{
    return m_gender == General::Female;
}

bool Player::isNeuter() const
{
    return m_gender == General::Neuter;
}

bool Player::isSexless() const
{
    return m_gender == General::Sexless;
}

int Player::getSeat() const
{
    return seat;
}

void Player::setSeat(int seat)
{
    this->seat = seat;
}

bool Player::isAdjacentTo(const Player *another) const
{
    int alive_length = 1 + getAliveSiblings().length();
    if (!alive || !another->isAlive())
        return false;
    if (getMark("chaoyuan_"+another->objectName()) > 0 || another->getMark("chaoyuan_"+objectName()) > 0)
        return true;
    if (qAbs(seat - another->seat) == 1
        || (seat == 1 && another->seat == alive_length)
        || (seat == alive_length && another->seat == 1)) {
        return true;
    }
    return false;
}

bool Player::isAlive() const
{
    return alive;
}

bool Player::isDead() const
{
    return !alive;
}

void Player::setAlive(bool alive)
{
    this->alive = alive;
}

QString Player::getFlags() const
{
    return QStringList(flags.toList()).join("|");
}

QStringList Player::getFlagList() const
{
    return QStringList(flags.toList());
}

void Player::setFlags(const QString &flag)
{
    if (flag == ".") {
        clearFlags();
        return;
    }
    static QChar unset_symbol('-');
    if (flag.startsWith(unset_symbol)) {
        QString copy = flag;
        copy.remove(unset_symbol);
        flags.remove(copy);
    } else {
        flags.insert(flag);
    }
}

bool Player::hasFlag(const QString &flag) const
{
    return flags.contains(flag);
}

void Player::clearFlags()
{
    flags.clear();
}

int Player::getAttackRange(bool include_weapon) const
{
    if (hasFlag("InfinityAttackRange") || getMark("InfinityAttackRange") > 0)
        return 1000;

    include_weapon = include_weapon && weapon != NULL;

    int fixeddis = Sanguosha->correctAttackRange(this, include_weapon, true);
    if (fixeddis > 0)
        return fixeddis;

    int original_range = 1, weapon_range = 0;

    if (include_weapon) {
        const Weapon *card = qobject_cast<const Weapon *>(weapon->getRealCard());
        Q_ASSERT(card);
        weapon_range = card->getRange();
    }

    int real_range = qMax(original_range, weapon_range) + Sanguosha->correctAttackRange(this, include_weapon, false);

    if (real_range < 0)
        real_range = 0;

    return real_range;
}

bool Player::inMyAttackRange(const Player *other, int distance_fix, bool chengwu) const
{
    // for zhaofu

    foreach (const Player *p, getAliveSiblings()) {
        if (p->hasLordSkill("zhaofu") && p->distanceTo(other) == 1 && getKingdom() == "wu")
            return true;
    }

    // for jinchengwu

    if (chengwu && hasLordSkill("jinchengwu") && this != other) {
        foreach (const Player *p, getAliveSiblings()) {
            if (p->getKingdom() == "jin" && p->inMyAttackRange(other, distance_fix, false))
                return true;
        }
    }

    // for chaoyuan

    if (getMark("chaoyuan_"+other->objectName()) > 0)
        return true;

    // end
    if (attack_range_pair.contains(other)) return true;
    return this != other && distanceTo(other, distance_fix) <= getAttackRange();
}

void Player::setFixedDistance(const Player *player, int distance)
{
    if (distance < 0)
        fixed_distance.remove(player, -distance);
    else
        fixed_distance.insert(player, distance);
}

void Player::removeFixedDistance(const Player *player, int distance)
{
    fixed_distance.remove(player, distance);
}

void Player::insertAttackRangePair(const Player *player)
{
    attack_range_pair.append(player);
}

void Player::removeAttackRangePair(const Player *player)
{
    attack_range_pair.removeOne(player);
}

int Player::distanceTo(const Player *other, int distance_fix) const
{
    if (other == NULL)
        return 0;

    if (this == other)
        return 0;

    if (hasSkill("zhuiji") && other->getHp() < getHp())
        return 1;
    if (hasSkill("newzhuiji") && other->getHp() <= getHp())
        return 1;

    int mark = -1;
    foreach (QString mark_name, getMarkNames()) {
        if (mark_name.startsWith("fixed_distance_to_" + other->objectName()) && getMark(mark_name) > 0) {
            if (mark < 0)
                mark = getMark(mark_name);
            else
                mark = qMin(mark, getMark(mark_name));
        }
    }
    if (mark > 0)
        return mark;

    if (fixed_distance.contains(other)) {
        QList<int> distance_list = fixed_distance.values(other);
        int min = 10000;
        foreach (int d, distance_list) {
            if (min > d)
                min = d;
        }

        return min;
    }

    int right = qAbs(seat - other->seat);
    int left = aliveCount() - right;
    int distance = qMin(left, right);

    distance += Sanguosha->correctDistance(this, other);
    distance += distance_fix;

    // keep the distance >=1
    if (distance < 1)
        distance = 1;

    return distance;
}

void Player::setGeneral(const General *new_general)
{
    if (this->general != new_general) {
        this->general = new_general;

        if (new_general && kingdom.isEmpty())
            setKingdom(new_general->getKingdom());

        emit general_changed();
    }
}

void Player::setGeneralName(const QString &general_name)
{
    const General *new_general = Sanguosha->getGeneral(general_name);
    Q_ASSERT(general_name.isNull() || general_name.isEmpty() || new_general != NULL);
    setGeneral(new_general);
}

QString Player::getGeneralName() const
{
    if (general)
        return general->objectName();
    else
        return QString();
}

void Player::setGeneral2Name(const QString &general_name)
{
    const General *new_general = Sanguosha->getGeneral(general_name);
    if (general2 != new_general) {
        general2 = new_general;

        emit general2_changed();
    }
}

QString Player::getGeneral2Name() const
{
    if (general2)
        return general2->objectName();
    else
        return QString();
}

const General *Player::getGeneral2() const
{
    return general2;
}

QString Player::getState() const
{
    return state;
}

void Player::setState(const QString &state)
{
    if (this->state != state) {
        this->state = state;
        emit state_changed();
    }
}

void Player::setRole(const QString &role)
{
    if (this->role != role) {
        this->role = role;
        emit role_changed(role);
    }
}

QString Player::getRole() const
{
    return role;
}

Player::Role Player::getRoleEnum() const
{
    static QMap<QString, Role> role_map;
    if (role_map.isEmpty()) {
        role_map.insert("lord", Lord);
        role_map.insert("loyalist", Loyalist);
        role_map.insert("rebel", Rebel);
        role_map.insert("renegade", Renegade);
    }

    return role_map.value(role);
}

const General *Player::getAvatarGeneral() const
{
    if (general)
        return general;

    QString general_name = property("avatar").toString();
    if (general_name.isEmpty())
        return NULL;
    return Sanguosha->getGeneral(general_name);
}

const General *Player::getGeneral() const
{
    return general;
}

bool Player::isLord() const
{
    return getRole() == "lord";
}

bool Player::hasSkill(const QString &skill_name, bool include_lose) const
{
    if (!include_lose) {
        if (!hasEquipSkill(skill_name)) {
            const Skill *skill = Sanguosha->getSkill(skill_name);
            if (skill && !Sanguosha->correctSkillValidity(this, skill))
                return false;
        }
    }
    return skills.contains(skill_name)
        || acquired_skills.contains(skill_name) || property("pingjian_triggerskill").toString() == skill_name;
}

bool Player::hasSkill(const Skill *skill, bool include_lose /* = false */) const
{
    Q_ASSERT(skill != NULL);
    return hasSkill(skill->objectName(), include_lose);
}

bool Player::hasSkills(const QString &skill_name, bool include_lose) const
{
    foreach(QString skill, skill_name.split("|"))
    {
        bool checkpoint = true;
        foreach (QString sk, skill.split("+")) {
            if (!hasSkill(sk, include_lose)) {
                checkpoint = false;
                break;
            }
        }
        if (checkpoint) return true;
    }
    return false;
}

bool Player::hasInnateSkill(const QString &skill_name) const
{
    if (general && general->hasSkill(skill_name))
        return true;

    if (general2 && general2->hasSkill(skill_name))
        return true;

    return false;
}

bool Player::hasInnateSkill(const Skill *skill) const
{
    Q_ASSERT(skill != NULL);
    return hasInnateSkill(skill->objectName());
}

bool Player::hasLordSkill(const QString &skill_name, bool include_lose) const
{
    if (!isLord() && hasSkill("weidi")) {
        foreach (const Player *player, getAliveSiblings()) {
            if (player->isLord()) {
                if (player->hasLordSkill(skill_name, true))
                    return true;
                break;
            }
        }
    }

    if (!hasSkill(skill_name, include_lose))
        return false;

    if (acquired_skills.contains(skill_name))
        return true;

    QString mode = getGameMode();
    if (mode == "06_3v3" || mode == "06_XMode" || mode == "02_1v1" || mode == "04_if" || Config.value("WithoutLordskill", false).toBool())
        return false;

    if (ServerInfo.EnableHegemony)
        return false;

    if (isLord())
        return skills.contains(skill_name);

    return false;
}

bool Player::hasLordSkill(const Skill *skill, bool include_lose /* = false */) const
{
    Q_ASSERT(skill != NULL);
    return hasLordSkill(skill->objectName(), include_lose);
}

void Player::acquireSkill(const QString &skill_name)
{
    acquired_skills.append(skill_name);
}

void Player::detachSkill(const QString &skill_name)
{
    acquired_skills.removeOne(skill_name);
}

void Player::detachAllSkills()
{
    acquired_skills.clear();
}

void Player::addSkill(const QString &skill_name)
{
    skills << skill_name;
}

void Player::loseSkill(const QString &skill_name)
{
    skills.removeOne(skill_name);
}

QString Player::getPhaseString() const
{
    switch (phase) {
    case RoundStart: return "round_start";
    case Start: return "start";
    case Judge: return "judge";
    case Draw: return "draw";
    case Play: return "play";
    case Discard: return "discard";
    case Finish: return "finish";
    case NotActive:
    default:
        return "not_active";
    }
}

void Player::setPhaseString(const QString &phase_str)
{
    static QMap<QString, Phase> phase_map;
    if (phase_map.isEmpty()) {
        phase_map.insert("round_start", RoundStart);
        phase_map.insert("start", Start);
        phase_map.insert("judge", Judge);
        phase_map.insert("draw", Draw);
        phase_map.insert("play", Play);
        phase_map.insert("discard", Discard);
        phase_map.insert("finish", Finish);
        phase_map.insert("not_active", NotActive);
    }

    setPhase(phase_map.value(phase_str, NotActive));
}

void Player::setEquip(WrappedCard *equip)
{
    const EquipCard *card = qobject_cast<const EquipCard *>(equip->getRealCard());
    Q_ASSERT(card != NULL);
    switch (card->location()) {
    case EquipCard::WeaponLocation: weapon = equip; break;
    case EquipCard::ArmorLocation: armor = equip; break;
    case EquipCard::DefensiveHorseLocation: defensive_horse = equip; break;
    case EquipCard::OffensiveHorseLocation: offensive_horse = equip; break;
    case EquipCard::TreasureLocation: treasure = equip; break;
    }
}

void Player::removeEquip(WrappedCard *equip)
{
    const EquipCard *card = qobject_cast<const EquipCard *>(Sanguosha->getEngineCard(equip->getId()));
    Q_ASSERT(card != NULL);
    switch (card->location()) {
    case EquipCard::WeaponLocation: weapon = NULL; break;
    case EquipCard::ArmorLocation: armor = NULL; break;
    case EquipCard::DefensiveHorseLocation: defensive_horse = NULL; break;
    case EquipCard::OffensiveHorseLocation: offensive_horse = NULL; break;
    case EquipCard::TreasureLocation: treasure = NULL; break;
    }
}

bool Player::hasEquip(const Card *card) const
{
    Q_ASSERT(card != NULL);
    int weapon_id = -1, armor_id = -1, def_id = -1, off_id = -1, tr_id = -1;
    if (weapon) weapon_id = weapon->getEffectiveId();
    if (armor) armor_id = armor->getEffectiveId();
    if (defensive_horse) def_id = defensive_horse->getEffectiveId();
    if (offensive_horse) off_id = offensive_horse->getEffectiveId();
    if (treasure) tr_id = treasure->getEffectiveId();
    QList<int> ids;
    if (card->isVirtualCard())
        ids << card->getSubcards();
    else
        ids << card->getId();
    if (ids.isEmpty()) return false;
    foreach (int id, ids) {
        if (id != weapon_id && id != armor_id && id != def_id && id != off_id && id != tr_id)
            return false;
    }
    return true;
}

bool Player::hasEquip() const
{
    return weapon != NULL || armor != NULL || defensive_horse != NULL || offensive_horse != NULL || treasure != NULL;
}

WrappedCard *Player::getWeapon() const
{
    return weapon;
}

WrappedCard *Player::getArmor() const
{
    return armor;
}

WrappedCard *Player::getDefensiveHorse() const
{
    return defensive_horse;
}

WrappedCard *Player::getOffensiveHorse() const
{
    return offensive_horse;
}

WrappedCard *Player::getTreasure() const
{
    return treasure;
}

QList<const Card *> Player::getEquips() const
{
    QList<const Card *> equips;
    if (weapon)
        equips << weapon;
    if (armor)
        equips << armor;
    if (defensive_horse)
        equips << defensive_horse;
    if (offensive_horse)
        equips << offensive_horse;
    if (treasure)
        equips << treasure;

    return equips;
}

QList<int> Player::getEquipsId() const
{
    QList<int> equips;
    foreach (const Card *card, getEquips())
        equips << card->getEffectiveId();

    return equips;
}

const EquipCard *Player::getEquip(int index) const
{
    WrappedCard *equip;
    switch (index) {
    case 0: equip = weapon; break;
    case 1: equip = armor; break;
    case 2: equip = defensive_horse; break;
    case 3: equip = offensive_horse; break;
    case 4: equip = treasure; break;
    default:
        return NULL;
    }
    if (equip != NULL)
        return qobject_cast<const EquipCard *>(equip->getRealCard());

    return NULL;
}

bool Player::hasWeapon(const QString &weapon_name) const
{
    if (!weapon || getMark("Equips_Nullified_to_Yourself") > 0) return false;
    if (weapon->objectName() == weapon_name || weapon->isKindOf(weapon_name.toStdString().c_str())) return true;
    const Card *real_weapon = Sanguosha->getEngineCard(weapon->getEffectiveId());
    return real_weapon->objectName() == weapon_name || real_weapon->isKindOf(weapon_name.toStdString().c_str());
}

bool Player::hasArmorEffect(const QString &armor_name) const
{
    if (!tag["Qinggang"].toStringList().isEmpty() || getMark("Armor_Nullified") > 0
        || getMark("Equips_Nullified_to_Yourself") > 0 || !hasEquipArea(1))
        return false;

    const Player *current = NULL;
    foreach (const Player *p, getAliveSiblings()) {
        if (p->getPhase() != Player::NotActive) {
            current = p;
            break;
        }
    }
    if (current && current->hasSkill("benxi")) {
        bool alladj = true;
        foreach (const Player *p, current->getAliveSiblings()) {
            if (current->distanceTo(p) != 1) {
                alladj = false;
                break;
            }
        }
        if (alladj) return false;
    }

    if (!armor_name.isEmpty()) {
        QStringList equips = property("View_As_Equips_List").toStringList();
        if (equips.contains(armor_name)) return true;

        if (armor == NULL && alive) {
            if (armor_name == "eight_diagram" && hasSkills("bazhen|linglong|xiangrui"))
                return true;
            if (armor_name == "vine" && hasSkill("bossmanjia"))
                return true;
        }
        if (!armor) return false;
        if (armor->objectName() == armor_name || armor->isKindOf(armor_name.toStdString().c_str())) return true;
        if (armor_name == "sliver_lion" && armor->objectName() == "small_silver_lion") return true;
        const Card *real_armor = Sanguosha->getEngineCard(armor->getEffectiveId());
        return real_armor->objectName() == armor_name || real_armor->isKindOf(armor_name.toStdString().c_str());
    } else {
        return armor != NULL;
    }

    return false;
}

bool Player::hasTreasure(const QString &treasure_name) const
{
    if (!treasure || getMark("Equips_Nullified_to_Yourself") > 0) return false;
    if (treasure->objectName() == treasure_name || treasure->isKindOf(treasure_name.toStdString().c_str())) return true;
    const Card *real_treasure = Sanguosha->getEngineCard(treasure->getEffectiveId());
    return real_treasure->objectName() == treasure_name || real_treasure->isKindOf(treasure_name.toStdString().c_str());
}

QList<const Card *> Player::getJudgingArea() const
{
    QList<const Card *>cards;
    foreach(int card_id, judging_area)
        cards.append(Sanguosha->getCard(card_id));
    return cards;
}

QList<int> Player::getJudgingAreaID() const
{
    return judging_area;
}

Player::Phase Player::getPhase() const
{
    return phase;
}

void Player::setPhase(Phase phase)
{
    this->phase = phase;
    emit phase_changed();
}

bool Player::faceUp() const
{
    return face_up;
}

void Player::setFaceUp(bool face_up)
{
    if (this->face_up != face_up) {
        this->face_up = face_up;
        emit state_changed();
    }
}

int Player::getMaxCards() const
{
    int origin = Sanguosha->correctMaxCards(this, true);
    if (origin < 0)
        origin = qMax(hp, 0);
    int rule = 0, total = 0, extra = 0;
    if (Config.MaxHpScheme == 3 && general2) {
        total = general->getMaxHp() + general2->getMaxHp();
        if (total % 2 != 0 && getMark("AwakenLostMaxHp") == 0)
            rule = 1;
    }
    extra += Sanguosha->correctMaxCards(this);

    return qMax(origin + rule + extra, 0);
}

QString Player::getKingdom(bool original) const
{
    if (original || (kingdom.isEmpty() && general))
        return general->getKingdom();
    else
        return kingdom;
}

void Player::setKingdom(const QString &kingdom)
{
    if (this->kingdom != kingdom) {
        this->kingdom = kingdom;
        emit kingdom_changed();
    }
}

bool Player::isKongcheng() const
{
    return getHandcardNum() == 0;
}

bool Player::isNude() const
{
    return isKongcheng() && !hasEquip();
}

bool Player::isAllNude() const
{
    return isNude() && judging_area.isEmpty();
}

bool Player::canDiscard(const Player *to, const QString &flags) const
{
    if (to == NULL || to->isDead()) return false;
    static QChar handcard_flag('h');
    static QChar equip_flag('e');
    static QChar judging_flag('j');

    /*if (flags.contains(handcard_flag) && !to->isKongcheng()) return true;
    if (flags.contains(judging_flag) && !to->getJudgingArea().isEmpty()) return true;
    if (flags.contains(equip_flag)) {
        if (to->getDefensiveHorse() || to->getOffensiveHorse()) return true;
        if ((to->getWeapon() || to->getArmor() || to->getTreasure()) && (!to->hasSkill("qicai") || this == to)) return true;
    }*/
    /*if (flags.contains(handcard_flag)) {
        foreach (const Card *card, to->getHandcards()) {
            int id = card->getEffectiveId();
            if (this->canDiscard(to, id))
                return true;
        }
    }*/
    if (flags.contains(handcard_flag) && !to->isKongcheng()) return true;

    if (flags.contains(equip_flag)) {
        foreach (const Card *card, to->getEquips()) {
            int id = card->getEffectiveId();
            if (this->canDiscard(to, id))
                return true;
        }
    }
    if (flags.contains(judging_flag)) {
        foreach (const Card *card, to->getJudgingArea()) {
            int id = card->getEffectiveId();
            if (this->canDiscard(to, id))
                return true;
        }
    }
    return false;
}

bool Player::canDiscard(const Player *to, int card_id) const
{
    if (to == NULL)
        return false;

    if (to->isDead()) return false;

    if (to->hasSkill("qicai") && this != to) {
        if ((to->getWeapon() && card_id == to->getWeapon()->getEffectiveId())
            || (to->getArmor() && card_id == to->getArmor()->getEffectiveId())
            || (to->getTreasure() && card_id == to->getTreasure()->getEffectiveId()))
            return false;
    } else if (to->hasSkill("muying") && this != to) {
        if (to->getArmor() && card_id == to->getArmor()->getEffectiveId())
            return false;
    } else if (this == to) {
        if (!getJudgingAreaID().contains(card_id) && isJilei(Sanguosha->getCard(card_id)))
            return false;
    }
    return true;
}

void Player::addDelayedTrick(const Card *trick)
{
    judging_area << trick->getId();
}

void Player::removeDelayedTrick(const Card *trick)
{
    int index = judging_area.indexOf(trick->getId());
    if (index >= 0)
        judging_area.removeAt(index);
}

bool Player::containsTrick(const QString &trick_name) const
{
    foreach (int trick_id, judging_area) {
        WrappedCard *trick = Sanguosha->getWrappedCard(trick_id);
        if (trick->objectName() == trick_name)
            return true;
    }
    return false;
}

bool Player::isChained() const
{
    return chained;
}

void Player::setChained(bool chained)
{
    if (this->chained != chained) {
        this->chained = chained;
        emit state_changed();
    }
}

void Player::addMark(const QString &mark, int add_num)
{
    int value = marks.value(mark, 0);
    value += add_num;
    setMark(mark, value);
}

void Player::removeMark(const QString &mark, int remove_num)
{
    int value = marks.value(mark, 0);
    value -= remove_num;
    value = qMax(0, value);
    setMark(mark, value);
}

void Player::setMark(const QString &mark, int value)
{
    if (marks[mark] != value)
        marks[mark] = value;
}

int Player::getMark(const QString &mark) const
{
    return marks.value(mark, 0);
}

QStringList Player::getMarkNames() const
{
    return marks.keys();
}

bool Player::canSlash(const Player *other, const Card *slash, bool distance_limit,
    int rangefix, const QList<const Player *> &others) const
{
    if (other == NULL)
        return false;

    if (other == this || !other->isAlive())
        return false;

    Slash *newslash = new Slash(Card::NoSuit, 0);
    newslash->deleteLater();
#define THIS_SLASH (slash == NULL ? newslash : slash)
    if (isProhibited(other, THIS_SLASH, others))
        return false;

    if (distance_limit)
        return inMyAttackRange(other, rangefix - Sanguosha->correctCardTarget(TargetModSkill::DistanceLimit, this, THIS_SLASH, other));
    else
        return true;
#undef THIS_SLASH
}

bool Player::canSlash(const Player *other, bool distance_limit, int rangefix, const QList<const Player *> &others) const
{
    return canSlash(other, NULL, distance_limit, rangefix, others);
}

int Player::getCardCount(bool include_equip, bool include_judging) const
{
    int count = getHandcardNum();
    if (include_equip) {
        if (weapon) count++;
        if (armor) count++;
        if (defensive_horse) count++;
        if (offensive_horse) count++;
        if (treasure) count++;
    }
    if (include_judging)
        count += judging_area.length();
    return count;
}

QList<int> Player::getPile(const QString &pile_name) const
{
    return piles[pile_name];
}

QStringList Player::getPileNames() const
{
    QStringList names;
    foreach(QString pile_name, piles.keys())
        names.append(pile_name);
    return names;
}

QString Player::getPileName(int card_id) const
{
    foreach (QString pile_name, piles.keys()) {
        QList<int> pile = piles[pile_name];
        if (pile.contains(card_id))
            return pile_name;
    }

    return QString();
}

bool Player::pileOpen(const QString &pile_name, const QString &player) const
{
    return pile_open[pile_name].contains(player);
}

void Player::setPileOpen(const QString &pile_name, const QString &player)
{
    if (pile_open[pile_name].contains(player)) return;
    pile_open[pile_name].append(player);
}

QList<int> Player::getHandPile() const
{
    QList<int> result;
    foreach (const QString &pile, getPileNames()) {
        if (pile.startsWith("&") || pile == "wooden_ox") {
            foreach (int id, getPile(pile)) {
                result.append(id);
            }
        }
    }
    return result;
}

void Player::addHistory(const QString &name, int times)
{
    history[name] += times;
}

int Player::getSlashCount() const
{
    return history.value("Slash", 0)
        + history.value("ThunderSlash", 0)
        + history.value("FireSlash", 0)
        + history.value("IceSlash", 0);
}

void Player::clearHistory(const QString &name)
{
    if (name.isEmpty())
        history.clear();
    else
        history.remove(name);
}

bool Player::hasUsed(const QString &card_class, bool actual) const
{
    if (!actual) {
        if (property("AllSkillNoLimitingTimes").toBool()) return false;

        QStringList card_class_names = property("SkillNoLimitingTimes").toStringList();
        if (card_class_names.contains(card_class)) return false;
    }

    return history.value(card_class, 0) > 0;
}

int Player::usedTimes(const QString &card_class, bool actual) const
{
    if (!actual) {
        if (property("AllSkillNoLimitingTimes").toBool()) return 0;

        QStringList card_class_names = property("SkillNoLimitingTimes").toStringList();
        if (card_class_names.contains(card_class)) return 0;
    }

    return history.value(card_class, 0);
}

bool Player::hasEquipSkill(const QString &skill_name) const
{
    if (weapon) {
        const Weapon *weaponc = qobject_cast<const Weapon *>(weapon->getRealCard());
        if (Sanguosha->getSkill(weaponc) && Sanguosha->getSkill(weaponc)->objectName() == skill_name)
            return true;
    }
    if (armor) {
        const Armor *armorc = qobject_cast<const Armor *>(armor->getRealCard());
        if (Sanguosha->getSkill(armorc) && Sanguosha->getSkill(armorc)->objectName() == skill_name)
            return true;
    }
    if (treasure) {
        const Treasure *treasurec = qobject_cast<const Treasure *>(treasure->getRealCard());
        if (Sanguosha->getSkill(treasurec) && Sanguosha->getSkill(treasurec)->objectName() == skill_name)
            return true;
    }
    return false;
}

QSet<const TriggerSkill *> Player::getTriggerSkills() const
{
    QSet<const TriggerSkill *> skillList;
    QStringList skill_list = skills + acquired_skills;
    foreach (QString skill_name, skill_list.toSet()) {
        const TriggerSkill *skill = Sanguosha->getTriggerSkill(skill_name);
        if (skill && !hasEquipSkill(skill->objectName()))
            skillList << skill;
    }

    return skillList;
}

QSet<const Skill *> Player::getSkills(bool include_equip, bool visible_only) const
{
    return getSkillList(include_equip, visible_only).toSet();
}

QList<const Skill *> Player::getSkillList(bool include_equip, bool visible_only) const
{
    QList<const Skill *> skillList;
    QStringList skill_list = skills + acquired_skills;
    foreach (QString skill_name, skill_list) {
        const Skill *skill = Sanguosha->getSkill(skill_name);
        if (skill && !skillList.contains(skill)
            && (include_equip || !hasEquipSkill(skill->objectName()))
            && (!visible_only || skill->isVisible()))
            skillList << skill;
    }

    return skillList;
}

QSet<const Skill *> Player::getVisibleSkills(bool include_equip) const
{
    return getVisibleSkillList(include_equip).toSet();
}

QList<const Skill *> Player::getVisibleSkillList(bool include_equip) const
{
    return getSkillList(include_equip, true);
}

QStringList Player::getAcquiredSkills() const
{
    return acquired_skills;
}

QString Player::getSkillDescription() const
{
    QString description = QString();

    QString property_str = QString();
    property_str.append("<table border='1'>");
    //property_str.append("<tr><th colspan='2'>" + Sanguosha->translate("PLAYER_DESCRIPTION") + "</th></tr>");
    property_str.append("<tr><th>" + Sanguosha->translate("PLAYER_DESCRIPTION_GENDER") + "</th><td>");
    QString gender_str("  <img src='image/gender/%1.png' height=17 />");
    if (isMale())
        property_str.append(gender_str.arg("male") + Sanguosha->translate("male"));
    else if (isFemale())
        property_str.append(gender_str.arg("female") + Sanguosha->translate("female"));
    else if (isNeuter())
        property_str.append(gender_str.arg("neuter") + Sanguosha->translate("neuter"));
    else if (isSexless())
        property_str.append(gender_str.arg("sexless") + Sanguosha->translate("sexless"));

    property_str.append("</td></tr><tr><th>" + Sanguosha->translate("PLAYER_DESCRIPTION_KINGDOM") + "</th><td>");
    QString color_str = Sanguosha->getKingdomColor(kingdom).name();
    QString kingdom_str = QString("<font color=%1>%2</font>     ").arg(color_str).arg(Sanguosha->translate(kingdom));
    kingdom_str.prepend(QString("<img src='image/kingdom/icon/%1.png' height=17/>    ").arg(kingdom));
    property_str.append(kingdom_str);
    property_str.append("</td></tr></table><br/><br/>");

    QList<const Skill *> skill_list = getVisibleSkillList();
    QList<const Skill *> basara_list;
    if (getGeneralName() == "anjiang" || getGeneral2Name() == "anjiang") {
        QString basara = property("basara_generals").toString();
        if (!basara.isEmpty()) {
            QStringList basaras = basara.split("+");
            foreach (QString basara_gen, basaras) {
                const General *general = Sanguosha->getGeneral(basara_gen);
                if (general) basara_list.append(general->getVisibleSkillList());
            }
        }
    }

    foreach (const Skill *skill, skill_list + basara_list) {
        if (skill->isAttachedLordSkill() || (!hasSkill(skill) && !basara_list.contains(skill)))
            continue;
        QString skill_name = Sanguosha->translate(skill->objectName());
        QString desc = skill->getDescription();
        desc.replace("\n", "<br/>");
        description.append(QString("<b>%1</b>: %2 <br/> <br/>").arg(skill_name).arg(desc));
    }

    QStringList related_skillnames;
    if (getGeneral())
        related_skillnames += getGeneral()->getRelatedSkillNames();
    if (getGeneral2())
        related_skillnames += getGeneral2()->getRelatedSkillNames();
    foreach (const QString &skill_name, related_skillnames) {
        const Skill *skill = Sanguosha->getSkill(skill_name);
        if (skill && skill_name.startsWith("characteristic")) {
            QString desc = skill->getDescription();
            desc.replace("\n", "<br/>");
            description.append(QString("<b>%1</b>: %2 <br/> <br/>").arg(Sanguosha->translate(skill_name)).arg(desc));
        }
    }

    if (description.isEmpty())
        description = tr("No skills");
    else {
        description.prepend(property_str);

    }
    return description;
}

bool Player::isProhibited(const Player *to, const Card *card, const QList<const Player *> &others) const
{
    return Sanguosha->isProhibited(this, to, card, others);
}

bool Player::isPindianProhibited(const Player *to) const
{
    return Sanguosha->isPindianProhibited(this, to);
}

bool Player::canSlashWithoutCrossbow(const Card *slash) const
{
    Slash *newslash = new Slash(Card::NoSuit, 0);
    newslash->deleteLater();
#define THIS_SLASH (slash == NULL ? newslash : slash)
    int slash_count = getSlashCount();
    foreach (const Player *p, getAliveSiblings()) {
        int valid_slash_count = 1;
        valid_slash_count += Sanguosha->correctCardTarget(TargetModSkill::Residue, this, THIS_SLASH, p);
        if (slash_count < valid_slash_count)
            return true;
    }
    return false;
#undef THIS_SLASH
}

void Player::setCardLimitation(const QString &limit_list, const QString &pattern, bool single_turn)
{
    QStringList limit_type = limit_list.split(",");
    QString _pattern = pattern;
    if (!pattern.endsWith("$1") && !pattern.endsWith("$0")) {
        QString symb = single_turn ? "$1" : "$0";
        _pattern = _pattern + symb;
    }
    foreach (QString limit, limit_type) {
        Card::HandlingMethod method = Sanguosha->getCardHandlingMethod(limit);
        card_limitation[method] << _pattern;
    }
}

void Player::removeCardLimitation(const QString &limit_list, const QString &pattern)
{
    QStringList limit_type = limit_list.split(",");
    QString _pattern = pattern;
    if (!_pattern.endsWith("$1") && !_pattern.endsWith("$0"))
        _pattern = _pattern + "$0";
    foreach (QString limit, limit_type) {
        Card::HandlingMethod method = Sanguosha->getCardHandlingMethod(limit);
        card_limitation[method].removeOne(_pattern);
    }
}

void Player::clearCardLimitation(bool single_turn)
{
    QList<Card::HandlingMethod> limit_type;
    limit_type << Card::MethodUse << Card::MethodResponse << Card::MethodDiscard
        << Card::MethodRecast << Card::MethodPindian;
    foreach(Card::HandlingMethod method, limit_type)
    {
        QStringList limit_patterns = card_limitation[method];
        foreach (QString pattern, limit_patterns) {
            if (!single_turn || pattern.endsWith("$1"))
                card_limitation[method].removeAll(pattern);
        }
    }
}

bool Player::isCardLimited(const Card *card, Card::HandlingMethod method, bool isHandcard) const
{
    if (method == Card::MethodNone)
        return false;
    if (card->getTypeId() == Card::TypeSkill && method == card->getHandlingMethod()) {
        foreach (int card_id, card->getSubcards()) {
            const Card *c = Sanguosha->getCard(card_id);
            foreach (QString pattern, card_limitation[method]) {
                QString _pattern = pattern.split("$").first();
                if (isHandcard)
                    _pattern.replace("hand", ".");
                ExpPattern p(_pattern);
                if (p.match(this, c)) return true;
            }
        }
    } else {
        foreach (QString pattern, card_limitation[method]) {
            QString _pattern = pattern.split("$").first();
            if (isHandcard)
                _pattern.replace("hand", ".");
            ExpPattern p(_pattern);
            if (p.match(this, card)) return true;
        }
    }

    return false;
}

void Player::addQinggangTag(const Card *card)
{
    if (!card) return;
    QStringList qinggang = this->tag["Qinggang"].toStringList();
    qinggang.append(card->toString());
    this->tag["Qinggang"] = QVariant::fromValue(qinggang);
}

void Player::removeQinggangTag(const Card *card)
{
    if (!card) return;
    QStringList qinggang = this->tag["Qinggang"].toStringList();
    if (!qinggang.isEmpty()) {
        qinggang.removeOne(card->toString());
        this->tag["Qinggang"] = qinggang;
    }
}

void Player::copyFrom(Player *p)
{
    Player *b = this;
    Player *a = p;

    b->marks = QMap<QString, int>(a->marks);
    b->piles = QMap<QString, QList<int> >(a->piles);
    b->acquired_skills = QStringList(a->acquired_skills);
    b->flags = QSet<QString>(a->flags);
    b->history = QHash<QString, int>(a->history);
    b->m_gender = a->m_gender;

    b->hp = a->hp;
    b->max_hp = a->max_hp;
    b->kingdom = a->kingdom;
    b->role = a->role;
    b->seat = a->seat;
    b->alive = a->alive;

    b->phase = a->phase;
    b->weapon = a->weapon;
    b->armor = a->armor;
    b->defensive_horse = a->defensive_horse;
    b->offensive_horse = a->offensive_horse;
    b->treasure = a->treasure;
    b->face_up = a->face_up;
    b->chained = a->chained;
    b->judging_area = QList<int>(a->judging_area);
    b->fixed_distance = QMultiHash<const Player *, int>(a->fixed_distance);
    b->card_limitation = QMap<Card::HandlingMethod, QStringList>(a->card_limitation);

    b->tag = QVariantMap(a->tag);
}

QList<const Player *> Player::getSiblings() const
{
    QList<const Player *> siblings;
    if (parent()) {
        siblings = parent()->findChildren<const Player *>();
        siblings.removeOne(this);
    }
    return siblings;
}

QList<const Player *> Player::getAliveSiblings() const
{
    QList<const Player *> siblings = getSiblings();
    foreach (const Player *p, siblings) {
        if (!p->isAlive())
            siblings.removeOne(p);
    }
    return siblings;
}

bool Player::isNostalGeneral(const Player *p, const QString &general_name)
{
    static QStringList nostalMark;
    if (nostalMark.isEmpty())
        nostalMark << "nos_" << "tw_";
    foreach (const QString &s, nostalMark) {
        QString nostalName = s + general_name;
        if (p->getGeneralName().contains(nostalName) || (p->getGeneralName() != p->getGeneral2Name() && p->getGeneral2Name().contains(nostalName)))
            return true;
    }

    return false;
}

void Player::loseAttachLordSkill(const QString &skill_name )
{
    int nline = skill_name.indexOf("-");
    if (nline == -1)
        nline = skill_name.indexOf("_");
    QString engskillname = skill_name.left(nline);
    //find the lordskill that the lord owns from the attached skill. e.g. find "huangtian" of zhangjiao
    //from "huangtian_attach" of othen "qun" hero by splitting the "-" or "_";
    bool remove = true;
    
    foreach (const Player* p, getSiblings()) {
        const General* general = p->getGeneral();
        if (general->hasSkill(engskillname)) {
            remove = false;
            break;
        } else {
            if (general->hasSkill("weidi") && isLord() && hasSkill(engskillname)) {
                remove = false;
                break;
            }
            if (general->hasSkill("weiwudi_guixin") && p->hasSkill(engskillname)) {
                remove = false;
                break;
            }

        }
        if (p->getGeneral2()) {
            const General* general2 = p->getGeneral2();
            if (general2->hasSkill(engskillname)) {
                remove = false;
                break;
            } else {
                if (general2->hasSkill("weidi") && isLord() && hasSkill(engskillname)) {
                    remove = false;
                    break;
                }
                if (general2->hasSkill("weiwudi_guixin") && p->hasSkill(engskillname)) {
                    remove = false;
                    break;
                }
            }
        }
    }
    if (remove)
    {
        loseSkill(skill_name);
    }
}

bool Player::hasEquipArea(int i) const
{
    Q_ASSERT(i > -1 && i < 5);
    if (i == 0) return hasweaponarea;
    else if (i == 1) return hasarmorarea;
    else if (i == 2) return hasdefensivehorsearea;
    else if (i == 3) return hasoffensivehorsearea;
    else if (i == 4) return hastreasurearea;
    return false;
}

bool Player::hasEquipArea() const
{
    return hasweaponarea == true || hasarmorarea == true || hasdefensivehorsearea == true || hasoffensivehorsearea == true
            || hastreasurearea == true;
}

bool Player::hasWeaponArea() const
{
    return hasweaponarea;
}

void Player::setWeaponArea(bool flag)
{
    if (this->hasweaponarea != flag) this->hasweaponarea = flag;
}

bool Player::hasArmorArea() const
{
    return hasarmorarea;
}

void Player::setArmorArea(bool flag)
{
    if (this->hasarmorarea != flag) this->hasarmorarea = flag;
}

bool Player::hasDefensiveHorseArea() const
{
    return hasdefensivehorsearea;
}

void Player::setDefensiveHorseArea(bool flag)
{
    if (this->hasdefensivehorsearea != flag) this->hasdefensivehorsearea = flag;
}

bool Player::hasOffensiveHorseArea() const
{
    return hasdefensivehorsearea;
}

void Player::setOffensiveHorseArea(bool flag)
{
    if (this->hasoffensivehorsearea != flag) this->hasoffensivehorsearea = flag;
}

bool Player::hasTreasureArea() const
{
    return hastreasurearea;
}

void Player::setTreasureArea(bool flag)
{
    if (this->hastreasurearea != flag) this->hastreasurearea = flag;
}

void Player::setEquipArea(int i, bool flag)
{
    if (i == 0) setWeaponArea(flag);
    else if (i == 1) setArmorArea(flag);
    else if (i == 2) setDefensiveHorseArea(flag);
    else if (i == 3) setOffensiveHorseArea(flag);
    else if (i == 4) setTreasureArea(flag);
}

bool Player::hasJudgeArea() const
{
    return hasjudgearea;
}

void Player::setJudgeArea(bool flag)
{
    if (this->hasjudgearea != flag) this->hasjudgearea = flag;
}

bool Player::canPindian(const Player *target, bool except_self) const
{
    if (isDead() || isKongcheng() || !target || target->isDead() || target->isKongcheng()) return false;
    if (except_self && this == target) return false;
    if (isPindianProhibited(target)) return false;
    return true;
}

bool Player::canPindian(bool except_self) const
{
    if (isDead() || isKongcheng()) return false;
    //if (isPindianProhibited(NULL)) return false;
    QList<const Player *> players = getAliveSiblings();
    players << this;
    foreach (const Player *p, players) {
        if (canPindian(p, except_self))
            return true;
    }
    return false;
}

bool Player::canBePindianed(bool except_self) const
{
    if (isDead() || isKongcheng()) return false;
    QList<const Player *> players = getAliveSiblings();
    players << this;
    foreach (const Player *p, players) {
        if (p->canPindian(this, except_self))
            return true;
    }
    return false;
}

bool Player::canEffect(const Player *target, QString skill_name) const  //判断target是否不受player技能的影响，暂时不考虑“不受自己技能影响”，这种太麻烦了
{
    if (!target || this == target) return true;
    if (target->getMark("Invincible") > 0) return false;
    return true;
}

bool Player::isYourFriend(const Player *fri, QString mode) const
{
    if (mode == NULL || mode == "")
        mode = ServerInfo.GameMode;
    if (mode == "04_2v2" || mode == "04_tt") {
        QString role = isLord() ? "loyalist" : getRole();
        QString f_role = fri->isLord() ? "loyalist" : fri->getRole();
        if (f_role == role && f_role != "renegade" && getRole() != "renegade")
             return true;
        return false;
    } else if (mode == "04_if") {
        QString role = getRole();
        QString f_role = fri->getRole();
        if (role == f_role) return true;
        if ((role == "lord" && f_role == "loyalist") || (f_role == "lord" && role == "loyalist")) return true;
        if ((role == "rebel" && f_role == "renegade") || (f_role == "rebel" && role == "renegade")) return true;
        return false;
    }
    return false;
}

bool Player::isWeidi() const
{
    if (!isLord() && hasSkill("weidi"))
        return true;
    return false;
}

int Player::getChangeSkillState(const QString &skill_name) const
{
    QString str = "ChangeSkill_" + skill_name + "_State";
    const char *ch = str.toLatin1().constData();
    int n = property(ch).toInt();
    if (n <= 0) n = 1;
    return n;
}

int Player::getLevelSkillState(const QString &skill_name) const
{
    QString str = "LevelSkill_" + skill_name + "_State";
    const char *ch = str.toLatin1().constData();
    int n = property(ch).toInt();
    if (n <= 0) n = 1;
    return n;
}

bool Player::hasCard(const Card *card) const
{
    if (getEquips().contains(card))
        return true;
    if (getHandcards().contains(card))
        return true;
    return false;
}

bool Player::hasCard(int id) const
{
    const Card *card = Sanguosha->getCard(id);
    if (getEquips().contains(card))
        return true;
    if (getHandcards().contains(card))
        return true;
    return false;
}

QList<int> Player::getdrawPile() const
{
    return StringList2IntList(property("PlayerWantToGetDrawPile").toStringList());
}

QList<int> Player::getdiscardPile() const
{
    return StringList2IntList(property("PlayerWantToGetDiscardPile").toStringList());
}

QString Player::getDeathReason() const
{
    return property("My_Death_Reason").toString();
}

bool Player::isJieGeneral() const
{
    if (getGeneralName().startsWith("tenyear_") || getGeneral2Name().startsWith("tenyear_"))
        return true;
    if (getGeneralName().startsWith("ol_") || getGeneral2Name().startsWith("ol_"))
        return true;
    if (getGeneralName().startsWith("second_ol_") || getGeneral2Name().startsWith("second_ol_"))
        return true;
    if (getGeneralName().startsWith("mobile_") || getGeneral2Name().startsWith("mobile_"))
        return true;
    return false;
}

bool Player::isJieGeneral(const QString &name, const QString &except_name) const
{
    if (!isJieGeneral()) return false;

    if (except_name != QString()) {
        if (getGeneralName().contains(except_name) || getGeneral2Name().contains(except_name))
            return false;
    }

    if (name == QString()) return true;

    if (getGeneralName().contains(name) || getGeneral2Name().contains(name)) return true;
    return false;
}

bool Player::hasHideSkill(int general) const
{
    if (general == 1 && getGeneral())
        return getGeneral()->hasHideSkill();
    else if (general == 2 && getGeneral2())
        return getGeneral2()->hasHideSkill();
    return false;
}

bool Player::inYinniState() const
{
    return !property("yinni_general").toString().isEmpty() || !property("yinni_general2").toString().isEmpty();
}

bool Player::canSeeHandcard(const Player *player) const
{
    if (this == player) return true;
    if ((ServerInfo.GameMode == "04_2v2" || ServerInfo.GameMode == "04_tt" || ServerInfo.GameMode == "04_if") && isYourFriend(player, ServerInfo.GameMode)) return true;
    foreach (QString mark, player->getMarkNames()) {
        if (mark.startsWith("HandcardVisible_ALL") && player->getMark(mark) > 0)
            return true;
        if (mark.startsWith("HandcardVisible_" + objectName()) && player->getMark(mark) > 0)
            return true;
        if (mark.contains("+handcard_public+") && player->getMark(mark) > 0)
            return true;
    }
    return false;
}
