#ifndef _CARD_H
#define _CARD_H

class Room;
class Player;
class ServerPlayer;
class Client;
class ClientPlayer;
class CardItem;

struct CardEffectStruct;
struct CardUseStruct;

#include "src/pch.h"

class Card : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString suit READ getSuitString CONSTANT)
    Q_PROPERTY(bool red READ isRed STORED false CONSTANT)
    Q_PROPERTY(bool black READ isBlack STORED false CONSTANT)
    Q_PROPERTY(int id READ getId CONSTANT)
    Q_PROPERTY(int number READ getNumber WRITE setNumber)
    Q_PROPERTY(QString number_string READ getNumberString CONSTANT)
    Q_PROPERTY(QString type READ getType CONSTANT)
    Q_PROPERTY(bool target_fixed READ targetFixed)
    Q_PROPERTY(bool mute READ isMute CONSTANT)
    Q_PROPERTY(bool equipped READ isEquipped)
    Q_PROPERTY(Color color READ getColor)
    Q_PROPERTY(bool can_recast READ canRecast WRITE setCanRecast)
    Q_PROPERTY(bool is_gift READ isGift WRITE setGift)
    Q_PROPERTY(bool damage_card READ isDamageCard WRITE setDamageCard)

    Q_ENUMS(Suit)
    Q_ENUMS(CardType)
    Q_ENUMS(HandlingMethod)

public:
    // enumeration type
    enum Suit
    {
        Spade, Club, Heart, Diamond, NoSuitBlack, NoSuitRed, NoSuit, SuitToBeDecided = -1
    };
    enum Color
    {
        Red, Black, Colorless
    };
    enum HandlingMethod
    {
        MethodNone, MethodUse, MethodResponse, MethodDiscard, MethodRecast, MethodPindian
    };

    static const Suit AllSuits[4];

    // card types
    enum CardType
    {
        TypeSkill, TypeBasic, TypeTrick, TypeEquip
    };

    // constructor
    Card(Suit suit, int number, bool target_fixed = false);

    // property getters/setters
    QString getSuitString() const;
    bool isRed() const;
    bool isBlack() const;
    int getId() const;
    virtual void setId(int id);
    int getEffectiveId() const;

    int getNumber() const;
    virtual void setNumber(int number);
    QString getNumberString() const;

    Suit getSuit() const;
    virtual void setSuit(Suit suit);

    bool sameColorWith(const Card *other) const;
    bool sameNameWith(const Card *card, bool different_slash = false) const;
    bool sameNameWith(const QString &card_name, bool different_slash = false) const;
    Color getColor() const;
    QString getFullName(bool include_suit = false) const;
    QString getLogName() const;
    QString getName() const;
    QString getSkillName(bool removePrefix = true) const;
    virtual void setSkillName(const QString &skill_name);
    QString getDescription() const;
    virtual bool isGift() const;
    virtual void setGift(bool flag);

    virtual bool isMute() const;
    virtual void setMute(bool flag);
    virtual bool willThrow() const;
    virtual bool canRecast() const;
    virtual bool isOvert() const;
    virtual bool noIndicator() const;
    virtual bool isCopy() const;
    virtual bool isUnknownCard() const;
    virtual bool hasPreAction() const;
    virtual Card::HandlingMethod getHandlingMethod() const;
    void setCanRecast(bool can);
    void setOvert(bool can);
    void setIndicatorHide(bool can);
    void setCopy(bool can);
    void setUnknownCard(bool can);
    void setDamageCard(bool flag);

    virtual void setFlags(const QString &flag) const;
    inline virtual void setFlags(const QStringList &fs)
    {
        flags = fs;
    }
    bool hasFlag(const QString &flag) const;
    virtual void clearFlags() const;

    virtual void setTag(const QString &key, const QVariant &data) const;
    virtual void removeTag(const QString &key) const;

    virtual QString getPackage() const;
    inline virtual QString getClassName() const
    {
        return metaObject()->className();
    }
    virtual bool isVirtualCard() const;
    virtual bool isEquipped() const;
    virtual QString getCommonEffectName() const;
    virtual bool match(const QString &pattern) const;

    virtual void addSubcard(int card_id);
    virtual void addSubcard(const Card *card);
    virtual QList<int> getSubcards() const;
    virtual void clearSubcards();
    virtual QString subcardString() const;
    virtual void addSubcards(const QList<const Card *> &cards);
    virtual void addSubcards(const QList<int> &subcards_list);
    virtual int subcardsLength() const;
    virtual void shuffleSubcards();

    virtual QString getType() const = 0;
    virtual QString getSubtype() const = 0;
    virtual CardType getTypeId() const = 0;
    virtual bool isNDTrick() const;
    virtual bool isDamageCard() const;

    // card target selection
    virtual bool targetFixed() const;
    virtual bool targetsFeasible(const QList<const Player *> &targets, const Player *Self) const;
    // @todo: the following two functions should be merged into one.
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self) const;
    virtual bool targetFilter(const QList<const Player *> &targets, const Player *to_select, const Player *Self, int &maxVotes) const;
    virtual bool isAvailable(const Player *player) const;

    inline virtual const Card *getRealCard() const
    {
        return this;
    }
    virtual const Card *validate(CardUseStruct &cardUse) const;
    virtual const Card *validateInResponse(ServerPlayer *user) const;

    virtual void doPreAction(Room *room, const CardUseStruct &card_use) const;
    virtual void onUse(Room *room, const CardUseStruct &card_use) const;
    virtual void use(Room *room, ServerPlayer *source, QList<ServerPlayer *> &targets) const;
    virtual void onEffect(const CardEffectStruct &effect) const;
    virtual bool isCancelable(const CardEffectStruct &effect) const;

    inline virtual bool isKindOf(const char *cardType) const
    {
        return inherits(cardType);
    }
    inline virtual QStringList getFlags() const
    {
        return flags;
    }

    inline virtual bool isModified() const
    {
        return false;
    }
    inline virtual void onNullified(ServerPlayer *) const
    {
        return;
    }

    // static functions
    static bool CompareByNumber(const Card *a, const Card *b);
    static bool CompareBySuit(const Card *a, const Card *b);
    static bool CompareByType(const Card *a, const Card *b);
    static Card *Clone(const Card *card);
    static QString Suit2String(Suit suit);
    static const int S_UNKNOWN_CARD_ID;

    static const Card *Parse(const QString &str);
    virtual QString toString(bool hidden = false) const;

    mutable QVariantMap tag;

protected:
    QList<int> subcards;
    bool target_fixed;
    bool mute;
    bool will_throw;
    bool has_preact;
    bool can_recast;
    Suit m_suit;
    int m_number;
    int m_id;
    QString m_skillName;
    bool is_gift;
    bool damage_card;
    Card::HandlingMethod handling_method;
    bool is_overt;
    bool no_indicator;
    bool is_copy;
    bool is_unknown_card;

    mutable QStringList flags;
};

class SkillCard : public Card
{
    Q_OBJECT

public:
    SkillCard();
    void setUserString(const QString &user_string);
    QString getUserString() const;

    virtual QString getSubtype() const;
    virtual QString getType() const;
    virtual CardType getTypeId() const;
    virtual QString toString(bool hidden = false) const;

protected:
    QString user_string;
};

class DummyCard : public SkillCard
{
    Q_OBJECT

public:
    DummyCard();
    DummyCard(const QList<int> &subcards);

    QString getSubtype() const;
    QString getType() const;
    QString toString(bool hidden = false) const;
};

#endif

