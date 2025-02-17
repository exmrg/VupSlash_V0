#include "gamerule.h"
#include "serverplayer.h"
#include "room.h"
#include "standard.h"
#include "maneuvering.h"
#include "engine.h"
#include "settings.h"
#include "json.h"
#include "roomthread.h"

GameRule::GameRule(QObject *)
    : TriggerSkill("game_rule")
{
    //@todo: this setParent is illegitimate in QT and is equivalent to calling
    // setParent(NULL). So taking it off at the moment until we figure out
    // a way to do it.
    //setParent(parent);

    events << GameReady << TurnStart << EventPhaseStart
        << EventPhaseProceeding << EventPhaseEnd << EventPhaseChanging
        << PreCardUsed << CardUsed << TargetSpecified << CardFinished << CardEffected
        << PreCardResponded
        << TrickCardCanceling
        << HpChanged
        << EventLoseSkill << EventAcquireSkill
        << AskForPeaches << AskForPeachesDone << BuryVictim << GameOverJudge
        << SlashHit << SlashEffected << SlashProceed
        << ConfirmDamage << DamageDone << DamageComplete
        << StartJudge << FinishRetrial << FinishJudge
        << ChoiceMade
        << CardsMoveOneTime;
}

bool GameRule::triggerable(const ServerPlayer *) const
{
    return true;
}

int GameRule::getPriority(TriggerEvent) const
{
    return 0;
}

void GameRule::onPhaseProceed(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    switch (player->getPhase()) {
    case Player::PhaseNone: {
        Q_ASSERT(false);
    }
    case Player::RoundStart:{
        break;
    }
    case Player::Start: {
        break;
    }
    case Player::Judge: {
        QList<const Card *> tricks = player->getJudgingArea();
        while (!tricks.isEmpty() && player->isAlive()) {
            const Card *trick = tricks.takeLast();
            if (trick->getSubtype() == "field_card") {    //场地牌不判定
                continue;
            }
            /*if (trick->isKindOf("Indulgence")) {
                foreach(ServerPlayer *p, room->getAlivePlayers()) {
                    if (player->getMark("jichong_from_"+p->objectName()+"_id_"+QString::number(trick->getEffectiveId())) > 0) {
                        player->setFlags("jichong_from_"+p->objectName());
                    }
                }
            }*/
            QVariant data = QVariant::fromValue(trick);
            bool broken = room->getThread()->trigger(BeforeDelayedTrickEffect, room, player, data);
            if (broken) {
                //CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, QString());
                //room->throwCard(trick, reason, NULL);
                continue;
            }
            bool on_effect = room->cardEffect(trick, NULL, player);
            if (!on_effect)
                trick->onNullified(player);
        }
        break;
    }
    case Player::Draw: {
        int num = 2;
        if (player->hasFlag("Global_FirstRound")) {
            room->setPlayerFlag(player, "-Global_FirstRound");
            if (room->getMode() == "02_1v1") num--;
        }

        QVariant data = num;
        if (!room->getThread()->trigger(DrawNCards, room, player, data))
        {
            int n = data.toInt();
            if (n > 0)
                player->drawCards(n, "draw_phase");
            QVariant _n = n;
            room->getThread()->trigger(AfterDrawNCards, room, player, _n);
        }
        break;
    }
    case Player::Play: {
        while (player->isAlive()) {
            CardUseStruct card_use;
            room->activate(player, card_use);
            if (card_use.card != NULL)
                room->useCard(card_use, true);
            else
                break;
        }
        break;
    }
    case Player::Discard: {
        int handcard_num = player->getHandcardNum();
        QVariantList  cardlist = player->tag["IgnoreCards"].toList();
        player->tag.remove("IgnoreCards");
        QList<int> ids = VariantList2IntList(cardlist);
        QStringList strlist;
        foreach (const Card *card, player->getHandcards()) {
            if (ids.contains(card->getEffectiveId())) {
                handcard_num--;
                QString str = card->toString();
                strlist << card->toString();
                room->setPlayerCardLimitation(player, "discard", str, true);
            }
        }

        int discard_num = handcard_num - player->getMaxCards();
        //if (discard_num > 0)
            //room->askForDiscard(player, "gamerule", discard_num, discard_num);
        QVariant data = discard_num;
        if (!room->getThread()->trigger(DiscardNCards, room, player, data))
        {
            int n = data.toInt();
            if (n > 0)
                room->askForDiscard(player, "gamerule", n, n);
            QVariant _n = n;
            room->getThread()->trigger(AfterDiscardNCards, room, player, _n);
        }
        foreach (QString str, strlist)
            room->removePlayerCardLimitation(player, "discard", str + "$1");
        break;
    }
    case Player::Finish: {
        break;
    }
    case Player::NotActive:{
        break;
    }
    }
}

bool GameRule::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    if (room->getTag("SkipGameRule").toBool()) {
        room->removeTag("SkipGameRule");
        return false;
    }

    // Handle global events
    if (player == NULL) {
        if (triggerEvent == GameReady) {
            if (room->getMode() == "04_boss") {
                int difficulty = Config.value("BossModeDifficulty", 0).toInt();
                if ((difficulty & (1 << GameRule::BMDIncMaxHp)) > 0) {
                    foreach (ServerPlayer *p, room->getPlayers()) {
                        if (p->isLord()) continue;
                        int m = p->getMaxHp() + 2;
                        p->setProperty("maxhp", m);
                        p->setProperty("hp", m);
                        room->broadcastProperty(p, "maxhp");
                        room->broadcastProperty(p, "hp");
                    }
                }
            }
            if (room->getMode() == "03_1v2") {
                room->acquireSkill(room->getLord(), "feiyang");
                room->acquireSkill(room->getLord(), "bahu");
            }
            foreach (ServerPlayer *player, room->getPlayers()) {
                if (player->getGeneral()->getKingdom() == "god" && player->getGeneralName() != "anjiang"
                    && !player->getGeneralName().startsWith("boss_"))
                    room->setPlayerProperty(player, "kingdom", room->askForKingdom(player));
                foreach (const Skill *skill, player->getVisibleSkillList()) {
                    //if (skill->getFrequency() == Skill::Limited && !skill->getLimitMark().isEmpty()
                    if (skill->isLimitedSkill() && !skill->getLimitMark().isEmpty()
                        && (!skill->isLordSkill() || player->hasLordSkill(skill->objectName())))
                        room->setPlayerMark(player, skill->getLimitMark(), 1);
                }
            }
            room->setTag("FirstRound", true);
            bool kof_mode = room->getMode() == "02_1v1" && Config.value("1v1/Rule", "2013").toString() != "Classical";
            QList<int> n_list;
            foreach (ServerPlayer *p, room->getPlayers()) {
                int n = kof_mode ? p->getMaxHp() : 4;
                QVariant data = n;
                room->getThread()->trigger(DrawInitialCards, room, p, data);
                n_list << data.toInt();
            }
            room->drawCards(room->getPlayers(), n_list, QString());
            if (Config.EnableLuckCard)
                room->askForLuckCard();
            int i = 0;
            foreach (ServerPlayer *p, room->getPlayers()) {
                QVariant _nlistati = n_list.at(i);
                room->getThread()->trigger(AfterDrawInitialCards, room, p, _nlistati);
                i++;
            }
        }
        return false;
    }

    switch (triggerEvent) {
    case TurnStart: {
        player = room->getCurrent();
        if (room->getTag("FirstRound").toBool()) {
            room->setTag("FirstRound", false);
            room->setPlayerFlag(player, "Global_FirstRound");
        }

        LogMessage log;
        log.type = "$AppendSeparator";
        room->sendLog(log);
        room->addPlayerMark(player, "Global_TurnCount");
        room->setPlayerMark(player, "damage_point_round", 0);
        if (room->getTag("Global_ExtraTurn" + player->objectName()).toBool()) {
            room->setPlayerMark(player, "@extra_turn", 1);
        } else {
            if (player == room->getAlivePlayers().first()) {
                int n = room->getTag("TurnLengthCount").toInt();
                room->setTag("TurnLengthCount", n + 1);
                QVariant rsdata = n + 1;
                room->doBroadcastNotify(QSanProtocol::S_COMMAND_ADD_ROUND, QVariant());
                foreach (ServerPlayer *p, room->getPlayers()) {
                    foreach (QString mark, p->getMarkNames()) {
                        if (mark.endsWith("_lun") || mark.endsWith("_lun!"))
                            room->setPlayerMark(p, mark, 0);
                    }
                }
                if (n == 0) {
                    foreach(ServerPlayer *p, room->getAlivePlayers())
                        room->getThread()->trigger(BeforeGameStart, room, p);
                    foreach(ServerPlayer *p, room->getAlivePlayers())
                        room->getThread()->trigger(GameStart, room, p);
                }
                foreach(ServerPlayer *p, room->getAlivePlayers())
                    room->getThread()->trigger(BeforeRoundStart, room, p, rsdata);
                foreach(ServerPlayer *p, room->getAlivePlayers())
                    room->getThread()->trigger(RoundStart, room, p, rsdata);
            }
        }
        if (room->getMode() == "04_boss" && player->isLord()) {
            int turn = player->getMark("Global_TurnCount");
            if (turn == 1)
                room->doLightbox("BossLevelA\\ 1 \\BossLevelB", 2000, 100);

            LogMessage log2;
            log2.type = "#BossTurnCount";
            log2.from = player;
            log2.arg = QString::number(turn);
            room->sendLog(log2);

            int limit = Config.value("BossModeTurnLimit", 70).toInt();
            int level = room->getTag("BossModeLevel").toInt();
            if (limit >= 0 && level < Config.BossLevel && player->getMark("Global_TurnCount") > limit)
                room->gameOver("lord");
        }

        if (!player->faceUp()) {
            room->removeTag("Global_ExtraTurn" + player->objectName());
            room->setPlayerMark(player, "@extra_turn", 0);
            player->turnOver();
        } else {
            player->play();
        }

        break;
    }
    case EventPhaseStart: {
        if (player->getPhase() == Player::RoundStart)
            player->breakYinniState();
        break;
    }
    case EventPhaseProceeding: {
        onPhaseProceed(player);
        break;
    }
    case EventPhaseEnd: {
        if (player->getPhase() == Player::Play)
            room->addPlayerHistory(player, ".");
        break;
    }
    case EventPhaseChanging: {
        PhaseChangeStruct change = data.value<PhaseChangeStruct>();
        if (change.to == Player::RoundStart) {
            QStringList skilllist = player->property("NextTurnSkill").toStringList();
            if (!skilllist.isEmpty()) {
                room->setPlayerProperty(player, "NextTurnSkill", QVariant());
                QString string = "NextTurnSkill_";
                QStringList lose;
                foreach (QString str, skilllist) {
                    QString st = string + str;
                    char* ch;
                    QByteArray ba = st.toLatin1();
                    ch = ba.data();
                    QStringList list = player->property(ch).toStringList();
                    room->setPlayerProperty(player, ch, QVariant());
                    foreach (QString strr, list) {
                        if (player->hasSkill(strr, true))
                            lose << "-" + strr;
                    }
                }
                if (!lose.isEmpty())
                    room->handleAcquireDetachSkills(player, lose.join("|"));
            }
        }
        if (change.to == Player::NotActive) {
            room->setPlayerProperty(player, "ExtraMaxCards_OneTurn", 0);
            room->setPlayerProperty(player, "ExtraAttackRange_OneTurn", 0);
            room->setPlayerProperty(player, "ExtraSlashCishu_OneTurn", 0);
            room->setPlayerProperty(player, "ExtraSlashJuli_OneTurn", 0);
            room->setPlayerProperty(player, "ExtraSlashMubiao_OneTurn", 0);
            room->setPlayerProperty(player, "ExtraDistance_From_OneTurn", 0);
            room->setPlayerProperty(player, "ExtraDistance_To_OneTurn", 0);
            room->setPlayerProperty(player, "ExtraFixedMaxCards_OneTurn", 0);

            player->tag.remove("IgnoreCards");

            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                if (p->getMark("drank") > 0) {
                    if (p->isAlive()) {
                        LogMessage log;
                        log.type = "#UnsetDrankEndOfTurn";
                        log.from = player;
                        log.to << p;
                        room->sendLog(log);
                    }

                    room->setPlayerMark(p, "drank", 0);
                }
                room->setPlayerMark(p, "Analeptic_used_times", 0);
            }
            room->setPlayerFlag(player, ".");
            room->clearPlayerCardLimitation(player, true);
            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                foreach (QString mark, p->getMarkNames()) {
                    if (mark.endsWith("-Clear") && p->getMark(mark) > 0)
                        room->setPlayerMark(p, mark, 0);
                    if (mark.endsWith("-PlayClear") && p->getMark(mark) > 0)
                        room->setPlayerMark(p, mark, 0);
                }
            }

            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                QStringList skilllist = p->property("OneTurnSkill").toStringList();
                if (!skilllist.isEmpty()) {
                    room->setPlayerProperty(p, "OneTurnSkill", QVariant());
                    QString string = "OneTurnSkill_";
                    QStringList lose;
                    foreach (QString str, skilllist) {
                        QString st = string + str;
                        char* ch;
                        QByteArray ba = st.toLatin1();
                        ch = ba.data();
                        QStringList list = p->property(ch).toStringList();
                        room->setPlayerProperty(p, ch, QVariant());
                        foreach (QString strr, list) {
                            if (p->hasSkill(strr, true))
                                lose << "-" + strr;
                        }
                    }
                    if (!lose.isEmpty())
                        room->handleAcquireDetachSkills(p, lose.join("|"));
                }
            }

            QStringList skilllist = player->property("TurnEndSkill").toStringList();
            if (!skilllist.isEmpty()) {
                room->setPlayerProperty(player, "TurnEndSkill", QVariant());
                QString string = "TurnEndSkill_";
                QStringList lose;
                foreach (QString str, skilllist) {
                    QString st = string + str;
                    char* ch;
                    QByteArray ba = st.toLatin1();
                    ch = ba.data();
                    QStringList list = player->property(ch).toStringList();
                    room->setPlayerProperty(player, ch, QVariant());
                    foreach (QString strr, list) {
                        if (player->hasSkill(strr, true))
                            lose << "-" + strr;
                    }
                }
                if (!lose.isEmpty())
                    room->handleAcquireDetachSkills(player, lose.join("|"));
            }

            if (room->getTag("Global_ExtraTurn" + player->objectName()).toBool()) {
                room->removeTag("Global_ExtraTurn" + player->objectName());
                room->setPlayerMark(player, "@extra_turn", 0);
            }

        } else if (change.to == Player::Play) {
            room->addPlayerHistory(player, ".");
            room->setPlayerMark(player, "damage_point_play_phase", 0);
        } else if (change.from == Player::Play) {
            foreach (ServerPlayer *p, room->getAllPlayers(true)) {
                foreach (QString mark, p->getMarkNames()) {
                    if (mark.endsWith("-PlayClear") && p->getMark(mark) > 0)
                        room->setPlayerMark(p, mark, 0);
                }
            }
        }
        break;
    }
    case PreCardUsed: {
        if (data.canConvert<CardUseStruct>()) {
            CardUseStruct card_use = data.value<CardUseStruct>();
            if (card_use.from->hasFlag("Global_ForbidSurrender")) {
                card_use.from->setFlags("-Global_ForbidSurrender");
                room->doNotify(card_use.from, QSanProtocol::S_COMMAND_ENABLE_SURRENDER, QVariant(true));
            }
            if (card_use.card->hasFlag("JINGYIN"))
                card_use.card->setFlags("-JINGYIN");
            else if (card_use.card->hasFlag("YUANBEN")) {
                card_use.card->setFlags("-YUANBEN");
                card_use.from->broadcastSkillInvoke(card_use.card->objectName());
            } else
                card_use.from->broadcastSkillInvoke(card_use.card);
            if (!card_use.card->getSkillName().isNull() && card_use.card->getSkillName(true) == card_use.card->getSkillName(false)
                && card_use.m_isOwnerUse && card_use.from->hasSkill(card_use.card->getSkillName()))
                room->notifySkillInvoked(card_use.from, card_use.card->getSkillName());
            if (card_use.card->hasFlag("RemoveFromHistory")) {
                room->setCardFlag(card_use.card, "-RemoveFromHistory");
                if (card_use.m_addHistory) {
                    card_use.m_addHistory = false;
                    room->addPlayerHistory(card_use.from, card_use.card->getClassName(), -1);
                    data = QVariant::fromValue(card_use);
                }
            }
        }
        break;
    }
    case CardUsed: {
        if (data.canConvert<CardUseStruct>()) {
            CardUseStruct card_use = data.value<CardUseStruct>();
            RoomThread *thread = room->getThread();

            if (card_use.card->hasPreAction())
                card_use.card->doPreAction(room, card_use);

            if (card_use.from && !card_use.to.isEmpty()) {
                thread->trigger(TargetSpecifying, room, card_use.from, data);
                CardUseStruct card_use = data.value<CardUseStruct>();
                QList<ServerPlayer *> targets = card_use.to;
                foreach (ServerPlayer *to, card_use.to) {
                    if (targets.contains(to)) {
                        thread->trigger(TargetConfirming, room, to, data);
                        CardUseStruct new_use = data.value<CardUseStruct>();
                        targets = new_use.to;
                    }
                }
            }
            card_use = data.value<CardUseStruct>();

            try {
                QVariantList jink_list_backup;
                if (card_use.card->isKindOf("Slash")) {
                    jink_list_backup = card_use.from->tag["Jink_" + card_use.card->toString()].toList();
                    QVariantList jink_list;
                    for (int i = 0; i < card_use.to.length(); i++)
                        jink_list.append(QVariant(1));
                    card_use.from->tag["Jink_" + card_use.card->toString()] = QVariant::fromValue(jink_list);
                }
                if (card_use.from && !card_use.to.isEmpty()) {
                    thread->trigger(TargetSpecified, room, card_use.from, data);
                    foreach(ServerPlayer *p, room->getAllPlayers())
                        thread->trigger(TargetConfirmed, room, p, data);
                }

                card_use = data.value<CardUseStruct>();

                room->setTag("CardUseNullifiedList", QVariant::fromValue(card_use.nullified_list));
                room->setTag("CardUseNoRespondList", QVariant::fromValue(card_use.no_respond_list));
                room->setTag("CardUseNoOffsetList", QVariant::fromValue(card_use.no_offset_list));

                card_use.card->use(room, card_use.from, card_use.to);
                if (!jink_list_backup.isEmpty())
                    card_use.from->tag["Jink_" + card_use.card->toString()] = QVariant::fromValue(jink_list_backup);
            }
            catch (TriggerEvent triggerEvent) {
                if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                    card_use.from->tag.remove("Jink_" + card_use.card->toString());
                throw triggerEvent;
            }
        }

        break;
    }
    case TargetSpecified: {
        CardUseStruct use = data.value<CardUseStruct>();
        if (use.card->isKindOf("Slash") && use.card->hasFlag("SlashIgnoreArmor")) {
            room->setCardFlag(use.card, "-SlashIgnoreArmor");
            foreach (ServerPlayer *p, use.to)
                p->addQinggangTag(use.card);
        }
        break;
    }
    case CardFinished: {
        CardUseStruct use = data.value<CardUseStruct>();
        room->clearCardFlag(use.card);

        if (use.card->isKindOf("AOE") || use.card->isKindOf("GlobalEffect")) {
            foreach(ServerPlayer *p, room->getAlivePlayers())
                room->doNotify(p, QSanProtocol::S_COMMAND_NULLIFICATION_ASKED, QVariant("."));
        }
        if (use.card->isKindOf("Slash"))
            use.from->tag.remove("Jink_" + use.card->toString());

        break;
    }
    case PreCardResponded:
    {
        CardResponseStruct resp = data.value<CardResponseStruct>();
        if (resp.m_isRetrial == false) {
            if (resp.m_card->hasFlag("JINGYIN"))
                resp.m_card->setFlags("-JINGYIN");
            else if (resp.m_card->hasFlag("YUANBEN")) {
                resp.m_card->setFlags("-YUANBEN");
                player->broadcastSkillInvoke(resp.m_card->objectName());
            }
            else
                player->broadcastSkillInvoke(resp.m_card);
        }

        break;
    }
    case TrickCardCanceling: {
        CardEffectStruct effect = data.value<CardEffectStruct>();
        if (effect.no_respond || effect.no_offset)
            return true;

        break;
    }
    case EventAcquireSkill:
    case EventLoseSkill: {
        QString skill_name = data.toString();
        const Skill *skill = Sanguosha->getSkill(skill_name);
        bool refilter = skill->inherits("FilterSkill");

        if (refilter)
            room->filterCards(player, player->getCards("he"), triggerEvent == EventLoseSkill);

        QStringList lose;
        QStringList skilllistone = player->property("OneTurnSkill").toStringList();
        QStringList skilllist = player->property("NextTurnSkill").toStringList();
        QStringList skilllistend = player->property("TurnEndSkill").toStringList();
        if (skilllistone.contains(skill_name)) {
            QString string = "OneTurnSkill_";
            QString st = string + skill_name;
            char* ch;
            QByteArray ba = st.toLatin1();
            ch = ba.data();
            QStringList list = player->property(ch).toStringList();
            foreach (QString str, list) {
                if (player->hasSkill(str, true) && !lose.contains("-" + str))
                    lose << "-" + str;
            }
            skilllistone.removeOne(skill_name);
            room->setPlayerProperty(player, "OneTurnSkill", skilllistone);
            room->setPlayerProperty(player, ch, QVariant());
        }

        if (skilllist.contains(skill_name)) {
            QString string = "NextTurnSkill_";
            QString st = string + skill_name;
            char*  ch;
            QByteArray ba = st.toLatin1();
            ch = ba.data();
            QStringList list = player->property(ch).toStringList();
            foreach (QString str, list) {
                if (player->hasSkill(str, true) && !lose.contains("-" + str))
                    lose << "-" + str;
            }
            skilllist.removeOne(skill_name);
            room->setPlayerProperty(player, "NextTurnSkill", skilllist);
            room->setPlayerProperty(player, ch, QVariant());
        }

        if (skilllistend.contains(skill_name)) {
            QString string = "TurnEndSkill_";
            QString st = string + skill_name;
            char* ch;
            QByteArray ba = st.toLatin1();
            ch = ba.data();
            QStringList list = player->property(ch).toStringList();
            foreach (QString str, list) {
                if (player->hasSkill(str, true) && !lose.contains("-" + str))
                    lose << "-" + str;
            }
            skilllistend.removeOne(skill_name);
            room->setPlayerProperty(player, "TurnEndSkill", skilllistend);
            room->setPlayerProperty(player, ch, QVariant());
        }

        if (!lose.isEmpty())
            room->handleAcquireDetachSkills(player, lose.join("|"));

        break;
    }
    case HpChanged: {

        player->breakYinniState();

        if (player->getHp() > 0)
            break;
        if (data.isNull() || data.canConvert<RecoverStruct>())
            break;
        if (data.canConvert<DamageStruct>()) {
            DamageStruct damage = data.value<DamageStruct>();
            room->enterDying(player, &damage, NULL);
        } else if (data.canConvert<HpLostStruct>()) {
            HpLostStruct hplost = data.value<HpLostStruct>();
            room->enterDying(player, NULL, &hplost);
        } else {
            room->enterDying(player, NULL, NULL);
        }

        break;
    }
    case AskForPeaches: {
        DyingStruct dying = data.value<DyingStruct>();
        const Card *peach = NULL;

        while (dying.who->getHp() <= 0) {
            peach = NULL;

            // coupling Wansha here to deal with complicated rule problems
            ServerPlayer *current = room->getCurrent();
            if (current && current->isAlive() && current->getPhase() != Player::NotActive && current->hasSkill("wansha")) {
                if (player != current && player != dying.who) {
                    player->setFlags("wansha");
                    room->addPlayerMark(player, "Global_PreventPeach");
                }
            }
            // coupling SpDushi here to deal with complicated rule problems
            if (dying.who->isAlive() && dying.who->hasSkill("spdushi") && player != dying.who) {
                player->setFlags("spdushi");
                room->addPlayerMark(player, "Global_PreventPeach");
            }

            if (dying.who->isAlive())
                peach = room->askForSinglePeach(player, dying.who);

            if (player->hasFlag("wansha") && player->getMark("Global_PreventPeach") > 0) {
                player->setFlags("-wansha");
                room->removePlayerMark(player, "Global_PreventPeach");
            }
            if (player->hasFlag("spdushi") && player->getMark("Global_PreventPeach") > 0) {
                player->setFlags("-spdushi");
                room->removePlayerMark(player, "Global_PreventPeach");
            }

            if (peach == NULL)
                break;
            room->useCard(CardUseStruct(peach, player, dying.who));
        }
        break;
    }
    case AskForPeachesDone: {
        if (player->getHp() <= 0 && player->isAlive()) {
            if (room->getThread()->trigger(DyingToDeath, room, player, data)) {
                room->removeTag("LastDyingData");
                room->setPlayerFlag(player, "-Global_Dying");
            } else {
                DyingStruct dying = data.value<DyingStruct>();
                room->killPlayer(player, dying.damage, dying.hplost);
            }
        }

        break;
    }
    case ConfirmDamage: {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.card && damage.to->getMark("SlashIsDrank") > 0) {
            LogMessage log;
            log.type = "#AnalepticBuff";
            log.from = damage.from;
            log.to << damage.to;
            log.arg = QString::number(damage.damage);

            damage.damage += damage.to->getMark("SlashIsDrank");
            damage.to->setMark("SlashIsDrank", 0);

            log.arg2 = QString::number(damage.damage);

            room->sendLog(log);

            data = QVariant::fromValue(damage);
        }

        break;
    }
    case DamageDone: {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.from && !damage.from->isAlive())
            damage.from = NULL;
        data = QVariant::fromValue(damage);

        LogMessage log;

        if (damage.from) {
            log.type = "#Damage";
            log.from = damage.from;
        } else {
            log.type = "#DamageNoSource";
        }

        log.to << damage.to;
        log.arg = QString::number(damage.damage);

        switch (damage.nature) {
        case DamageStruct::Normal: log.arg2 = "normal_nature"; break;
        case DamageStruct::Fire: log.arg2 = "fire_nature"; break;
        case DamageStruct::Thunder: log.arg2 = "thunder_nature"; break;
        case DamageStruct::Ice: log.arg2 = "ice_nature"; break;
        case DamageStruct::Light: log.arg2 = "light_nature"; break;
        case DamageStruct::Dark: log.arg2 = "dark_nature"; break;
        }

        room->sendLog(log);

        int new_hp = damage.to->getHp() - damage.damage;

        QString change_str = QString("%1:%2").arg(damage.to->objectName()).arg(-damage.damage);
        switch (damage.nature) {
        case DamageStruct::Fire: change_str.append("F"); break;
        case DamageStruct::Thunder: change_str.append("T"); break;
        case DamageStruct::Ice: change_str.append("I"); break;
        case DamageStruct::Light: change_str.append("L"); break;
        case DamageStruct::Dark: change_str.append("D"); break;
        default: break;
        }

        if (damage.damage > 0) {
            room->notifySkillInvoked(damage.to, QString("-"+QString::number(damage.damage)));
        }

        QString audio_name = "";
        if (damage.reason == "shengqiang")
            audio_name = "shot";
        else if (damage.card && damage.card->hasFlag("xingyao_hit"))
            audio_name = "earth_hit";
        else if (damage.reason == "potato_mine")
            audio_name = "potato_mine";
        JsonArray arg;
        arg << damage.to->objectName() << -damage.damage << damage.nature << audio_name;
        room->doBroadcastNotify(QSanProtocol::S_COMMAND_CHANGE_HP, arg);

        room->setTag("HpChangedData", data);

        if (damage.nature != DamageStruct::Normal && damage.nature != DamageStruct::Light && damage.nature != DamageStruct::Dark && player->isChained() && !damage.chain) {
            int n = room->getTag("is_chained").toInt();
            n++;
            room->setTag("is_chained", n);
        }

        room->setPlayerProperty(damage.to, "hp", new_hp);

        break;
    }
    case DamageComplete: {
        DamageStruct damage = data.value<DamageStruct>();
        if (damage.prevented)
            break;
        if (damage.nature != DamageStruct::Normal && damage.nature != DamageStruct::Light && damage.nature != DamageStruct::Dark && player->isChained())
            room->setPlayerChained(player);

        if (room->getTag("is_chained").toInt() > 0) {
            if (damage.nature != DamageStruct::Normal && damage.nature != DamageStruct::Light && damage.nature != DamageStruct::Dark && !damage.chain) {
                // iron chain effect
                int n = room->getTag("is_chained").toInt();
                n--;
                room->setTag("is_chained", n);
                QList<ServerPlayer *> chained_players;
                if (room->getCurrent()->isDead())
                    chained_players = room->getOtherPlayers(room->getCurrent());
                else
                    chained_players = room->getAllPlayers();
                chained_players.removeOne(player);
                foreach (ServerPlayer *chained_player, chained_players) {
                    if (chained_player->isChained()) {
                        room->getThread()->delay();
                        LogMessage log;
                        log.type = "#IronChainDamage";
                        log.from = chained_player;
                        room->sendLog(log);

                        DamageStruct chain_damage = damage;
                        chain_damage.to = chained_player;
                        chain_damage.chain = true;
                        chain_damage.transfer = false;
                        chain_damage.transfer_reason = QString();

                        room->damage(chain_damage);
                    }
                }
            }
        }
        if (room->getMode() == "02_1v1" || room->getMode() == "06_XMode") {
            foreach (ServerPlayer *p, room->getAllPlayers()) {
                if (p->hasFlag("Global_DebutFlag")) {
                    p->setFlags("-Global_DebutFlag");
                    if (room->getMode() == "02_1v1")
                        room->getThread()->trigger(Debut, room, p);
                }
            }
        }
        break;
    }
    case CardEffected: {
        if (data.canConvert<CardEffectStruct>()) {
            CardEffectStruct effect = data.value<CardEffectStruct>();
            if (!effect.card->isKindOf("Slash") && effect.nullified) {
                LogMessage log;
                log.type = "#CardNullified";
                log.from = effect.to;
                log.arg = effect.card->objectName();
                room->sendLog(log);

                return true;
            } else if (effect.card->getTypeId() == Card::TypeTrick) {
                bool is_canceled = room->isCanceled(effect);

                if (room->getTag("NullifyingTimes").toInt() > 0)
                    room->getThread()->trigger(EffectResponded, room, effect.from, data);

                if (is_canceled) {
                    effect.to->setFlags("Global_NonSkillNullify");
                    room->getThread()->trigger(EffectOffsetted, room, effect.from, data);
                    return true;
                } else {
                    room->getThread()->trigger(TrickEffect, room, effect.to, data);
                }
            }
            if (effect.to->isAlive() || effect.card->isKindOf("Slash"))
                effect.card->onEffect(effect);
        }

        break;
    }
    case SlashEffected: {
        SlashEffectStruct effect = data.value<SlashEffectStruct>();
        if (effect.nullified) {
            LogMessage log;
            log.type = "#CardNullified";
            log.from = effect.to;
            log.arg = effect.slash->objectName();
            room->sendLog(log);

            return true;
        }
        if (effect.no_respond || effect.no_offset) {
            room->slashResult(effect, NULL);
        } else {
            if (effect.jink_num > 0)
                room->getThread()->trigger(SlashProceed, room, effect.from, data);
            else
                room->slashResult(effect, NULL);
        }

        break;
    }
    case SlashProceed: {
        SlashEffectStruct effect = data.value<SlashEffectStruct>();
        QString slasher = effect.from->objectName();
        if (!effect.to->isAlive())
            break;
        if (effect.jink_num == 1) {
            const Card *jink = room->askForCard(effect.to, "jink", "slash-jink:" + slasher, data, Card::MethodUse, effect.from, false, QString(), false, effect.slash);
            room->slashResult(effect, room->isJinkEffected(effect.to, jink) ? jink : NULL);
        } else {
            DummyCard *jink = new DummyCard;
            const Card *asked_jink = NULL;
            for (int i = effect.jink_num; i > 0; i--) {
                QString prompt = QString("@multi-jink%1:%2::%3").arg(i == effect.jink_num ? "-start" : QString())
                    .arg(slasher).arg(i);
                asked_jink = room->askForCard(effect.to, "jink", prompt, data, Card::MethodUse, effect.from, false, QString(), false, effect.slash);
                if (!room->isJinkEffected(effect.to, asked_jink)) {
                    delete jink;
                    room->slashResult(effect, NULL);
                    return false;
                } else {
                    jink->addSubcard(asked_jink->getEffectiveId());
                }
            }
            room->slashResult(effect, jink);
        }

        break;
    }
    case SlashHit: {
        SlashEffectStruct effect = data.value<SlashEffectStruct>();

        if (effect.drank > 0) effect.to->setMark("SlashIsDrank", effect.drank);
        room->damage(DamageStruct(effect.slash, effect.from, effect.to, 1, effect.nature));

        break;
    }
    case GameOverJudge: {
        if (room->getMode() == "04_boss" && player->isLord()
            && (Config.value("BossModeEndless", false).toBool() || room->getTag("BossModeLevel").toInt() < Config.BossLevel - 1))
            break;
        if (room->getMode() == "02_1v1") {
            QStringList list = player->tag["1v1Arrange"].toStringList();
            QString rule = Config.value("1v1/Rule", "2013").toString();
            if (list.length() > ((rule == "2013") ? 3 : 0)) break;
        }

        QString winner = getWinner(player);
        if (!winner.isNull()) {
            room->gameOver(winner);
            return true;
        }

        break;
    }
    case BuryVictim: {
        int cheer_count = 0;
        if (room->getMode() == "04_if") {
            cheer_count = player->getMark("@Cheer_1") + player->getMark("@Cheer_2") + player->getMark("@Cheer_3") + player->getMark("@Cheer_4") + player->getMark("@Cheer_5") + player->getMark("@Cheer_6") + player->getMark("@Cheer_7") + player->getMark("@Cheer_8");
            cheer_count = floor(cheer_count / 2.0);
        }

        DeathStruct death = data.value<DeathStruct>();
        player->bury();

        if (room->getTag("SkipNormalDeathProcess").toBool())
            return false;

        ServerPlayer *killer = death.damage ? death.damage->from : NULL;
        //if (killer)
            rewardAndPunish(killer, player);

        if (room->getMode() == "02_1v1") {
            QStringList list = player->tag["1v1Arrange"].toStringList();
            QString rule = Config.value("1v1/Rule", "2013").toString();
            if (list.length() <= ((rule == "2013") ? 3 : 0)) break;

            if (rule == "Classical") {
                player->tag["1v1ChangeGeneral"] = list.takeFirst();
                player->tag["1v1Arrange"] = list;
            } else {
                player->tag["1v1ChangeGeneral"] = list.first();
            }

            changeGeneral1v1(player);
            if (death.damage == NULL)
                room->getThread()->trigger(Debut, room, player);
            else
                player->setFlags("Global_DebutFlag");
            return false;
        } else if (room->getMode() == "04_if") {
            QStringList selected = player->getSelected();
            selected.removeOne(Sanguosha->translate("parent:"+player->getGeneralName()) != "parent:"+player->getGeneralName() ? Sanguosha->translate("parent:"+player->getGeneralName()) : player->getGeneralName());
            if (selected.length() > 0) {
                QString general_name = room->askForGeneral(player, selected);
                room->revivePlayer(player);
                foreach(const Skill *skill, player->getSkillList(false, true)) {
                    room->detachSkillFromPlayer(player, skill->objectName(), false, false, false, true);
                    foreach(const Skill *sub_skill, Sanguosha->getRelatedSkills(skill->objectName())) {
                        room->detachSkillFromPlayer(player, sub_skill->objectName(), false, false, false, true);
                    }
                }
                player->obtainEquipArea();
                player->obtainJudgeArea();
                if (player->isChained())
                    room->setPlayerProperty(player, "chained", false);
                if (!player->faceUp())
                    player->turnOver();
                room->setPlayerMark(player, "IF_gaincheer", cheer_count);
                room->changeHero(player, general_name, true, true);
                room->setPlayerProperty(player, "kingdom", player->getRole() == "loyalist" ? "team_fire" : "team_ice");
                player->clearSelected();
                for (int i=0;i<selected.length();i++)
                    player->addToSelected(selected.at(i));

                room->setTag("FirstRound", true); //For Manjuan
                int draw_num = 4;
                QVariant data = draw_num;
                room->getThread()->trigger(DrawInitialCards, room, player, data);
                draw_num = data.toInt();
                try {
                    player->drawCards(draw_num);
                    room->setTag("FirstRound", false);
                }
                catch (TriggerEvent triggerEvent) {
                    if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                        room->setTag("FirstRound", false);
                    throw triggerEvent;
                }
                QVariant _drawnum = draw_num;
                room->getThread()->trigger(AfterDrawInitialCards, room, player, _drawnum);
                room->getThread()->trigger(IceFireDebut, room, player);
            }
        } else if (room->getMode() == "06_XMode") {
            changeGeneralXMode(player);
            if (death.damage != NULL)
                player->setFlags("Global_DebutFlag");
            return false;
        } else if (room->getMode() == "04_boss" && player->isLord()) {
            int level = room->getTag("BossModeLevel").toInt();
            level++;
            room->setTag("BossModeLevel", level);
            doBossModeDifficultySettings(player);
            changeGeneralBossMode(player);
            if (death.damage != NULL)
                player->setFlags("Global_DebutFlag");
            room->doLightbox(QString("BossLevelA\\ %1 \\BossLevelB").arg(level + 1), 2000, 100);
            return false;
        }

        break;
    }
    case StartJudge: {
        int card_id = room->drawCard();

        JudgeStruct *judge = data.value<JudgeStruct *>();
        judge->card = Sanguosha->getCard(card_id);

        LogMessage log;
        log.type = "$InitialJudge";
        log.from = player;
        log.card_str = QString::number(judge->card->getEffectiveId());
        room->sendLog(log);

        room->moveCardTo(judge->card, NULL, judge->who, Player::PlaceJudge,
            CardMoveReason(CardMoveReason::S_REASON_JUDGE,
            judge->who->objectName(),
            QString(), QString(), judge->reason), true);
        judge->updateResult();
        break;
    }
    case FinishRetrial: {
        JudgeStruct *judge = data.value<JudgeStruct *>();

        judge->result_objname = judge->card->objectName();
        judge->result_number = judge->card->getNumber();
        judge->result_suit = judge->card->getSuit();

        LogMessage log;
        log.type = "$JudgeResult";
        log.from = player;
        log.card_str = QString::number(judge->card->getEffectiveId());
        room->sendLog(log);

        int delay = Config.AIDelay;
        if (!judge->time_consuming)
            room->getThread()->delay(delay);
        if (judge->play_animation) {
            room->sendJudgeResult(judge);
            room->getThread()->delay(Config.S_JUDGE_LONG_DELAY);
        }

        break;
    }
    case FinishJudge: {
        JudgeStruct *judge = data.value<JudgeStruct *>();

        if (room->getCardPlace(judge->card->getEffectiveId()) == Player::PlaceJudge) {
            CardMoveReason reason(CardMoveReason::S_REASON_JUDGEDONE, judge->who->objectName(),
                judge->reason, QString());
            if (judge->retrial_by_response)
                reason.m_extraData = QVariant::fromValue(judge->retrial_by_response);
            room->moveCardTo(judge->card, judge->who, NULL, Player::DiscardPile, reason, true);
        }

        break;
    }
    case ChoiceMade: {
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            foreach (QString flag, p->getFlagList()) {
                if (flag.startsWith("Global_") && flag.endsWith("Failed"))
                    room->setPlayerFlag(p, "-" + flag);
            }
        }
        break;
    }
    case CardsMoveOneTime: {
        CardsMoveOneTimeStruct move = data.value<CardsMoveOneTimeStruct>();
        if ((!move.from && move.from_places.contains(Player::DrawPile)) || (!move.to && move.to_place == Player::DrawPile)) {
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                room->setPlayerProperty(p, "PlayerWantToGetDrawPile", QVariant::fromValue(IntList2StringList(room->getDrawPile())));
        }
        if ((!move.from && move.from_places.contains(Player::DiscardPile)) || (!move.to && move.to_place == Player::DiscardPile)) {
            foreach (ServerPlayer *p, room->getAllPlayers(true))
                room->setPlayerProperty(p, "PlayerWantToGetDiscardPile", QVariant::fromValue(IntList2StringList(room->getDiscardPile())));
        }
        if ((move.from && move.from == player && move.from_places.contains(Player::PlaceHand)) ||
                (move.to && move.to == player && move.to_place == Player::PlaceHand))
            room->setPlayerProperty(player, "My_Visible_HandCards", IntList2StringList(player->handCards()).join("+"));
        break;
    }
    default:
        break;
    }

    return false;
}

void GameRule::changeGeneral1v1(ServerPlayer *player) const
{
    Config.AIDelay = Config.OriginAIDelay;

    Room *room = player->getRoom();
    bool classical = (Config.value("1v1/Rule", "2013").toString() == "Classical");
    QString new_general;
    if (classical) {
        new_general = player->tag["1v1ChangeGeneral"].toString();
        player->tag.remove("1v1ChangeGeneral");
    } else {
        QStringList list = player->tag["1v1Arrange"].toStringList();
        if (player->getAI())
            new_general = list.first();
        else
            new_general = room->askForGeneral(player, list);
        list.removeOne(new_general);
        player->tag["1v1Arrange"] = QVariant::fromValue(list);
    }

    if (player->getPhase() != Player::NotActive)
        player->changePhase(player->getPhase(), Player::NotActive);

    room->revivePlayer(player);
    room->changeHero(player, new_general, true, true);
    if (player->getGeneral()->getKingdom() == "god")
        room->setPlayerProperty(player, "kingdom", room->askForKingdom(player));
    room->addPlayerHistory(player, ".");

    if (player->getKingdom() != player->getGeneral()->getKingdom())
        room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());

    QList<ServerPlayer *> notified = classical ? room->getOtherPlayers(player, true) : room->getPlayers();
    room->doBroadcastNotify(notified, QSanProtocol::S_COMMAND_REVEAL_GENERAL, JsonArray() << player->objectName() << new_general);

    if (!player->faceUp())
        player->turnOver();

    if (player->isChained())
        room->setPlayerChained(player);

    room->setTag("FirstRound", true); //For Manjuan
    int draw_num = classical ? 4 : player->getMaxHp();
    QVariant data = draw_num;
    room->getThread()->trigger(DrawInitialCards, room, player, data);
    draw_num = data.toInt();
    try {
        player->drawCards(draw_num);
        room->setTag("FirstRound", false);
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            room->setTag("FirstRound", false);
        throw triggerEvent;
    }
    QVariant _drawnum = draw_num;
    room->getThread()->trigger(AfterDrawInitialCards, room, player, _drawnum);
}

void GameRule::changeGeneralXMode(ServerPlayer *player) const
{
    Config.AIDelay = Config.OriginAIDelay;

    Room *room = player->getRoom();
    ServerPlayer *leader = player->tag["XModeLeader"].value<ServerPlayer *>();
    Q_ASSERT(leader);
    QStringList backup = leader->tag["XModeBackup"].toStringList();
    QString general = room->askForGeneral(leader, backup);
    if (backup.contains(general))
        backup.removeOne(general);
    else
        backup.takeFirst();
    leader->tag["XModeBackup"] = QVariant::fromValue(backup);

    if (player->getPhase() != Player::NotActive)
        player->changePhase(player->getPhase(), Player::NotActive);

    room->revivePlayer(player);
    room->changeHero(player, general, true, true);
    if (player->getGeneral()->getKingdom() == "god")
        room->setPlayerProperty(player, "kingdom", room->askForKingdom(player));
    room->addPlayerHistory(player, ".");

    if (player->getKingdom() != player->getGeneral()->getKingdom())
        room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());

    if (!player->faceUp())
        player->turnOver();

    if (player->isChained())
        room->setPlayerChained(player);

    room->setTag("FirstRound", true); //For Manjuan
    QVariant data(4);
    room->getThread()->trigger(DrawInitialCards, room, player, data);
    int num = data.toInt();
    try {
        player->drawCards(num);
        room->setTag("FirstRound", false);
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            room->setTag("FirstRound", false);
        throw triggerEvent;
    }

    QVariant _num = num;
    room->getThread()->trigger(AfterDrawInitialCards, room, player, _num);
}

void GameRule::changeGeneralBossMode(ServerPlayer *player) const
{
    Config.AIDelay = Config.OriginAIDelay;

    Room *room = player->getRoom();
    int level = room->getTag("BossModeLevel").toInt();
    room->doBroadcastNotify(QSanProtocol::S_COMMAND_UPDATE_BOSS_LEVEL, QVariant(level));
    QString general;
    if (level <= Config.BossLevel - 1) {
        QStringList boss_generals = Config.BossGenerals.at(level).split("+");
        if (boss_generals.length() == 1)
            general = boss_generals.first();
        else {
            if (Config.value("OptionalBoss", false).toBool())
                general = room->askForGeneral(player, boss_generals);
            else
                general = boss_generals.at(qrand() % boss_generals.length());
        }
    } else {
        general = (qrand() % 2 == 0) ? "sujiang" : "sujiangf";
    }

    if (player->getPhase() != Player::NotActive)
        player->changePhase(player->getPhase(), Player::NotActive);

    room->revivePlayer(player);
    room->changeHero(player, general, true, true);
    room->setPlayerMark(player, "BossMode_Boss", 1);
    int actualmaxhp = player->getMaxHp();
    if (level >= Config.BossLevel)
        actualmaxhp = level * 5 + 5;
    int difficulty = Config.value("BossModeDifficulty", 0).toInt();
    if ((difficulty & (1 << BMDDecMaxHp)) > 0) {
        if (level == 0);
        else if (level == 1) actualmaxhp -= 2;
        else if (level == 2) actualmaxhp -= 4;
        else actualmaxhp -= 5;
    }
    if (actualmaxhp != player->getMaxHp()) {
        player->setProperty("maxhp", actualmaxhp);
        player->setProperty("hp", actualmaxhp);
        room->broadcastProperty(player, "maxhp");
        room->broadcastProperty(player, "hp");
    }
    if (level >= Config.BossLevel)
        acquireBossSkills(player, level);
    room->addPlayerHistory(player, ".");

    if (player->getKingdom() != player->getGeneral()->getKingdom())
        room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());

    if (!player->faceUp())
        player->turnOver();

    if (player->isChained())
        room->setPlayerChained(player);

    room->setTag("FirstRound", true); //For Manjuan
    QVariant data(4);
    room->getThread()->trigger(DrawInitialCards, room, player, data);
    int num = data.toInt();
    try {
        player->drawCards(num);
        room->setTag("FirstRound", false);
    }
    catch (TriggerEvent triggerEvent) {
        if (triggerEvent == TurnBroken || triggerEvent == StageChange)
            room->setTag("FirstRound", false);
        throw triggerEvent;
    }

    QVariant _num = num;
    room->getThread()->trigger(AfterDrawInitialCards, room, player, _num);
}

void GameRule::acquireBossSkills(ServerPlayer *player, int level) const
{
    QStringList skills = Config.BossEndlessSkills;
    int num = qBound(qMin(5, skills.length()), 5 + level - Config.BossLevel, qMin(10, skills.length()));
    for (int i = 0; i < num; i++) {
        QString skill = skills.at(qrand() % skills.length());
        skills.removeOne(skill);
        if (skill.contains("+")) {
            QStringList subskills = skill.split("+");
            skill = subskills.at(qrand() % subskills.length());
        }
        player->getRoom()->acquireSkill(player, skill);
    }
}

void GameRule::doBossModeDifficultySettings(ServerPlayer *lord) const
{
    Room *room = lord->getRoom();
    QList<ServerPlayer *> unions = room->getOtherPlayers(lord, true);
    int difficulty = Config.value("BossModeDifficulty", 0).toInt();
    if ((difficulty & (1 << BMDRevive)) > 0) {
        foreach (ServerPlayer *p, unions) {
            if (p->isDead() && p->getMaxHp() > 0) {
                room->revivePlayer(p, true);
                room->addPlayerHistory(p, ".");
                if (!p->faceUp())
                    p->turnOver();
                if (p->isChained())
                    room->setPlayerChained(p);
                p->setProperty("hp", qMin(p->getMaxHp(), 4));
                room->broadcastProperty(p, "hp");
                QStringList acquired = p->tag["BossModeAcquiredSkills"].toStringList();
                foreach (QString skillname, acquired) {
                    if (p->hasSkill(skillname, true))
                        acquired.removeOne(skillname);
                }
                p->tag["BossModeAcquiredSkills"] = QVariant::fromValue(acquired);
                if (!acquired.isEmpty())
                    room->handleAcquireDetachSkills(p, acquired, true);
                foreach (const Skill *skill, p->getSkillList()) {
                    //if (skill->getFrequency() == Skill::Limited && !skill->getLimitMark().isEmpty())
                    if (skill->isLimitedSkill() && !skill->getLimitMark().isEmpty())
                        room->setPlayerMark(p, skill->getLimitMark(), 1);
                }
            }
        }
    }
    if ((difficulty & (1 << BMDRecover)) > 0) {
        foreach (ServerPlayer *p, unions) {
            if (p->isAlive() && p->isWounded()) {
                p->setProperty("hp", p->getMaxHp());
                room->broadcastProperty(p, "hp");
            }
        }
    }
    if ((difficulty & (1 << BMDDraw)) > 0) {
        foreach (ServerPlayer *p, unions) {
            if (p->isAlive() && p->getHandcardNum() < 4) {
                room->setTag("FirstRound", true); //For Manjuan
                try {
                    p->drawCards(4 - p->getHandcardNum());
                    room->setTag("FirstRound", false);
                }
                catch (TriggerEvent triggerEvent) {
                    if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                        room->setTag("FirstRound", false);
                    throw triggerEvent;
                }
            }
        }
    }
    if ((difficulty & (1 << BMDReward)) > 0) {
        foreach (ServerPlayer *p, unions) {
            if (p->isAlive()) {
                room->setTag("FirstRound", true); //For Manjuan
                try {
                    p->drawCards(2);
                    room->setTag("FirstRound", false);
                }
                catch (TriggerEvent triggerEvent) {
                    if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                        room->setTag("FirstRound", false);
                    throw triggerEvent;
                }
            }
        }
    }
    if (Config.value("BossModeExp", false).toBool()) {
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (p->isLord() || p->isDead()) continue;

            QMap<QString, int> exp_map;
            while (true) {
                QStringList choices;
                QStringList allchoices;
                int exp = p->getMark("@bossExp");
                int level = room->getTag("BossModeLevel").toInt();
                exp_map["drawcard"] = 20 * level;
                exp_map["recover"] = 30 * level;
                exp_map["maxhp"] = p->getMaxHp() * 10 * level;
                exp_map["recovermaxhp"] = (20 + p->getMaxHp() * 10) * level;
                foreach (QString c, exp_map.keys()) {
                    allchoices << QString("[%1]|%2").arg(exp_map[c]).arg(c);
                    if (exp >= exp_map[c] && (c != "recover" || p->isWounded()))
                        choices << QString("[%1]|%2").arg(exp_map[c]).arg(c);
                }

                QStringList acquired = p->tag["BossModeAcquiredSkills"].toStringList();
                foreach (QString a, acquired) {
                    if (!p->getAcquiredSkills().contains(a))
                        acquired.removeOne(a);
                }
                int len = qMin(4, acquired.length() + 1);
                foreach (QString skillname, Config.BossExpSkills.keys()) {
                    int cost = Config.BossExpSkills[skillname] * len;
                    allchoices << QString("[%1]||%2").arg(cost).arg(skillname);
                    if (p->hasSkill(skillname, true)) continue;
                    if (exp >= cost)
                        choices << QString("[%1]||%2").arg(cost).arg(skillname);
                }
                if (choices.isEmpty()) break;
                allchoices << "cancel";
                choices << "cancel";
                ServerPlayer *choiceplayer = p;
                if (!p->isOnline()) {
                    foreach (ServerPlayer *cp, room->getPlayers()) {
                        if (!cp->isLord() && cp->isOnline()) {
                            choiceplayer = cp;
                            break;
                        }
                    }
                }
                room->setPlayerProperty(choiceplayer, "bossmodeexp", p->objectName());
                room->setPlayerProperty(choiceplayer, "bossmodeacquiredskills", acquired.join("+"));
                room->setPlayerProperty(choiceplayer, "bossmodeexpallchoices", allchoices.join("+"));
                QString choice = room->askForChoice(choiceplayer, "BossModeExpStore", choices.join("+"));
                if (choice == "cancel") {
                    break;
                } else if (choice.contains("||")) { // skill
                    QStringList skilllist;
                    QString skillattach = choice.split("|").last();
                    if (acquired.length() == 4) {
                        QString skilldetach = room->askForChoice(choiceplayer, "BossModeExpStoreSkillDetach", acquired.join("+"));
                        skilllist << "-" + skilldetach;
                        acquired.removeOne(skilldetach);
                    }
                    skilllist.append(skillattach);
                    acquired.append(skillattach);
                    p->tag["BossModeAcquiredSkills"] = QVariant::fromValue(acquired);
                    int cost = choice.split("]").first().mid(1).toInt();

                    LogMessage log;
                    log.type = "#UseExpPoint";
                    log.from = p;
                    log.arg = QString::number(cost);
                    log.arg2 = "BossModeExpStore:acquireskill";
                    room->sendLog(log);

                    room->removePlayerMark(p, "@bossExp", cost);
                    room->handleAcquireDetachSkills(p, skilllist, true);
                } else {
                    QString type = choice.split("|").last();
                    int cost = choice.split("]").first().mid(1).toInt();
                    room->removePlayerMark(p, "@bossExp", cost);

                    LogMessage log;
                    log.type = "#UseExpPoint";
                    log.from = p;
                    log.arg = QString::number(cost);
                    log.arg2 = "BossModeExpStore:" + type;
                    room->sendLog(log);

                    if (type == "drawcard") {
                        room->setTag("FirstRound", true); //For Manjuan
                        try {
                            p->drawCards(1);
                            room->setTag("FirstRound", false);
                        }
                        catch (TriggerEvent triggerEvent) {
                            if (triggerEvent == TurnBroken || triggerEvent == StageChange)
                                room->setTag("FirstRound", false);
                            throw triggerEvent;
                        }
                    } else {
                        int maxhp = p->getMaxHp();
                        int hp = p->getHp();
                        if (type.contains("maxhp")) {
                            p->setProperty("maxhp", maxhp + 1);
                            room->broadcastProperty(p, "maxhp");
                        }
                        if (type.contains("recover")) {
                            p->setProperty("hp", hp + 1);
                            room->broadcastProperty(p, "hp");
                        }

                        LogMessage log2;
                        log2.type = "#GetHp";
                        log2.from = p;
                        log2.arg = QString::number(p->getHp());
                        log2.arg2 = QString::number(p->getMaxHp());
                        room->sendLog(log2);
                    }
                }
            }
        }
    }
}

void GameRule::rewardAndPunish(ServerPlayer *killer, ServerPlayer *victim) const
{
    Room *room = victim->getRoom();

    if (room->getMode() == "03_1v2") {
        if (victim->getRole() == "rebel") {
            foreach (ServerPlayer *p, room->getOtherPlayers(victim)) {
                if (p->getRole() == "rebel") {
                    QString result = room->askForChoice(p, "doudizhu", "draw+recover+cancel");
                    if (result == "draw")
                        p->drawCards(2);
                    else if (result == "recover")
                        room->recover(p, RecoverStruct(p, NULL, 1));
                }
            }
        }
        return;
    } else if (room->getMode() == "04_2v2") {
        foreach (ServerPlayer *p, room->getOtherPlayers(victim)) {
            if (p->getRole() == victim->getRole()) {
                p->drawCards(1);
                break;
            }
        }
        return;
    } else if (room->getMode() == "04_tt") {
        foreach (ServerPlayer *p, room->getOtherPlayers(victim)) {
            if (p->getRole() == victim->getRole()) {
                if (p->askForSkillInvoke("tietie_draw", QVariant("choice:"), false)) {
                    p->drawCards(1);
                    break;
                }
            }
        }
        return;
    } else if (room->getMode() == "04_if") {

        return;
    }

    if (!killer || killer->isDead() || room->getMode() == "06_XMode"
        || room->getMode() == "04_boss"
        || room->getMode() == "08_defense")
        return;

    if (room->getMode() == "06_3v3") {
        if (Config.value("3v3/OfficialRule", "2013").toString().startsWith("201"))
            killer->drawCards(2, "kill");
        else
            killer->drawCards(3, "kill");
    } else {
        if (victim->getRole() == "rebel" && killer != victim)
            killer->drawCards(3, "kill");
        else if (victim->getRole() == "loyalist" && killer->getRole() == "lord")
            killer->throwAllHandCardsAndEquips();
    }
}

QString GameRule::getWinner(ServerPlayer *victim) const
{
    Room *room = victim->getRoom();
    QString winner;

    if (room->getMode() == "06_3v3" || room->getMode() == "04_if") {
        switch (victim->getRoleEnum()) {
        case Player::Lord: winner = "renegade+rebel"; break;
        case Player::Renegade: winner = "lord+loyalist"; break;
        default:
            break;
        }
    } else if (room->getMode() == "06_XMode") {
        QString role = victim->getRole();
        ServerPlayer *leader = victim->tag["XModeLeader"].value<ServerPlayer *>();
        if (leader->tag["XModeBackup"].toStringList().isEmpty()) {
            if (role.startsWith('r'))
                winner = "lord+loyalist";
            else
                winner = "renegade+rebel";
        }
    } else if (room->getMode() == "08_defense" || room->getMode() == "04_2v2" || room->getMode() == "04_tt") {
        QStringList alive_roles = room->aliveRoles(victim);
        if (!alive_roles.contains("loyalist"))
            winner = "rebel";
        else if (!alive_roles.contains("rebel"))
            winner = "loyalist";
    } else if (Config.EnableHegemony) {
        bool has_anjiang = false, has_diff_kingdoms = false;
        QString init_kingdom;
        foreach (ServerPlayer *p, room->getAlivePlayers()) {
            if (!p->property("basara_generals").toString().isEmpty())
                has_anjiang = true;

            if (init_kingdom.isEmpty())
                init_kingdom = p->getKingdom();
            else if (init_kingdom != p->getKingdom())
                has_diff_kingdoms = true;
        }

        if (!has_anjiang && !has_diff_kingdoms) {
            QStringList winners;
            QString aliveKingdom = room->getAlivePlayers().first()->getKingdom();
            foreach (ServerPlayer *p, room->getPlayers()) {
                if (p->isAlive()) winners << p->objectName();
                if (p->getKingdom() == aliveKingdom) {
                    QStringList generals = p->property("basara_generals").toString().split("+");
                    if (generals.size() && !Config.Enable2ndGeneral) continue;
                    if (generals.size() > 1) continue;

                    //if someone showed his kingdom before death,
                    //he should be considered victorious as well if his kingdom survives
                    winners << p->objectName();
                }
            }
            winner = winners.join("+");
        }
        if (!winner.isNull()) {
            foreach (ServerPlayer *player, room->getAllPlayers()) {
                if (player->getGeneralName() == "anjiang") {
                    QStringList generals = player->property("basara_generals").toString().split("+");
                    room->changePlayerGeneral(player, generals.at(0));

                    room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());
                    room->setPlayerProperty(player, "role", BasaraMode::getMappedRole(player->getKingdom()));

                    generals.takeFirst();
                    player->setProperty("basara_generals", generals.join("+"));
                    room->notifyProperty(player, player, "basara_generals");
                }
                if (Config.Enable2ndGeneral && player->getGeneral2Name() == "anjiang") {
                    QStringList generals = player->property("basara_generals").toString().split("+");
                    room->changePlayerGeneral2(player, generals.at(0));
                }
            }
        }
    } else {
        QStringList alive_roles = room->aliveRoles(victim);
        switch (victim->getRoleEnum()) {
        case Player::Lord: {
            if (alive_roles.length() == 1 && alive_roles.first() == "renegade")
                winner = room->getAlivePlayers().first()->objectName();
            else
                winner = "rebel";
            break;
        }
        case Player::Rebel:
        case Player::Renegade: {
            if (!alive_roles.contains("rebel") && !alive_roles.contains("renegade"))
                winner = "lord+loyalist";
            break;
        }
        default:
            break;
        }
    }

    return winner;
}

HulaoPassMode::HulaoPassMode(QObject *parent)
    : GameRule(parent)
{
    setObjectName("hulaopass_mode");
    events << HpChanged << StageChange;
}

bool HulaoPassMode::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    switch (triggerEvent) {
    case StageChange: {
        ServerPlayer *lord = room->getLord();
        room->setPlayerMark(lord, "secondMode", 1);
        QString lvbu = room->askForChoice(lord, "hulaopass_shenlvbu", "bnzs+sgwq");
        if (lvbu == "bnzs")
            room->changeHero(lord, "shenlvbu2", true, true, false, false);
        else
            room->changeHero(lord, "shenlvbu3", true, true, false, false);

        LogMessage log;
        log.type = "$AppendSeparator";
        room->sendLog(log);

        log.type = "#HulaoTransfigure";
        log.arg = "#shenlvbu1";
        log.arg2 = lvbu == "bnzs" ? "#shenlvbu2" : "#shenlvbu3";
        room->sendLog(log);

        //room->doLightbox("$StageChange", 5000);
        if (lvbu == "bnzs")
            room->doSuperLightbox("shenlvbu2", "StageChange");
        else
            room->doSuperLightbox("shenlvbu3", "StageChange");

        QList<const Card *> tricks = lord->getJudgingArea();
        if (!tricks.isEmpty()) {
            DummyCard *dummy = new DummyCard;
            foreach(const Card *trick, tricks)
                dummy->addSubcard(trick);
            CardMoveReason reason(CardMoveReason::S_REASON_NATURAL_ENTER, QString());
            room->throwCard(dummy, reason, NULL);
            delete dummy;
        }
        if (!lord->faceUp())
            lord->turnOver();
        if (lord->isChained())
            room->setPlayerChained(lord);
        break;
    }
    case GameReady: {
        // Handle global events
        if (player == NULL) {
            ServerPlayer *lord = room->getLord();
            lord->drawCards(8);
            foreach (ServerPlayer *player, room->getPlayers()) {
                if (!player->isLord())
                    player->drawCards(player->getSeat() + 1);
            }
            return false;
        }
        break;
    }
    case HpChanged: {
        if (player->isLord() && player->getHp() <= 4 && player->getMark("secondMode") == 0)
            throw StageChange;
        break;
    }
    case GameOverJudge: {
        if (player->isLord())
            room->gameOver("rebel");
        else
            if (room->aliveRoles(player).length() == 1)
                room->gameOver("lord");

        return false;
    }
    case BuryVictim: {
        if (player->hasFlag("actioned")) room->setPlayerFlag(player, "-actioned");

        LogMessage log;
        log.type = "#Reforming";
        log.from = player;
        room->sendLog(log);

        player->bury();
        room->setPlayerProperty(player, "hp", 0);

        foreach (ServerPlayer *p, room->getOtherPlayers(room->getLord())) {
            if (p->isAlive() && p->askForSkillInvoke("draw_1v3"))
                p->drawCards(1, "draw_1v3");
        }

        return false;
    }
    case TurnStart: {
        if (player->isDead()) {
            JsonArray arg;
            arg << QSanProtocol::S_GAME_EVENT_PLAYER_REFORM << player->objectName();
            room->doBroadcastNotify(QSanProtocol::S_COMMAND_LOG_EVENT, arg);

            QString choice = player->isWounded() ? "recover" : "draw";
            if (player->isWounded() && player->getHp() > 0)
                choice = room->askForChoice(player, "Hulaopass", "recover+draw");

            if (choice == "draw") {
                LogMessage log;
                log.type = "#ReformingDraw";
                log.from = player;
                log.arg = "1";
                room->sendLog(log);
                player->drawCards(1, "reform");
            } else {
                LogMessage log;
                log.type = "#ReformingRecover";
                log.from = player;
                log.arg = "1";
                room->sendLog(log);
                room->setPlayerProperty(player, "hp", player->getHp() + 1);
            }

            if (player->getHp() + player->getHandcardNum() == 6) {
                LogMessage log;
                log.type = "#ReformingRevive";
                log.from = player;
                room->sendLog(log);

                room->revivePlayer(player);
            }
        } else {
            LogMessage log;
            log.type = "$AppendSeparator";
            room->sendLog(log);
            room->addPlayerMark(player, "Global_TurnCount");
            if (room->getTag("Global_ExtraTurn" + player->objectName()).toBool()) {
                room->setPlayerMark(player, "@extra_turn", 1);
            } else {
                if (player->objectName() == room->getAlivePlayers().first()->objectName()) {
                    int n = room->getTag("TurnLengthCount").toInt();
                    room->setTag("TurnLengthCount", n + 1);
                    QVariant rsdata = n + 1;
                    room->doBroadcastNotify(QSanProtocol::S_COMMAND_ADD_ROUND, QVariant());
                    foreach (ServerPlayer *p, room->getPlayers()) {
                        foreach (QString mark, p->getMarkNames()) {
                            if (mark.endsWith("_lun") || mark.endsWith("_lun!"))
                                room->setPlayerMark(p, mark, 0);
                        }
                    }
                    if (n == 0) {
                        foreach(ServerPlayer *p, room->getAlivePlayers())
                            room->getThread()->trigger(BeforeGameStart, room, p);
                        foreach(ServerPlayer *p, room->getAlivePlayers())
                            room->getThread()->trigger(GameStart, room, p);
                    }
                    foreach(ServerPlayer *p, room->getAlivePlayers())
                        room->getThread()->trigger(BeforeRoundStart, room, p, rsdata);
                    foreach(ServerPlayer *p, room->getAlivePlayers())
                        room->getThread()->trigger(RoundStart, room, p, rsdata);
                }
            }

            if (!player->faceUp()) {
                room->removeTag("Global_ExtraTurn" + player->objectName());
                room->setPlayerMark(player, "@extra_turn", 0);
                player->turnOver();
            } else {
                player->play();
            }
        }

        return false;
    }
    default:
        break;
    }

    return GameRule::trigger(triggerEvent, room, player, data);
}

BasaraMode::BasaraMode(QObject *parent)
    : GameRule(parent)
{
    setObjectName("basara_mode");
    events << EventPhaseStart << DamageInflicted << BeforeGameOverJudge;
}

QString BasaraMode::getMappedRole(const QString &role)
{
    static QMap<QString, QString> roles;
    if (roles.isEmpty()) {
        roles["wei"] = "lord";
        roles["shu"] = "loyalist";
        roles["wu"] = "rebel";
        roles["qun"] = "renegade";
    }
    return roles[role];
}

int BasaraMode::getPriority(TriggerEvent) const
{
    return 15;
}

void BasaraMode::playerShowed(ServerPlayer *player) const
{
    Room *room = player->getRoom();
    QString name = player->property("basara_generals").toString();
    if (name.isEmpty())
        return;
    QStringList names = name.split("+");

    if (Config.EnableHegemony) {
        QMap<QString, int> kingdom_roles;
        foreach(ServerPlayer *p, room->getOtherPlayers(player))
            kingdom_roles[p->getKingdom()]++;

        if (kingdom_roles[Sanguosha->getGeneral(names.first())->getKingdom()] >= Config.value("HegemonyMaxShown", 2).toInt()
            && player->getGeneralName() == "anjiang")
            return;
    }

    QString answer = room->askForChoice(player, "RevealGeneral", "yes+no");
    if (answer == "yes") {
        QString general_name = room->askForGeneral(player, names);

        generalShowed(player, general_name);
        if (Config.EnableHegemony) room->getThread()->trigger(GameOverJudge, room, player);
        playerShowed(player);
    }
}

void BasaraMode::generalShowed(ServerPlayer *player, QString general_name) const
{
    Room *room = player->getRoom();
    QString name = player->property("basara_generals").toString();
    if (name.isEmpty())
        return;
    QStringList names = name.split("+");

    if (player->getGeneralName() == "anjiang") {
        room->changeHero(player, general_name, false, false, false, false);
        room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());

        if (player->getGeneral()->getKingdom() == "god")
            room->setPlayerProperty(player, "kingdom", room->askForKingdom(player));

        if (Config.EnableHegemony)
            room->setPlayerProperty(player, "role", getMappedRole(player->getKingdom()));
    } else {
        room->changeHero(player, general_name, false, false, true, false);
    }

    names.removeOne(general_name);
    player->setProperty("basara_generals", names.join("+"));
    room->notifyProperty(player, player, "basara_generals");

    LogMessage log;
    log.type = "#BasaraReveal";
    log.from = player;
    log.arg = player->getGeneralName();
    if (player->getGeneral2()) {
        log.type = "#BasaraRevealDual";
        log.arg2 = player->getGeneral2Name();
    }
    room->sendLog(log);
}

bool BasaraMode::trigger(TriggerEvent triggerEvent, Room *room, ServerPlayer *player, QVariant &data) const
{
    // Handle global events
    if (player == NULL) {
        if (triggerEvent == GameReady) {
            if (Config.EnableHegemony)
                room->setTag("SkipNormalDeathProcess", true);
            foreach (ServerPlayer *sp, room->getAlivePlayers()) {
                room->setPlayerProperty(sp, "general", "anjiang");
                sp->setGender(General::Sexless);
                room->setPlayerProperty(sp, "kingdom", "god");

                LogMessage log;
                log.type = "#BasaraGeneralChosen";
                log.arg = sp->property("basara_generals").toString().split("+").first();

                if (Config.Enable2ndGeneral) {
                    room->setPlayerProperty(sp, "general2", "anjiang");
                    log.type = "#BasaraGeneralChosenDual";
                    log.arg2 = sp->property("basara_generals").toString().split("+").last();
                }

                room->sendLog(log, sp);
            }
        }
        return false;
    }

    player->tag["triggerEvent"] = triggerEvent;
    player->tag["triggerEventData"] = data; // For AI

    switch (triggerEvent) {
    case CardEffected: {
        if (player->getPhase() == Player::NotActive) {
            CardEffectStruct ces = data.value<CardEffectStruct>();
            if (ces.card)
                if (ces.card->isKindOf("TrickCard") || ces.card->isKindOf("Slash"))
                    playerShowed(player);

            const ProhibitSkill *prohibit = room->isProhibited(ces.from, ces.to, ces.card);
            if (prohibit) {
                if (prohibit->isVisible() && ces.to->hasSkill(prohibit)) {
                    LogMessage log;
                    log.type = "#SkillAvoid";
                    log.from = ces.to;
                    log.arg = prohibit->objectName();
                    log.arg2 = ces.card != NULL ? ces.card->objectName() : "";
                    room->sendLog(log);

                    room->broadcastSkillInvoke(prohibit->objectName());
                    room->notifySkillInvoked(ces.to, prohibit->objectName());
                } else {
                    const Skill *skill = Sanguosha->getMainSkill(prohibit->objectName());
                    if (skill && skill->isVisible() && ces.to->hasSkill(skill)) {
                        QString skill_name = skill->objectName();
                        LogMessage log;
                        log.type = "#SkillAvoid";
                        log.from = ces.to;
                        log.arg = skill_name;
                        log.arg2 = ces.card != NULL ? ces.card->objectName() : "";
                        room->sendLog(log);

                        room->broadcastSkillInvoke(skill_name);
                        room->notifySkillInvoked(ces.to, skill_name);
                    }

                }

                return true;
            }
        }
        break;
    }
    case EventPhaseStart: {
        if (player->getPhase() == Player::RoundStart)
            playerShowed(player);

        break;
    }
    case DamageInflicted: {
        playerShowed(player);
        break;
    }
    case BeforeGameOverJudge: {
        if (player->getGeneralName() == "anjiang") {
            QStringList generals = player->property("basara_generals").toString().split("+");
            room->changePlayerGeneral(player, generals.at(0));

            room->setPlayerProperty(player, "kingdom", player->getGeneral()->getKingdom());
            if (Config.EnableHegemony)
                room->setPlayerProperty(player, "role", getMappedRole(player->getKingdom()));

            generals.takeFirst();
            player->setProperty("basara_generals", generals.join("+"));
            room->notifyProperty(player, player, "basara_generals");
        }
        if (Config.Enable2ndGeneral && player->getGeneral2Name() == "anjiang") {
            QStringList generals = player->property("basara_generals").toString().split("+");
            room->changePlayerGeneral2(player, generals.at(0));
            player->setProperty("basara_generals", QString());
            room->notifyProperty(player, player, "basara_generals");
        }
        break;
    }
    case BuryVictim: {
        DeathStruct death = data.value<DeathStruct>();
        player->bury();
        if (Config.EnableHegemony) {
            ServerPlayer *killer = death.damage ? death.damage->from : NULL;
            if (killer && killer->getKingdom() != "god") {
                if (killer->getKingdom() == player->getKingdom())
                    killer->throwAllHandCardsAndEquips();
                else if (killer->isAlive())
                    killer->drawCards(3, "kill");
            }
            return true;
        }

        break;
    }
    default:
        break;
    }
    return false;
}

