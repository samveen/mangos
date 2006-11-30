/*
 * Copyright (C) 2005,2006 MaNGOS <http://www.mangosproject.org/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Log.h"
#include "Opcodes.h"
#include "ObjectMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "UpdateMask.h"
#include "Player.h"
#include "QuestDef.h"
#include "GossipDef.h"
#include "Spell.h"
#include "UpdateData.h"
#include "Channel.h"
#include "Chat.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "CreatureAI.h"
#include "Formulas.h"
#include "Group.h"
#include "Guild.h"
#include "Pet.h"
#include "SpellAuras.h"
#include "Util.h"
#include "Transports.h"

#include <cmath>

Player::Player (WorldSession *session): Unit()
{
    m_transport = NULL;
    m_transX = 0.0f;
    m_transY = 0.0f;
    m_transZ = 0.0f;
    m_transO = 0.0f;

    m_objectType |= TYPE_PLAYER;
    m_objectTypeId = TYPEID_PLAYER;

    m_valuesCount = PLAYER_END;

    m_session = session;

    m_divider = 0;

    m_GMFlags = 0;
    if(GetSession()->GetSecurity() >=2)
        SetAcceptTicket(true);
    if(GetSession()->GetSecurity() >=1 && sWorld.getConfig(CONFIG_GM_WISPERING_TO))
        SetAcceptWhispers(true);

    m_curTarget = 0;
    m_curSelection = 0;
    m_lootGuid = 0;
    m_petInfoId = 0;
    m_petLevel = 0;
    m_petFamilyId = 0;

    m_regenTimer = 0;
    m_dismountCost = 0;

    m_nextSave = sWorld.getConfig(CONFIG_INTERVAL_SAVE);

    m_resurrectGUID = 0;
    m_resurrectX = m_resurrectY = m_resurrectZ = 0;
    m_resurrectHealth = m_resurrectMana = 0;

    memset(m_items, 0, sizeof(Item*)*BUYBACK_SLOT_END);
    memset(m_buybackitems, 0, sizeof(Item*)*(BUYBACK_SLOT_END - BUYBACK_SLOT_START));

    //m_pDuel       = NULL;
    //m_pDuelSender = NULL;
    //m_isInDuel = false;
    duel = NULL;

    m_GuildIdInvited = 0;

    m_groupLeader = 0;
    m_isInGroup = false;
    m_isInvited = false;

    m_dontMove = false;

    m_total_honor_points = 0;

    pTrader = NULL;

    ClearTrade();

    m_cinematic = 0;

    PlayerTalkClass = new PlayerMenu( GetSession() );
    m_currentBuybackSlot = BUYBACK_SLOT_START;

    for ( int aX = 0 ; aX < 8 ; aX++ )
        m_Tutorials[ aX ] = 0x00;
    ItemsSetEff[0]=NULL;
    ItemsSetEff[1]=NULL;
    ItemsSetEff[2]=NULL;
    m_regenTimer = 0;
    m_breathTimer = 0;
    m_isunderwater = 0;
    m_drunkTimer = 0;
    m_drunk = 0;
    m_restTime = 0;
    m_lastManaUse = 0;
    m_deathTimer = 0;

    m_pvp_count = 0;
    m_pvp_counting = false;

    m_bgInBattleGround = false;
    m_bgBattleGroundID = 0;

    m_movement_flags = 0;

    m_BlockValue = 0;

    m_logintime = time(NULL);
    m_Last_tick = m_logintime;
    m_soulStone = NULL;
    m_soulStoneSpell = 0;
    m_WeaponProficiency = 0;
    m_ArmorProficiency = 0;
    m_canParry = false;
    m_canDualWield = false;

    ////////////////////Rest System/////////////////////
    time_inn_enter=0;
    inn_pos_x=0;
    inn_pos_y=0;
    inn_pos_z=0;
    rest_bonus=0;
    rest_type=0;
    ////////////////////Rest System/////////////////////

    m_mailsLoaded = false;
    m_mailsUpdated = false;

    m_resetTalentsCost = 0;
    m_resetTalentsTime = 0;
    m_itemUpdateQueueBlocked = false;
}

Player::~Player ()
{
    DuelComplete(0);

    CombatStop();

    TradeCancel(false);

    RemoveAllAuras();

    uint32 eslot;
    for(int j = BUYBACK_SLOT_START; j < BUYBACK_SLOT_END; j++)
    {
        eslot = j - BUYBACK_SLOT_START;
        if(m_buybackitems[eslot])
            delete m_buybackitems[eslot];
        // already deleted from DB when player was saved
    }
    for(int i = 0; i < BANK_SLOT_BAG_END; i++)
    {
        if(m_items[i])
            delete m_items[i];
    }
    CleanupChannels();

    //all mailed items should be deleted, also all mail should be dealocated
    for (std::list<Mail*>::iterator itr =  m_mail.begin(); itr != m_mail.end();++itr)
        delete *itr;

    for (ItemMap::iterator iter = mMitems.begin(); iter != mMitems.end(); ++iter)
        delete iter->second;                                //if item is duplicated... then server may crash ... but that item should be dealocated

    delete PlayerTalkClass;

    if (m_transport)
    {
        m_transport->RemovePassenger(this);
    }
}

bool Player::Create( uint32 guidlow, WorldPacket& data )
{
    int i;
    uint8 race,class_,gender,skin,face,hairStyle,hairColor,facialHair,outfitId;

    Object::_Create(guidlow, HIGHGUID_PLAYER);

    data >> m_name;

    if(m_name.size() == 0)
        return false;

    normalizePlayerName(m_name);

    data >> race >> class_ >> gender >> skin >> face;
    data >> hairStyle >> hairColor >> facialHair >> outfitId;

    PlayerInfo const* info = objmgr.GetPlayerInfo(race, class_);
    if(!info)
    {
        sLog.outError("Player have incorrect race/class pair. Can't be loaded.");
        return false;
    }

    for (i = 0; i < BANK_SLOT_BAG_END; i++)
        m_items[i] = NULL;

    uint32 eslot;
    for(int j = BUYBACK_SLOT_START; j < BUYBACK_SLOT_END; j++)
    {
        eslot = j - BUYBACK_SLOT_START;
        m_buybackitems[eslot] = NULL;
        //        SetUInt64Value(PLAYER_FIELD_VENDORBUYBACK_SLOT_1+j*2,0);
        //        SetUInt32Value(PLAYER_FIELD_BUYBACK_PRICE_1+j,0);
        //        SetUInt32Value(PLAYER_FIELD_BUYBACK_TIMESTAMP_1+j,0);
    }

    m_race = race;
    m_class = class_;

    m_mapId = info->mapId;
    m_positionX = info->positionX;
    m_positionY = info->positionY;
    m_positionZ = info->positionZ;

    // Taxi nodes setup
    memset(m_taximask, 0, sizeof(m_taximask));

    // Automatically add the race's taxi hub to the character's taximask at creation time ( 1 << (taxi_node_id-1) )
    ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(race);
    if(!rEntry)
    {
        sLog.outError("Race %u not found in DB� (Wrong DBC files?)",race);
        return false;
    }

    m_taximask[0] = rEntry->startingTaxiMask;

    ChrClassesEntry const* cEntry = sChrClassesStore.LookupEntry(class_);
    if(!cEntry)
    {
        sLog.outError("Class %u not found in DB� (Wrong DBC files?)",class_);
        return false;
    }

    uint8 powertype = cEntry->powerType;

    uint32 unitfield;
    if(powertype == POWER_RAGE)
        unitfield = 0x1100EE00;
    else if(powertype == POWER_ENERGY)
        unitfield = 0x00000000;
    else if(powertype == POWER_MANA)
        unitfield = 0x0000EE00;
    else
    {
        sLog.outError("Invalid default powertype %u for player (class %u)",powertype,class_);
        return false;
    }

    if ( race == RACE_TAUREN )
        SetFloatValue(OBJECT_FIELD_SCALE_X, 1.35f);
    else
        SetFloatValue(OBJECT_FIELD_SCALE_X, 1.0f);

    SetFloatValue(UNIT_FIELD_BOUNDINGRADIUS, 0.388999998569489f );
    SetFloatValue(UNIT_FIELD_COMBATREACH, 1.5f   );

    SetUInt32Value(UNIT_FIELD_DISPLAYID, info->displayId + gender );
    SetUInt32Value(UNIT_FIELD_NATIVEDISPLAYID, info->displayId + gender );

    setFactionForRace(m_race);

    SetUInt32Value(UNIT_FIELD_BYTES_0, ( ( race ) | ( class_ << 8 ) | ( gender << 16 ) | ( powertype << 24 ) ) );
    SetUInt32Value(UNIT_FIELD_BYTES_1, unitfield );
    SetUInt32Value(UNIT_FIELD_BYTES_2, 0xEEEEEE00 );
    SetUInt32Value(UNIT_FIELD_FLAGS , UNIT_FLAG_NONE | UNIT_FLAG_ALLOW_SWIM );

    SetUInt32Value(UNIT_DYNAMIC_FLAGS, 0x10);
                                                            //-1 is default value
    SetUInt32Value(PLAYER_FIELD_WATCHED_FACTION_INDEX, uint32(-1));

    SetUInt32Value(PLAYER_BYTES, ((skin) | (face << 8) | (hairStyle << 16) | (hairColor << 24)));
    SetUInt32Value(PLAYER_BYTES_2, (facialHair | (0xEE << 8) | (0x00 << 16) | (0x02 << 24)));
    SetUInt32Value(PLAYER_BYTES_3, gender);
    SetUInt32Value(PLAYER_FIELD_BYTES, 0xEEE00000 );

    /*
        SetUInt32Value(PLAYER_GUILDID, 0);
        SetUInt32Value(PLAYER_GUILDRANK, 0);
        SetUInt32Value(PLAYER_GUILD_TIMESTAMP, 0);

        SetUInt32Value(PLAYER_FIELD_HONOR_RANK, 0);
        SetUInt32Value(PLAYER_FIELD_HONOR_HIGHEST_RANK, 0);    SetUInt32Value(PLAYER_FIELD_TODAY_KILLS, 0);    SetUInt32Value(PLAYER_FIELD_YESTERDAY_HONORABLE_KILLS, 0);    SetUInt32Value(PLAYER_FIELD_LAST_WEEK_HONORABLE_KILLS, 0);    SetUInt32Value(PLAYER_FIELD_THIS_WEEK_HONORABLE_KILLS, 0);    SetUInt32Value(PLAYER_FIELD_THIS_WEEK_HONOR, 0);    SetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS, 0);    SetUInt32Value(PLAYER_FIELD_LIFETIME_DISHONORABLE_KILLS, 0);    SetUInt32Value(PLAYER_FIELD_YESTERDAY_HONOR, 0);    SetUInt32Value(PLAYER_FIELD_LAST_WEEK_HONOR, 0);    SetUInt32Value(PLAYER_FIELD_LAST_WEEK_STANDING, 0);    SetUInt32Value(PLAYER_FIELD_LIFETIME_HONOR, 0);    SetUInt32Value(PLAYER_FIELD_SESSION_KILLS, 0);
    */

    InitStatsForLevel(1,false,false);

    // Played time
    m_Last_tick = time(NULL);
    m_Played_time[0] = 0;
    m_Played_time[1] = 0;

    uint32 titem_id;
    uint32 titem_amount;
    uint16 tspell, tskill[3], taction[4];
    std::list<uint16>::const_iterator skill_itr[3], action_itr[4];
    std::list<CreateSpellPair>::const_iterator spell_itr;

    spell_itr = info->spell.begin();

    for (; spell_itr!=info->spell.end(); spell_itr++)
    {
        tspell = spell_itr->first;
        if (tspell)
        {
            sLog.outDebug("PLAYER: Adding initial spell, id = %u",tspell);
            addSpell(tspell,spell_itr->second);
        }
    }

    for(i=0 ; i<3; i++)
        skill_itr[i] = info->skill[i].begin();

    for (; skill_itr[0]!=info->skill[0].end() && skill_itr[1]!=info->skill[1].end() && skill_itr[2]!=info->skill[2].end(); )
    {
        for (i=0; i<3; i++)
            tskill[i] = (*skill_itr[i]);

        if (tskill[0])
        {
            sLog.outDebug("PLAYER: Adding initial skill line, skillId = %u, value = %u, max = %u", tskill[0], tskill[1], tskill[2]);
            SetSkill(tskill[0], tskill[1], tskill[2]);
        }

        for(i=0; i<3; i++)
            skill_itr[i]++;
    }

    for(i=0; i<4; i++)
        action_itr[i] = info->action[i].begin();

    for (; action_itr[0]!=info->action[0].end() && action_itr[1]!=info->action[1].end();)
    {
        for( i=0; i<4 ;i++)
            taction[i] = (*action_itr[i]);

        addAction((uint8)taction[0], taction[1], (uint8)taction[2], (uint8)taction[3]);

        for( i=0; i<4 ;i++)
            action_itr[i]++;
    }

    m_petInfoId = 0;
    m_petLevel = 0;
    m_petFamilyId = 0;

    m_rating = 0;
    m_highest_rank = 0;
    m_standing = 0;

    UpdateBlockPercentage();

    // apply original stats mods before item equipment that call before equip _RemoveStatsMods()
    _ApplyStatsMods();

    uint16 dest;
    uint8 msg;
    Item *pItem;
    for (PlayerCreateInfoItems::const_iterator item_id_itr = info->item.begin(); item_id_itr!=info->item.end(); ++item_id_itr++)
    {
        titem_id     = item_id_itr->item_id;
        titem_amount = item_id_itr->item_amount;

        if (titem_id)
        {
            sLog.outDebug("STORAGE: Creating initial item, itemId = %u, count = %u",titem_id, titem_amount);

            pItem = CreateItem( titem_id, titem_amount);
            if( pItem )
            {
                msg = CanEquipItem( NULL_SLOT, dest, pItem, false );
                if( msg == EQUIP_ERR_OK )
                    EquipItem( dest, pItem, true);
                else
                {
                    // store in main bag to simplify second pass
                    msg = CanStoreItem( INVENTORY_SLOT_BAG_0, NULL_SLOT, dest, pItem, false );
                    if( msg == EQUIP_ERR_OK )
                        StoreItem( dest, pItem, true);
                    else
                    {
                        sLog.outError("STORAGE: Can't equip or store initial item %u for race %u class %u , error msg = %u",titem_id,race,class_,msg);
                        delete pItem;
                    }
                }
            }
            else
                sLog.outError("STORAGE: Can't create initial item %u (not existed item id) for race %u class %u , error msg = %u",titem_id,race,class_,msg);
        }
    }

    // bags and main-hamd weapon must equiped ant this moment
    // now second pass for not equiped (offhand weapon/shield if it attempt equiped before main-hand weapon)
    // or ammo not equiped in special bag
    for(int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        int16 pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
        pItem = GetItemByPos( pos );

        if(pItem)
        {
            // equip offhand weapon/shield if it attempt equiped before main-hand weapon
            msg = CanEquipItem( NULL_SLOT, dest, pItem, false );
            if( msg == EQUIP_ERR_OK )
            {
                RemoveItem(INVENTORY_SLOT_BAG_0, i,true);
                EquipItem( dest, pItem, true);
            }else
            // move other items to more appropriate slots (ammo not equiped in special bag)
            {
                msg = CanStoreItem( NULL_BAG, NULL_SLOT, dest, pItem, false );
                if( msg == EQUIP_ERR_OK )
                {
                    RemoveItem(INVENTORY_SLOT_BAG_0, i,true);
                    StoreItem( dest, pItem, true);
                }

                // if  this is ammo then use it
                uint8 msg = CanUseAmmo( pItem->GetProto()->ItemId );
                if( msg == EQUIP_ERR_OK )
                    SetUInt32Value(PLAYER_AMMO_ID, pItem->GetProto()->ItemId);
            }
        }
    }
    // all item positions resolved

    // remove applied original stats mods before item equipment
    _RemoveStatsMods();
    return true;
}

void Player::StartMirrorTimer(MirrorTimerType Type, uint32 MaxValue)
{
    WorldPacket data;
    uint32 BreathRegen = (uint32)-1;
    data.Initialize(SMSG_START_MIRROR_TIMER);
    data << (uint32)Type;
    data << MaxValue;
    data << MaxValue;
    data << BreathRegen;
    data << (uint32)0;
    data << (uint8)0;
    GetSession()->SendPacket(&data);
}

void Player::ModifyMirrorTimer(MirrorTimerType Type, uint32 MaxValue, uint32 CurrentValue, uint32 Regen)
{
    if(Type==BREATH_TIMER)
        m_breathTimer = ((MaxValue + 1000) - CurrentValue) / Regen;

    WorldPacket data;
    data.Initialize(SMSG_START_MIRROR_TIMER);
    data << (uint32)Type;
    data << CurrentValue;
    data << MaxValue;
    data << Regen;
    data << (uint32)0;
    data << (uint8)0;
    GetSession()->SendPacket( &data );
}

void Player::StopMirrorTimer(MirrorTimerType Type)
{
    if(Type==BREATH_TIMER)
        m_breathTimer = 0;

    WorldPacket data;
    data.Initialize(SMSG_STOP_MIRROR_TIMER);
    data << (uint32)Type;
    GetSession()->SendPacket( &data );
}

void Player::EnvironmentalDamage(uint64 Guid, uint8 Type, uint32 Amount)
{
    WorldPacket data;
    data.Initialize(SMSG_ENVIRONMENTALDAMAGELOG);
    data << Guid;
    data << (uint8)Type;
    data << Amount;
    data << (uint32)0;
    data << (uint32)0;
    //m_session->SendPacket(&data);
    //Let other players see that you get damage
    SendMessageToSet(&data, true);
    DealDamage((Unit*)this, Amount, SELF_DAMAGE, 0, true);
}

void Player::HandleDrowing(uint32 UnderWaterTime)
{
    if(!m_isunderwater)
        return;

    AuraList& mModWaterBreathing = GetAurasByType(SPELL_AURA_MOD_WATER_BREATHING);
    for(AuraList::iterator i = mModWaterBreathing.begin(); i != mModWaterBreathing.end(); ++i)
        UnderWaterTime = uint32(UnderWaterTime * (100.0f + (*i)->GetModifier()->m_amount) / 100.0f);

    //if have water breath , then remove bar
    if(waterbreath || !isAlive())
    {
        StopMirrorTimer(BREATH_TIMER);
        m_isunderwater = 0;
        return;
    }

    if ((m_isunderwater & 0x01) && !(m_isunderwater & 0x80) && isAlive())
    {
        //single trigger timer
        if (!(m_isunderwater & 0x02))
        {
            m_isunderwater|= 0x02;
            m_breathTimer = UnderWaterTime + 1000;
        }
        //single trigger "Breathbar"
        if ( m_breathTimer <= UnderWaterTime && !(m_isunderwater & 0x04))
        {
            m_isunderwater|= 0x04;
            StartMirrorTimer(BREATH_TIMER, UnderWaterTime);
        }
        //continius trigger drowning "Damage"
        if ((m_breathTimer == 0) && (m_isunderwater & 0x01))
        {
            //TODO: Check this formula
            uint64 guid = GetGUID();
            uint32 damage = (GetMaxHealth() / 5) + rand()%getLevel();

            EnvironmentalDamage(guid, DAMAGE_DROWNING,damage);
            m_breathTimer = 2000;
        }
    }
    //single trigger retract bar
    else if (!(m_isunderwater & 0x01) && !(m_isunderwater & 0x08) && (m_isunderwater & 0x02) && (m_breathTimer > 0) && isAlive())
    {
        m_isunderwater = 0x08;

        uint32 BreathRegen = 10;
        ModifyMirrorTimer(BREATH_TIMER, UnderWaterTime, m_breathTimer,BreathRegen);
        m_isunderwater = 0x10;
    }
    //remove bar
    else if ((m_breathTimer < 50) && !(m_isunderwater & 0x01) && (m_isunderwater == 0x10))
    {
        StopMirrorTimer(BREATH_TIMER);
        m_isunderwater = 0;
    }
}

void Player::HandleLava()
{
    if ((m_isunderwater & 0x80) && isAlive())
    {
        //Single trigger Set BreathTimer
        if (!(m_isunderwater & 0x04))
        {
            m_isunderwater|= 0x04;
            m_breathTimer = 1000;
        }
        //Reset BreathTimer and still in the lava
        if (!m_breathTimer)
        {
            uint64 guid;
            //uint32 damage = 10;
            uint32 damage = (GetMaxHealth() / 3) + rand()%getLevel();

            guid = GetGUID();
            EnvironmentalDamage(guid, DAMAGE_LAVA, damage);
            m_breathTimer = 1000;
        }

    }
    //Death timer disabled and WaterFlags reset
    else if (m_deathState == DEAD)
    {
        m_breathTimer = 0;
        m_isunderwater = 0;
    }
}

void Player::HandleSobering()
{
    m_drunkTimer = 0;
    if (m_drunk <= (0xFFFF / 30))
    {
        m_drunk = 0;
    }
    else
    {
        m_drunk -= (0xFFFF / 30);
    }
    SetUInt32Value(PLAYER_BYTES_3, (GetUInt32Value(PLAYER_BYTES_3) & 0xFFFF0001) | m_drunk);
}

void Player::SetDrunkValue(uint16 newDrunkValue)
{
    m_drunk = newDrunkValue;
    SetUInt32Value(PLAYER_BYTES_3,
        (GetUInt32Value(PLAYER_BYTES_3) & 0xFFFF0001) | m_drunk);
}

void Player::Update( uint32 p_time )
{
    if(!IsInWorld())
        return;

    WorldPacket data;

    Unit::Update( p_time );

    // update player only attacks
    if(uint32 ranged_att = getAttackTimer(RANGED_ATTACK))
    {
        setAttackTimer(RANGED_ATTACK, (p_time >= ranged_att ? 0 : ranged_att - p_time) );
    }

    if(uint32 off_att = getAttackTimer(OFF_ATTACK))
    {
        setAttackTimer(OFF_ATTACK, (p_time >= off_att ? 0 : off_att - p_time) );
    }

    time_t now = time (NULL);

    UpdatePVPFlag(time(NULL));

    UpdateDuelFlag(time(NULL));

    CheckDuelDistance();

    CheckExploreSystem();

    if (m_timedquests.size() > 0)
    {
        list<uint32>::iterator iter = m_timedquests.begin();
        while (iter != m_timedquests.end())
        {
            //if( mQuestStatus[*iter].m_timer > 0 )
            //{
            if( mQuestStatus[*iter].m_timer <= p_time )
            {
                FailTimedQuest( *iter );
                iter = m_timedquests.begin();
            }
            else
            {
                mQuestStatus[*iter].m_timer -= p_time;
                iter++;
            }
            //}
        }
    }

    if (isAttacking())
    {
        Unit *pVictim = getVictim();
        if( m_currentSpell == 0 && pVictim)
        {

            // default combat reach 10
            // TODO add weapon,skill check

            float pldistance = 5.0f;

            /*if(getClass() == WARRIOR)
                pldistance += 1;

            if(GetItemByPos( INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND ) != 0)
                pldistance += 2;

            if(GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_HANDS) != 0)
                pldistance += 3;*/

            if (isAttackReady(BASE_ATTACK))
            {
                if(!IsWithinDist(pVictim, pldistance))
                {
                    setAttackTimer(BASE_ATTACK,1000);
                    SendAttackSwingNotInRange();
                }
                //120 degreas of radiant range
                else if( !HasInArc( 2*M_PI/3, pVictim ))
                {
                    setAttackTimer(BASE_ATTACK,1000);
                    SendAttackSwingBadFacingAttack();
                }
                else
                {
                    // prevent base and off attack in same time, delay attack at 0.2 sec
                    if(haveOffhandWeapon())
                    {
                        uint32 off_att = getAttackTimer(OFF_ATTACK);
                        if(off_att < ATTACK_DISPLAY_DELAY)
                            setAttackTimer(OFF_ATTACK,ATTACK_DISPLAY_DELAY);
                    }
                    AttackerStateUpdate(pVictim, BASE_ATTACK);
                    resetAttackTimer(BASE_ATTACK);
                }
            }

            if ( haveOffhandWeapon() && isAttackReady(OFF_ATTACK))
            {
                if(! IsWithinDist(pVictim, pldistance))
                {
                    setAttackTimer(OFF_ATTACK,1000);
                }
                else if( !HasInArc( 2*M_PI/3, pVictim ))
                {
                    setAttackTimer(OFF_ATTACK,1000);
                }
                else
                {
                    // prevent base and off attack in same time, delay attack at 0.2 sec
                    uint32 base_att = getAttackTimer(BASE_ATTACK);
                    if(base_att < ATTACK_DISPLAY_DELAY)
                        setAttackTimer(BASE_ATTACK,ATTACK_DISPLAY_DELAY);
                    // do attack
                    AttackerStateUpdate(pVictim, OFF_ATTACK);
                    resetAttackTimer(OFF_ATTACK);
                }
            }
        }
    }

    else if (isAttacked())
    {
        // Leave here so we don't forget this case
        // Attacked, but not necessarily attacking
    }
    else
    {
        // Leave here so we don't forget this case
        // Not attacking or attacked
    }

    if(HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))
    {
        if(irand(0,100)<=3 && GetTimeInnEter() > 0)         //freeze update
        {
            int time_inn = time(NULL)-GetTimeInnEter();
            if (time_inn >= 10)                             //freeze update
            {
                float bubble = sWorld.getRate(RATE_REST_INGAME);
                                                            //speed collect rest bonus (section/in hour)
                SetRestBonus( GetRestBonus()+ time_inn*((float)GetUInt32Value(PLAYER_NEXT_LEVEL_XP)/144000)*bubble );
                UpdateInnerTime(time(NULL));
            }
        }
        if(GetRestType()==1)                                //rest in tavern
        {
            if(sqrt((GetPositionX()-GetInnPosX())*(GetPositionX()-GetInnPosX())+(GetPositionY()-GetInnPosY())*(GetPositionY()-GetInnPosY())+(GetPositionZ()-GetInnPosZ())*(GetPositionZ()-GetInnPosZ()))>40)
            {
                RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);
            }
        }
    }

    if(m_regenTimer > 0)
    {
        if(p_time >= m_regenTimer)
            m_regenTimer = 0;
        else
            m_regenTimer -= p_time;
    }

    if (isAlive())
    {
        RegenerateAll();
    }

    if (m_deathState == JUST_DIED)
    {        
        KillPlayer();
        if( GetSoulStoneSpell() && GetSoulStone())
        {
            SpellEntry *spellInfo = sSpellStore.LookupEntry(GetSoulStoneSpell());
            if(spellInfo)
            {
                Spell spell(this, spellInfo, true, 0);

                SpellCastTargets targets;
                targets.setUnitTarget( this );
                spell.m_CastItem = GetSoulStone();
                spell.prepare(&targets);
            }
            SetSoulStone(NULL);
            SetSoulStoneSpell(0);
        }
    }

    if(m_nextSave > 0)
    {
        if(p_time >= m_nextSave)
        {
            // m_nextSave reseted in SaveToDB call
            SaveToDB();
            sLog.outBasic("Player '%u' '%s' Saved", GetGUIDLow(), GetName());
        }
        else
        {
            m_nextSave -= p_time;
        }
    }

    //Breathtimer
    if(m_breathTimer > 0)
    {
        if(p_time >= m_breathTimer)
            m_breathTimer = 0;
        else
            m_breathTimer -= p_time;

    }

    //Handle Water/drowning
    HandleDrowing(60000);

    //Handle lava
    HandleLava();

    // Played time
    if (now > m_Last_tick)
    {
        uint32 elapsed = uint32(now - m_Last_tick);
        m_Played_time[0] += elapsed;                        // Total played time
        m_Played_time[1] += elapsed;                        // Level played time
        m_Last_tick = now;
    }

    if (m_drunk)
    {
        m_drunkTimer += p_time;

        if (m_drunkTimer > 30000)
            HandleSobering();
    }

    if(m_deathTimer > 0)
    {
        if(p_time >= m_deathTimer)
        {
            m_deathTimer = 0;
            BuildPlayerRepop();
            RepopAtGraveyard();
        }
        else
            m_deathTimer -= p_time;
    }
    UpdateEnchantTime(p_time);
}

void Player::BuildEnumData( WorldPacket * p_data )
{
    *p_data << GetGUID();
    *p_data << m_name;

    *p_data << getRace();
    *p_data << getClass();
    *p_data << getGender();

    uint32 bytes = GetUInt32Value(PLAYER_BYTES);
    *p_data << uint8(bytes);
    *p_data << uint8(bytes >> 8);
    *p_data << uint8(bytes >> 16);
    *p_data << uint8(bytes >> 24);

    bytes = GetUInt32Value(PLAYER_BYTES_2);
    *p_data << uint8(bytes);

    *p_data << uint8(getLevel());                           //1
    uint32 zoneId = MapManager::Instance ().GetMap(m_mapId)->GetZoneId(m_positionX,m_positionY);

    *p_data << zoneId;
    *p_data << GetMapId();

    *p_data << m_positionX;
    *p_data << m_positionY;
    *p_data << m_positionZ;

    *p_data << GetUInt32Value(PLAYER_GUILDID);              //probebly wrong

    //*p_data << GetUInt32Value(PLAYER_GUILDRANK);    //this was
    *p_data << uint8(0x0);
    *p_data << uint8(GetUInt32Value(PLAYER_FLAGS) << 1);
    *p_data << uint8(0x0);                                  //Bit 4 is something dono
    *p_data << uint8(0x0);                                  //is this player_GUILDRANK????

    *p_data << (uint8)0;
    *p_data << (uint32)m_petInfoId;
    *p_data << (uint32)m_petLevel;
    *p_data << (uint32)m_petFamilyId;

    ItemPrototype const *items[EQUIPMENT_SLOT_END];
    for (int i = 0; i < EQUIPMENT_SLOT_END; i++)
        items[i] = NULL;

    QueryResult *result = sDatabase.PQuery("SELECT `slot`,`item_template` FROM `character_inventory` WHERE `guid` = '%u' AND `bag` = 0",GetGUIDLow());
    if (result)
    {
        do
        {
            Field *fields  = result->Fetch();
            uint8  slot    = fields[0].GetUInt8() & 255;
            uint32 item_id = fields[1].GetUInt32();
            if( slot >= EQUIPMENT_SLOT_END )
                continue;

            items[slot] = objmgr.GetItemPrototype(item_id);
            if(!items[slot])
            {
                sLog.outError( "Player::BuildEnumData: Player %s have unknown item (id: #%u) in inventory, skipped.", GetName(),item_id );
                continue;
            }
        } while (result->NextRow());
        delete result;
    }

    for (int i = 0; i < EQUIPMENT_SLOT_END; i++)
    {
        if (items[i] != NULL)
        {
            *p_data << (uint32)items[i]->DisplayInfoID;
            *p_data << (uint8)items[i]->InventoryType;
        }
        else
        {
            *p_data << (uint32)0;
            *p_data << (uint8)0;
        }
    }
    // EQUIPMENT_SLOT_END always 0,0
    *p_data << (uint32)0;
    *p_data << (uint8)0;
}

bool Player::ToggleAFK()
{
    if(HasFlag(PLAYER_FLAGS,PLAYER_FLAGS_AFK))
        RemoveFlag(PLAYER_FLAGS,PLAYER_FLAGS_AFK);
    else
    {
        // to prevent show <AFK> in invisiable mode
        if(isGMVisible())
            SetFlag(PLAYER_FLAGS,PLAYER_FLAGS_AFK);
        else
            GetSession()->SendNotification("You invisible currently!");
    }

    return HasFlag(PLAYER_FLAGS,PLAYER_FLAGS_AFK);
}

bool Player::ToggleDND()
{
    if(HasFlag(PLAYER_FLAGS,PLAYER_FLAGS_DND))
        RemoveFlag(PLAYER_FLAGS,PLAYER_FLAGS_DND);
    else
    {
        // to prevent show <DND> in invisiable mode
        if(isGMVisible())
            SetFlag(PLAYER_FLAGS,PLAYER_FLAGS_DND);
        else
            GetSession()->SendNotification("You invisible currently!");
    }

    return HasFlag(PLAYER_FLAGS,PLAYER_FLAGS_DND);
}

uint8 Player::chatTag()
{
    if(isGameMaster())
        return 3;
    else if(isDND())
        return 2;
    if(isAFK())
        return 1;
    else
        return 0;
}

void Player::SendFriendlist()
{
    WorldPacket data;
    uint8 i=0;
    Field *fields;
    Player* pObj;
    FriendStr friendstr[255];

    QueryResult *result = sDatabase.PQuery("SELECT `friend` FROM `character_social` WHERE `flags` = 'FRIEND' AND `guid` = '%u'",GetGUIDLow());
    if(result)
    {
        fields = result->Fetch();

        do
        {
            friendstr[i].PlayerGUID = fields[0].GetUInt64();
            pObj = ObjectAccessor::Instance().FindPlayer(friendstr[i].PlayerGUID);
            if( pObj && pObj->isGMVisibleFor(this))
            {
                if(pObj->isAFK())
                    friendstr[i].Status = 2;
                else if(pObj->isDND())
                    friendstr[i].Status = 4;
                else
                    friendstr[i].Status = 1;
                friendstr[i].Area = pObj->GetZoneId();
                friendstr[i].Level = pObj->getLevel();
                friendstr[i].Class = pObj->getClass();
            }
            else
            {
                friendstr[i].Status = 0;
                friendstr[i].Area = 0;
                friendstr[i].Level = 0;
                friendstr[i].Class = 0;
            }
            i++;

            // prevent overflow
            if(i==255)
                break;
        } while( result->NextRow() );

        delete result;
    }

    data.Initialize( SMSG_FRIEND_LIST );
    data << i;

    for (int j=0; j < i; j++)
    {

        sLog.outDetail( "WORLD: Adding Friend Guid: %u, Status:%u, Area:%u, Level:%u Class:%u",GUID_LOPART(friendstr[j].PlayerGUID), friendstr[j].Status, friendstr[j].Area,friendstr[j].Level,friendstr[j].Class  );

        data << friendstr[j].PlayerGUID << friendstr[j].Status ;
        if (friendstr[j].Status != 0)
            data << friendstr[j].Area << friendstr[j].Level << friendstr[j].Class;
    }

    this->GetSession()->SendPacket( &data );
    sLog.outDebug( "WORLD: Sent (SMSG_FRIEND_LIST)" );
}

void Player::SendIgnorelist()
{
    WorldPacket dataI;

    unsigned char nrignore=0;
    Field *fields;

    QueryResult *result = sDatabase.PQuery("SELECT COUNT(`friend`) FROM `character_social` WHERE `flags` = 'IGNORE' AND `guid` = '%u'", GetGUIDLow());

    if(!result) return;

    fields = result->Fetch();
    nrignore=fields[0].GetUInt32();
    delete result;

    dataI.Initialize( SMSG_IGNORE_LIST );
    dataI << nrignore;

    result = sDatabase.PQuery("SELECT `friend` FROM `character_social` WHERE `flags` = 'IGNORE' AND `guid` = '%u'", GetGUIDLow());

    if(!result) return;

    do
    {

        fields = result->Fetch();
        dataI << fields[0].GetUInt64();

    }while( result->NextRow() );
    delete result;

    this->GetSession()->SendPacket( &dataI );
    sLog.outDebug( "WORLD: Sent (SMSG_IGNORE_LIST)" );
}

void Player::TeleportTo(uint32 mapid, float x, float y, float z, float orientation, bool outofrange, bool ignore_transport)
{
    // prepering unsommon pet if lost (we must get pet before teleportation or will not find it later)
    Creature* pet = GetPet();

    // if we were on a transport, leave
    if (ignore_transport && m_transport)
    {
        m_transport->RemovePassenger(this);
        m_transport = NULL;
        m_transX = 0.0f;
        m_transY = 0.0f;
        m_transZ = 0.0f;
        m_transO = 0.0f;
    }

    if ((this->GetMapId() == mapid) && (!m_transport))
    {
        // near teleport
        WorldPacket data;
        BuildTeleportAckMsg(&data, x, y, z, orientation);
        GetSession()->SendPacket(&data);
        SetPosition( x, y, z, orientation );
        BuildHeartBeatMsg(&data);
        SendMessageToSet(&data, true);
    }
    else
    {
        MapManager::Instance().GetMap(GetMapId())->Remove(this, false);
        WorldPacket data;
        data.Initialize(SMSG_TRANSFER_PENDING);
        data << uint32(mapid);
        if (m_transport)
        {
            data << m_transport->GetEntry() << GetMapId();
        }
        GetSession()->SendPacket(&data);

        data.Initialize(SMSG_NEW_WORLD);
        if (m_transport)
        {
            data << (uint32)mapid << m_transX << m_transY << m_transZ << m_transO;
        }
        else
        {
            data << (uint32)mapid << (float)x << (float)y << (float)z << (float)orientation;
        }
        GetSession()->SendPacket( &data );

        SetMapId(mapid);
        if(m_transport)
        {
            Relocate(x + m_transX, y + m_transY, z + m_transZ, orientation + m_transO);
            SetPosition(x + m_transX, y + m_transY, z + m_transZ, orientation + m_transO);
        }
        else
        {
            Relocate(x, y, z, orientation);
            SetPosition(x, y, z, orientation);
        }
        SetDontMove(true);
        //SaveToDB();

        //MapManager::Instance().GetMap(GetMapId())->Add(this);

        // Resend spell list to client after far teleport.
        SendInitialSpells();
    }

    if (outofrange)
    {
        CombatStop();

        // remove selection
        if(GetSelection())
        {
            Unit* unit = ObjectAccessor::Instance().GetUnit(*this, GetSelection());
            if(unit)
                SendOutOfRange(unit);
        }

        // unsommon pet if lost
        if(pet && !IsWithinDistInMap(pet, OWNER_MAX_DISTANCE))
            UnsummonPet(pet);
    }
}

void Player::AddToWorld()
{
    Object::AddToWorld();

    for(int i = 0; i < BANK_SLOT_BAG_END; i++)
    {
        if(m_items[i])
            m_items[i]->AddToWorld();
    }
    AddWeather();

    if(Corpse* corpse = GetCorpse())
        corpse->UpdateForPlayer(this,true);
}

void Player::RemoveFromWorld()
{

    for(int i = 0; i < BANK_SLOT_BAG_END; i++)
    {
        if(m_items[i])
            m_items[i]->RemoveFromWorld();
    }

    Object::RemoveFromWorld();
}

void Player::CalcRage( uint32 damage,bool attacker )
{
    uint32 addRage = 0;

    if(attacker)
        addRage = (uint32)(10*damage/(getLevel()*0.5f));
    else
        addRage = (uint32)(10*damage/(getLevel()*1.5f));

    ModifyPower(POWER_RAGE, addRage);
}

void Player::RegenerateAll()
{

    if (m_regenTimer != 0)
        return;
    uint32 regenDelay = 2000;

    // Not in combat or they have regeneration
    if (!isInCombat() || HasAuraType(SPELL_AURA_MOD_REGEN_DURING_COMBAT))
    {
        RegenerateHealth();
        if (!isInCombat())
            Regenerate(POWER_RAGE);
    }

    Regenerate( POWER_ENERGY );
    Regenerate( POWER_MANA );

    m_regenTimer = regenDelay;

}

void Player::Regenerate(Powers power)
{
    uint32 curValue = GetPower(power);
    uint32 maxValue = GetMaxPower(power);

    if(power != POWER_RAGE)
    {
        if (curValue >= maxValue)   return;
    }
    else if (curValue == 0)
        return;

    float ManaIncreaseRate = sWorld.getRate(RATE_POWER_MANA);
    float RageIncreaseRate = sWorld.getRate(RATE_POWER_RAGE);

    float Spirit = GetStat(STAT_SPIRIT);
    uint8 Class = getClass();

    if( ManaIncreaseRate <= 0 ) ManaIncreaseRate = 1;
    if( RageIncreaseRate <= 0 ) RageIncreaseRate = 1;

    float addvalue = 0.0;

    switch (power)
    {
        case POWER_MANA:
            // If < 5s after previous cast which used mana, no regeneration unless
            // we happen to have a modifer that adds it back
            // If > 5s, get portion between the 5s and now, up to a maximum of 2s worth
            uint32 msecSinceLastCast;
            msecSinceLastCast = ((uint32)getMSTime() - m_lastManaUse);
            if (msecSinceLastCast >= 7000)
            {
                ManaIncreaseRate *= 1;
            }
            else
            {
                long regenInterrupt = GetTotalAuraModifier(SPELL_AURA_MOD_MANA_REGEN_INTERRUPT);
                if (msecSinceLastCast < 5000)
                {
                    ManaIncreaseRate *= (float)regenInterrupt / 100;
                }
                else
                {
                    ManaIncreaseRate =  (((1 - (float)(msecSinceLastCast - 5000)/2000)) * regenInterrupt)
                        + (((float)(msecSinceLastCast - 5000)/2000) * ManaIncreaseRate * 100);
                    ManaIncreaseRate /= 100;
                }
            }
            ManaIncreaseRate = (ManaIncreaseRate * 100 + GetTotalAuraModifier(SPELL_AURA_MOD_POWER_REGEN_PERCENT)) / 100;

            switch (Class)
            {
                case CLASS_DRUID:   addvalue = (Spirit/5 + 15)   * ManaIncreaseRate; break;
                case CLASS_HUNTER:  addvalue = (Spirit/5 + 15)   * ManaIncreaseRate; break;
                case CLASS_MAGE:    addvalue = (Spirit/4 + 12.5) * ManaIncreaseRate; break;
                case CLASS_PALADIN: addvalue = (Spirit/5 + 15)   * ManaIncreaseRate; break;
                case CLASS_PRIEST:  addvalue = (Spirit/4 + 12.5) * ManaIncreaseRate; break;
                case CLASS_SHAMAN:  addvalue = (Spirit/5 + 17)   * ManaIncreaseRate; break;
                case CLASS_WARLOCK: addvalue = (Spirit/5 + 15)   * ManaIncreaseRate; break;
            }
            break;
        case POWER_RAGE:                                    // Regenerate rage
            addvalue = 30 * RageIncreaseRate;               // 3 rage by tick
            break;
        case POWER_ENERGY:                                  // Regenerate energy (rogue)
            addvalue = 20;
            break;
        case POWER_FOCUS:
        case POWER_HAPPINESS:
            break;
    }

    if (power != POWER_RAGE)
    {
        curValue += uint32(addvalue);
        if (curValue > maxValue) curValue = maxValue;
    }
    else
    {
        if(curValue <= uint32(addvalue))
            curValue = 0;
        else
            curValue -= uint32(addvalue);
    }
    SetPower(power, curValue);
}

void Player::RegenerateHealth()
{
    uint32 curValue = GetHealth();
    uint32 maxValue = GetMaxHealth();

    if (curValue >= maxValue) return;

    float HealthIncreaseRate = sWorld.getRate(RATE_HEALTH);

    float Spirit = GetStat(STAT_SPIRIT);
    uint8 Class = getClass();

    if( HealthIncreaseRate <= 0 ) HealthIncreaseRate = 1;

    float addvalue = 0.0;

    switch (Class)
    {
        case CLASS_DRUID:   addvalue = (Spirit*0.11 + 1)   * HealthIncreaseRate; break;
        case CLASS_HUNTER:  addvalue = (Spirit*0.43 - 5.5) * HealthIncreaseRate; break;
        case CLASS_MAGE:    addvalue = (Spirit*0.11 + 1)   * HealthIncreaseRate; break;
        case CLASS_PALADIN: addvalue = (Spirit*0.25)       * HealthIncreaseRate; break;
        case CLASS_PRIEST:  addvalue = (Spirit*0.15 + 1.4) * HealthIncreaseRate; break;
        case CLASS_ROGUE:   addvalue = (Spirit*0.84 - 13)  * HealthIncreaseRate; break;
        case CLASS_SHAMAN:  addvalue = (Spirit*0.28 - 3.6) * HealthIncreaseRate; break;
        case CLASS_WARLOCK: addvalue = (Spirit*0.12 + 1.5) * HealthIncreaseRate; break;
        case CLASS_WARRIOR: addvalue = (Spirit*1.26 - 22.6)* HealthIncreaseRate; break;
    }

    if (!isInCombat())
    {
        AuraList& mModHealthRegenPct = GetAurasByType(SPELL_AURA_MOD_HEALTH_REGEN_PERCENT);
        for(AuraList::iterator i = mModHealthRegenPct.begin(); i != mModHealthRegenPct.end(); ++i)
            addvalue *= (100.0f + (*i)->GetModifier()->m_amount) / 100.0f;
    }
    else
        addvalue *= m_AuraModifiers[SPELL_AURA_MOD_REGEN_DURING_COMBAT] / 100.0f;

    switch (getStandState())
    {
        case PLAYER_STATE_SIT_CHAIR:
        case PLAYER_STATE_SIT_LOW_CHAIR:
        case PLAYER_STATE_SIT_MEDIUM_CHAIR:
        case PLAYER_STATE_SIT_HIGH_CHAIR:
        case PLAYER_STATE_SIT:
        case PLAYER_STATE_SLEEP:
        case PLAYER_STATE_KNEEL:
            addvalue *= 1.5; break;
    }

    if(addvalue < 0)
        addvalue = 0;

    ModifyHealth(int32(addvalue));
}

bool Player::isAcceptTickets() const
{
    return GetSession()->GetSecurity() >=2 && (m_GMFlags & GM_ACCEPT_TICKETS);
}

void Player::SetGameMaster(bool on)
{
    if(on)
    {
        m_GMFlags |= GM_ON;
        setFaction(35);
        SetFlag(PLAYER_BYTES_2, 0x8);

        // to prevent show <GM> in invisible mode
        if(isGMVisible())
            SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GM);
    }
    else
    {
        m_GMFlags &= ~GM_ON;
        setFactionForRace(getRace());
        RemoveFlag(PLAYER_BYTES_2, 0x8);
        RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_GM);
    }
}

void Player::SetGMVisible(bool on)
{
    if(on)
    {
        m_GMFlags &= ~GM_INVISIBLE;                         //remove flag

        // if in GM mode show <GM> befire removing invisibility
        if(isGameMaster())
            SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GM);

        DeMorph();
        RemoveAura(10032,1);                                //crash?

    }
    else
    {
        m_GMFlags |= GM_INVISIBLE;                          //add flag

        // remove <AFK> before go to invisible mode
        if(isAFK())
            ToggleAFK();

        // remove <DND> before go to invisible mode
        if(isDND())
            ToggleDND();

        SetAcceptWhispers(false);
        SetGameMaster(true);                                // <GM> wiil be not added

        SetUInt32Value(UNIT_FIELD_DISPLAYID, 6908);         //Set invisible model

                                                            //Stealth spell
        SpellEntry *spellInfo = sSpellStore.LookupEntry( 10032 );
        Aura *Aur = new Aura(spellInfo, 1, this);
        AddAura(Aur);
    }

    // hide or show name
    GetSession()->SendNameQueryOpcode(this,true);
}

void Player::SendLogXPGain(uint32 GivenXP, Unit* victim, uint32 RestXP)
{
    WorldPacket data;
    data.Initialize( SMSG_LOG_XPGAIN );
    data << ( victim ? victim->GetGUID() : uint64(0) );
    data << uint32(GivenXP+RestXP);                         // given experience
    data << ( victim ? (uint8)0 : (uint8)1 );               // 00-kill_xp type, 01-non_kill_xp type
    data << uint32(RestXP);                                 // rested given experience
    data << float(1);                                       // 1 - none 0 - 100% group bonus output
    GetSession()->SendPacket(&data);
}

void Player::GiveXP(uint32 xp, Unit* victim)
{
    if ( xp < 1 )
        return;

    uint32 level = getLevel();

    // XP to money conversion processed in Player::RewardQuest
    if(level >= sWorld.getConfig(CONFIG_MAX_PLAYER_LEVEL))
        return;

    // XP resting bonus for kill
    uint32 rested_bonus_xp = victim ? GetXPRestBonus(xp) : 0;

    SendLogXPGain(xp,victim,rested_bonus_xp);

    uint32 curXP = GetUInt32Value(PLAYER_XP);
    uint32 nextLvlXP = GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    uint32 newXP = curXP + xp + rested_bonus_xp;

    while( newXP >= nextLvlXP && level < sWorld.getConfig(CONFIG_MAX_PLAYER_LEVEL) )
    {
        newXP -= nextLvlXP;

        GiveLevel();

        level = getLevel();
        nextLvlXP = GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    }

    SetUInt32Value(PLAYER_XP, newXP);
}

// Update player to next level
// Current player expirience not update (must be update by caller)
void Player::GiveLevel()
{
    uint32 level = getLevel();

    if ( level >= sWorld.getConfig(CONFIG_MAX_PLAYER_LEVEL) )
        return;

    level += 1;

    InitStatsForLevel(level);
}

void Player::InitStatsForLevel(uint32 level, bool sendgain, bool remove_mods)
{
    // Remove item, aura, stats bonuses
    if(remove_mods)
    {
        _RemoveAllItemMods();
        _RemoveAllAuraMods();
        _RemoveStatsMods();
    }

    PlayerLevelInfo info;

    objmgr.GetPlayerLevelInfo(getRace(),getClass(),level,&info);

    if(sendgain)
    {
        // send levelup info to client
        WorldPacket data;
        data.Initialize(SMSG_LEVELUP_INFO);
        data << uint32(level);
        data << uint32(int32(info.health) - GetMaxHealth());
        data << uint32(int32(info.mana)   - GetMaxPower(POWER_MANA));
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);
        data << uint32(0);

        for(int i = STAT_STRENGTH; i < MAX_STATS; ++i)
            data << uint32(int32(info.stats[i]) - GetStat(Stats(i)));

        GetSession()->SendPacket(&data);
    }

    SetUInt32Value(PLAYER_NEXT_LEVEL_XP, MaNGOS::XP::xp_to_level(level));

    // talentes base at level diff ( talentes = level - 9 but some can be used already)
    if(level < 10)
    {
        // Remove all talent points
        if(getLevel() >= 10)                                // Free any used talentes
        {
            resetTalents(true);
            SetUInt32Value(PLAYER_CHARACTER_POINTS1,0);
        }
    }
    else
    {
        // Update talent points amount
        if(level > getLevel())                                  // Add new talent points
            SetUInt32Value(PLAYER_CHARACTER_POINTS1,GetUInt32Value(PLAYER_CHARACTER_POINTS1)+min(level-getLevel(),level-9));
        else
        if(level < getLevel())                                  // Free if need talentes, remove some amount talent points
        {
            if(GetUInt32Value(PLAYER_CHARACTER_POINTS1) < (getLevel() - level))
               resetTalents(true);
            SetUInt32Value(PLAYER_CHARACTER_POINTS1,GetUInt32Value(PLAYER_CHARACTER_POINTS1)-(getLevel() - level));
        }
    }

    // update level, max level of skills
    SetLevel( level);
    UpdateMaxSkills ();

    // save new stats
    SetMaxPower(POWER_MANA, info.mana);
    if(getPowerType() == POWER_RAGE)
       SetMaxPower(POWER_RAGE, 1000 );
    else if(getPowerType()== POWER_ENERGY)
        SetMaxPower(POWER_ENERGY, 100 );

    SetMaxPower(POWER_FOCUS, 0 );
    SetMaxPower(POWER_HAPPINESS, 0 );

    SetMaxHealth(info.health);

    // save base values (bonuses already included in stored stats
    for(int i = STAT_STRENGTH; i < MAX_STATS; ++i)
        SetCreateStat(Stats(i), info.stats[i]);

    for(int i = STAT_STRENGTH; i < MAX_STATS; ++i)
        SetStat(Stats(i), info.stats[i]);

    // reset misc. values
    SetAttackTime(BASE_ATTACK,   2000 );
    SetAttackTime(OFF_ATTACK,    2000 );
    SetAttackTime(RANGED_ATTACK, 2000 );

    SetFloatValue(UNIT_FIELD_MINDAMAGE, 0 );
    SetFloatValue(UNIT_FIELD_MAXDAMAGE, 0 );
    SetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE, 0 );
    SetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE, 0 );
    SetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE, 0 );
    SetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE, 0 );

    SetArmor(m_createStats[STAT_AGILITY]*2);
    SetUInt32Value(UNIT_FIELD_ATTACK_POWER,        0 );
    SetUInt32Value(UNIT_FIELD_RANGED_ATTACK_POWER, 0 );

    SetFloatValue(PLAYER_FIELD_MOD_DAMAGE_DONE_PCT, 1.00);
    SetFloatValue(PLAYER_FIELD_MOD_DAMAGE_DONE_NEG, 0);
    SetFloatValue(PLAYER_FIELD_MOD_DAMAGE_DONE_POS, 0);

    for(int i = STAT_STRENGTH; i < MAX_STATS; ++i)
        SetPosStat(Stats(i), 0);

    for(int i = STAT_STRENGTH; i < MAX_STATS; ++i)
        SetNegStat(Stats(i), 0);

    // Base crit/parry values
    SetFloatValue(PLAYER_CRIT_PERCENTAGE, 5);
    SetFloatValue(PLAYER_PARRY_PERCENTAGE, 5);

    // Base dodge values
    switch(getClass())
    {
        case CLASS_PALADIN: SetFloatValue(PLAYER_DODGE_PERCENTAGE, 0.75); break;
        case CLASS_HUNTER:  SetFloatValue(PLAYER_DODGE_PERCENTAGE, 0.64); break;
        case CLASS_PRIEST:  SetFloatValue(PLAYER_DODGE_PERCENTAGE, 3.0 ); break;
        case CLASS_SHAMAN:  SetFloatValue(PLAYER_DODGE_PERCENTAGE, 1.75); break;
        case CLASS_MAGE:    SetFloatValue(PLAYER_DODGE_PERCENTAGE, 3.25); break;
        case CLASS_WARLOCK: SetFloatValue(PLAYER_DODGE_PERCENTAGE, 2.0 ); break;
        case CLASS_DRUID:   SetFloatValue(PLAYER_DODGE_PERCENTAGE, 0.75); break;
        case CLASS_ROGUE:
        case CLASS_WARRIOR:
        default:            SetFloatValue(PLAYER_DODGE_PERCENTAGE, 0.0 ); break;
    }

    // set armor (resistence 0) to original value (create_agility*2)
    SetArmor(m_createStats[STAT_AGILITY]*2);
    SetResistanceBuffMods(SpellSchools(0), true, 0);
    SetResistanceBuffMods(SpellSchools(0), false, 0);
    // set other resistence to original value (0)
    for (int i = 1; i < MAX_SPELL_SCHOOOL; i++)
    {
        SetResistance(SpellSchools(i), 0);
        SetResistanceBuffMods(SpellSchools(i), true, 0);
        SetResistanceBuffMods(SpellSchools(i), false, 0);
    }

    // apply stats, aura, items mods
    if(remove_mods)
    {
        _ApplyStatsMods();
        _ApplyAllAuraMods();
        _ApplyAllItemMods();
    }

    // update dependent from level part BlockChanceWithoutMods = 5 + (GetDefenceSkillValue() - getLevel()*5)*0.04);
    // must called with applied AuraMods (removed in call code)
    UpdateBlockPercentage();

    // set current level health and mana/energy to maximum after appling all mods.
    SetHealth(GetMaxHealth());
    SetPower(POWER_MANA, GetMaxPower(POWER_MANA));
    SetPower(POWER_ENERGY, GetMaxPower(POWER_ENERGY));
    if(GetPower(POWER_RAGE) > GetMaxPower(POWER_RAGE))
        SetPower(POWER_RAGE, GetMaxPower(POWER_RAGE));
    SetPower(POWER_FOCUS, 0);
    SetPower(POWER_HAPPINESS, 0);

    // Level Played Time reset
    m_Played_time[1] = 0;
}


void Player::SendInitialSpells()
{
    WorldPacket data;
    uint16 spellCount = 0;

    PlayerSpellMap::const_iterator itr;

    for (itr = m_spells.begin(); itr != m_spells.end(); ++itr)
    {
        if(itr->second->state == PLAYERSPELL_REMOVED)
            continue;

        if(itr->second->active)
            spellCount +=1;
    }

    data.Initialize( SMSG_INITIAL_SPELLS );
    data << uint8(0);
    data << uint16(spellCount);

    for (itr = m_spells.begin(); itr != m_spells.end(); ++itr)
    {
        if(itr->second->state == PLAYERSPELL_REMOVED)
            continue;

        if(!itr->second->active)
            continue;

        data << uint16(itr->first);
        data << uint16(itr->second->slotId);
    }
    data << uint16(0);

    WPAssert(data.size() == 5+(4*size_t(spellCount)));

    GetSession()->SendPacket(&data);

    sLog.outDetail( "CHARACTER: Sent Initial Spells" );
}

void Player::RemoveMail(uint32 id)
{
    std::list<Mail*>::iterator itr;
    for (itr = m_mail.begin(); itr != m_mail.end();)
    {
        if ((*itr)->messageID == id)
        {
            //do not delete item. beacuse Player::removeMail() is called when returning mail to sender.
            m_mail.erase(itr++);
        }
        else
        {
            ++itr;
        }
    }
}

//called when mail's state changed, removes mail and adds it to the end - mails are sorted
void Player::SetMail(Mail *m)
{
    if (!m_mailsLoaded)
        return;

    std::list<Mail*>::iterator itr;
    for (itr = m_mail.begin(); itr != m_mail.end();)
    {
        if ((*itr)->messageID == m->messageID)
            m_mail.erase(itr++);
        else
            ++itr;
    }
    m_mail.push_back(m);                                    //insert to the end
}

//call this function only when sending new mail
void Player::AddMail(Mail *m)
{
    WorldPacket data;

    data.Initialize(SMSG_RECEIVED_MAIL);
    data << uint32(0);
    GetSession()->SendPacket(&data);
    unReadMails++;

    if(!m_mailsLoaded)
        return;

    std::list<Mail*>::iterator itr;
    for (itr = m_mail.begin(); itr != m_mail.end();)
    {
        if ((*itr)->messageID == m->messageID)
        {
            m_mail.erase(itr++);
        }
        else
        {
            ++itr;
        }
    }
    m_mail.push_front(m);                                   //to insert new mail to beginning of maillist
}

bool Player::addSpell(uint16 spell_id, uint8 active, PlayerSpellState state, uint16 slot_id)
{
    SpellEntry *spellInfo = sSpellStore.LookupEntry(spell_id);
    if (!spellInfo)
    {
        sLog.outError("Player::addSpell: Non-existed in SpellStore spell #%u request.",spell_id);
        return false;
    }

    PlayerSpellMap::iterator itr = m_spells.find(spell_id);
    if (itr != m_spells.end())
    {
        if (itr->second->state == PLAYERSPELL_REMOVED)
        {
            delete itr->second;
            m_spells.erase(itr);
            state = PLAYERSPELL_CHANGED;
        }
        else
            return false;
    }

    PlayerSpell *newspell;

    newspell = new PlayerSpell;
    newspell->active = active;
    newspell->state = state;

    WorldPacket data;
    if(newspell->active && !canStackSpellRank(spellInfo))
    {
        PlayerSpellMap::iterator itr;
        for (itr = m_spells.begin(); itr != m_spells.end(); itr++)
        {
            if(itr->second->state == PLAYERSPELL_REMOVED) continue;
            SpellEntry *i_spellInfo = sSpellStore.LookupEntry(itr->first);
            if(!i_spellInfo) continue;

            if(IsRankSpellDueToSpell(spellInfo,itr->first))
            {
                if(itr->second->active)
                {
                    data.Initialize(SMSG_SUPERCEDED_SPELL);

                    if(FindSpellRank(spell_id) >= FindSpellRank(itr->first))
                    {
                        data << uint32(itr->first);
                        data << uint32(spell_id);
                        itr->second->active = 0;
                    }
                    else
                    {
                        data << uint32(spell_id);
                        data << uint32(itr->first);
                        newspell->active = 0;
                    }

                    GetSession()->SendPacket( &data );
                }
            }
        }
    }

    uint16 tmpslot=slot_id;

    if (tmpslot == 0xffff)
    {
        uint16 maxid = 0;
        PlayerSpellMap::iterator itr;
        for (itr = m_spells.begin(); itr != m_spells.end(); ++itr)
        {
            if(itr->second->state == PLAYERSPELL_REMOVED) continue;
            if (itr->second->slotId > maxid) maxid = itr->second->slotId;
        }
        tmpslot = maxid + 1;
    }

    newspell->slotId = tmpslot;
    m_spells[spell_id] = newspell;

    if (IsPassiveSpell(spell_id))
    {
        // if spell doesnt require a stance or the player is in the required stance
        if ((!spellInfo->Stances && spell_id != 5419) || (spellInfo->Stances & (1<<(m_form-1)) || (spell_id == 5419 && m_form == FORM_TRAVEL)))
            CastSpell(this, spell_id, true);
    }

    return true;
}

void Player::learnSpell(uint16 spell_id)
{
    SpellEntry *spellInfo = sSpellStore.LookupEntry(spell_id);
    if (!spellInfo)
    {
        sLog.outError("Player::addSpell: Non-existed in SpellStore spell #%u request.",spell_id);
        return;
    }

    WorldPacket data;
    data.Initialize(SMSG_LEARNED_SPELL);
    data <<uint32(spell_id);
    GetSession()->SendPacket(&data);

    if (!addSpell(spell_id,1))
        return;

    uint16 maxskill = getLevel()*5 > 300 ? 300 :getLevel()*5;
    switch(spell_id)
    {
        //Armor
        case 9078:                                          //Cloth
            SetSkill(415,1,1);
            break;
        case 9077:                                          //Leather
            SetSkill(414,1,1);
            break;
        case 8737:                                          //Mail
            SetSkill(413,1,1);
            break;
        case 750:                                           //Plate Mail
            SetSkill(293,1,1);
            break;
        case 9116:                                          //Shield
            SetSkill(433,1,1);
            break;
            //Melee Weapons
        case 196:                                           //Axes
            SetSkill(44,1,maxskill);
            break;
        case 197:                                           //Two-Handed Axes
            SetSkill(172,1,maxskill);
            break;
        case 227:                                           //Staves
            SetSkill(136,1,maxskill);
            break;
        case 198:                                           //Maces
            SetSkill(54,1,maxskill);
            break;
        case 199:                                           //Two-Handed Maces
            SetSkill(160,1,maxskill);
            break;
        case 201:                                           //Swords
            SetSkill(43,1,maxskill);
            break;
        case 202:                                           //Two-Handed Swords
            SetSkill(55,1,maxskill);
            break;
        case 1180:                                          //Daggers
            SetSkill(173,1,maxskill);
            break;
        case 15590:                                         //Fist Weapons
            SetSkill(473,1,maxskill);
            break;
        case 200:                                           //Polearms
            SetSkill(229,1,maxskill);
            break;
        case 3386:                                          //Polearms
            SetSkill(227,1,maxskill);
            break;
            //Range Weapons
        case 264:                                           //Bows
            SetSkill(45,1,maxskill);
            break;
        case 5011:                                          //Crossbows
            SetSkill(226,1,maxskill);
            break;
        case 266:                                           //Guns
            SetSkill(46,1,maxskill);
            break;
        case 2567:                                          //Thrown
            SetSkill(176,1,maxskill);
            break;
        case 5009:                                          //Wands
            SetSkill(228,1,maxskill);
            break;
            //Others
        case 2842:                                          //poisons
            SetSkill(40,1,maxskill);
            break;
            // Languages
        case 668: case 669: case 670: case 671:  case 672:  case 813: case 814:
        case 815: case 816: case 817: case 7340: case 7341: case 17737:
            if(LanguageDesc const* lang = GetLanguageDescBySpell(spell_id))
                SetSkill(lang->skill_id,1,300);
            break;
        default:break;
    }
}

bool Player::removeSpell(uint16 spell_id)
{
    PlayerSpellMap::iterator itr = m_spells.find(spell_id);
    if (itr != m_spells.end())
    {
        if(itr->second->state == PLAYERSPELL_REMOVED) return false;

        WorldPacket data;
        data.Initialize(SMSG_REMOVED_SPELL);
        data << itr->first;
        GetSession()->SendPacket(&data);

        if(itr->second->state == PLAYERSPELL_NEW)
        {
            delete itr->second;
            m_spells.erase(itr);
        }
        else
            itr->second->state = PLAYERSPELL_REMOVED;

        RemoveAurasDueToSpell(spell_id);
        return true;
    }
    return false;
}

void Player::_LoadSpellCooldowns()
{
    m_spellCooldowns.clear();

    QueryResult *result = sDatabase.PQuery("SELECT `spell`,`time` FROM `character_spell_cooldown` WHERE `guid` = '%u'",GetGUIDLow());

    if(result)
    {
        time_t curTime = time(NULL);

        WorldPacket data;

        data.Initialize(SMSG_SPELL_COOLDOWN);
        data << GetGUID();

        do
        {
            Field *fields = result->Fetch();

            uint32 spell_id = fields[0].GetUInt32();
            time_t db_time  = (time_t)fields[1].GetUInt64();

            if(!sSpellStore.LookupEntry(spell_id))
            {
                sLog.outError("Player %u have unknown spell %u in `character_spell_cooldown`, skipping.",GetGUIDLow(),spell_id);
                continue;
            }

            // skip outdated cooldown
            if(db_time <= curTime)
                continue;

            data << uint32(spell_id);
            data << uint32((db_time-curTime)*1000);         // in m.secs

            AddSpellCooldown(spell_id,db_time);

            sLog.outDebug("Player (GUID: %u) spell %u cooldown loaded (%u secs).",GetGUIDLow(),spell_id,uint32(db_time-curTime));
        }
        while( result->NextRow() );

        delete result;

        if(m_spellCooldowns.size() > 0)
            GetSession()->SendPacket(&data);
    }

    // setup item coldowns
    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        if(Item* pItem = GetItemByPos( INVENTORY_SLOT_BAG_0, i ))
        {
            uint32 spell_id = pItem->GetProto()->Spells[i].SpellId;
            if(spell_id != 0 && HaveSpellCooldown(spell_id))
            {
                sLog.outDebug("Item (GUID: %u Entry: %u) for spell: %u cooldown loaded.",pItem->GetGUIDLow(),pItem->GetEntry(),spell_id);
                WorldPacket data;
                data.Initialize(SMSG_ITEM_COOLDOWN);
                data << pItem->GetGUID();
                data << uint32(spell_id);
                GetSession()->SendPacket(&data);
                break;
            }
        }
    }
    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        if(Bag *pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i ))
        {
            for(uint32 j = 0; j < pBag->GetProto()->ContainerSlots; j++)
            {
                if(Item* pItem = GetItemByPos( i, j ))
                {
                    for(int i = 0; i < 5; ++i)
                    {
                        uint32 spell_id = pItem->GetProto()->Spells[i].SpellId;
                        if(spell_id != 0 && HaveSpellCooldown(spell_id))
                        {
                            sLog.outDebug("Item (GUID: %u Entry: %u) for spell: %u cooldown loaded.",pItem->GetGUIDLow(),pItem->GetEntry(),spell_id);
                            WorldPacket data;
                            data.Initialize(SMSG_ITEM_COOLDOWN);
                            data << pItem->GetGUID();
                            data << uint32(spell_id);
                            GetSession()->SendPacket(&data);
                            break;
                        }
                    }
                }
            }
        }
    }

}

void Player::_SaveSpellCooldowns()
{
    sDatabase.PExecute("DELETE FROM `character_spell_cooldown` WHERE `guid` = '%u'", GetGUIDLow());

    time_t curTime = time(NULL);

    // remove oudated and save active
    for(SpellCooldowns::iterator itr = m_spellCooldowns.begin();itr != m_spellCooldowns.end();)
    {
        if(itr->second <= curTime)
            m_spellCooldowns.erase(itr++);
        else
        {
            sDatabase.PExecute("INSERT INTO `character_spell_cooldown` (`guid`,`spell`,`time`) VALUES ('%u', '%u', '" I64FMTD "')", GetGUIDLow(), itr->first, itr->second);
            ++itr;
        }
    }
}

uint32 Player::resetTalentsCost() const
{
    // The first time reset costs 1 gold
    if(m_resetTalentsCost < 1*GOLD)
        return 1*GOLD;
    // then 5 gold
    else if(m_resetTalentsCost < 5*GOLD)
        return 5*GOLD;
    // After that it increases in increments of 5 gold
    else if(m_resetTalentsCost < 10*GOLD)
        return 10*GOLD;
    else
    {
        uint32 months = (sWorld.GetLastTickTime() - m_resetTalentsTime)/MONTH;
        if(months > 0)
        {
            // This cost will be reduced by a rate of 5 gold per month
            int32 new_cost = int32(m_resetTalentsCost) - 5*GOLD*months;
            // to a minimum of 10 gold.
            return (new_cost < 10*GOLD ? 10*GOLD : new_cost);
        }
        else
        {
            // After that it increases in increments of 5 gold
            int32 new_cost = m_resetTalentsCost + 5*GOLD;
            // until it hits a cap of 50 gold.
            if(new_cost > 50*GOLD)
                new_cost = 50*GOLD;
            return new_cost;
        }
    }
}

bool Player::resetTalents(bool no_cost)
{
    uint32 level = getLevel();
    if (level < 10 || (GetUInt32Value(PLAYER_CHARACTER_POINTS1) >= level - 9))
        return false;

    uint32 cost = 0;

    if(!no_cost)
    {
        cost = resetTalentsCost();

        if (GetMoney() < cost)
        {
            SendBuyError( BUY_ERR_NOT_ENOUGHT_MONEY, 0, 0, 0);
            return false;
        }
    }

    for (int i = 0; i < sTalentStore.GetNumRows(); i++)
    {
        TalentEntry *talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo) continue;
        for (int j = 0; j < 5; j++)
        {
            SpellEntry *spellInfo = sSpellStore.LookupEntry(talentInfo->RankID[j]);
            if (!spellInfo) continue;
            const PlayerSpellMap& s_list = GetSpellMap();
            for(PlayerSpellMap::const_iterator itr = s_list.begin(); itr != s_list.end(); ++itr)
            {
                if(itr->second->state == PLAYERSPELL_REMOVED) continue;
                if (itr->first == spellInfo->Id)
                {
                    RemoveAurasDueToSpell(itr->first);
                    removeSpell(itr->first);
                    break;
                }
            }
        }
    }

    SetUInt32Value(PLAYER_CHARACTER_POINTS1, level - 9);

    if(!no_cost)
    {
        ModifyMoney(-(int32)cost);

        m_resetTalentsCost = cost;
        m_resetTalentsTime = time(NULL);
    }
    return true;
}

bool Player::_removeSpell(uint16 spell_id)
{
    PlayerSpellMap::iterator itr = m_spells.find(spell_id);
    if (itr != m_spells.end())
    {
        delete itr->second;
        m_spells.erase(itr);
        return true;
    }
    return false;
}

Mail* Player::GetMail(uint32 id)
{
    std::list<Mail*>::iterator itr;
    for (itr = m_mail.begin(); itr != m_mail.end(); itr++)
    {
        if ((*itr)->messageID == id)
        {
            return (*itr);
        }
    }
    return NULL;
}

void Player::_SetCreateBits(UpdateMask *updateMask, Player *target) const
{
    if(target == this)
    {
        Object::_SetCreateBits(updateMask, target);
    }
    else
    {
        UpdateMask mask;
        mask.SetCount(m_valuesCount);
        _SetVisibleBits(&mask, target);

        for(uint16 index = 0; index < m_valuesCount; index++)
        {
            if(GetUInt32Value(index) != 0 && mask.GetBit(index))
                updateMask->SetBit(index);
        }
    }
}

void Player::_SetUpdateBits(UpdateMask *updateMask, Player *target) const
{
    if(target == this)
    {
        Object::_SetUpdateBits(updateMask, target);
    }
    else
    {
        UpdateMask mask;
        mask.SetCount(m_valuesCount);
        _SetVisibleBits(&mask, target);

        Object::_SetUpdateBits(updateMask, target);
        *updateMask &= mask;
    }
}

void Player::_SetVisibleBits(UpdateMask *updateMask, Player *target) const
{
    updateMask->SetBit(OBJECT_FIELD_GUID);
    updateMask->SetBit(OBJECT_FIELD_TYPE);
    updateMask->SetBit(OBJECT_FIELD_SCALE_X);

    updateMask->SetBit(UNIT_FIELD_SUMMON);
    updateMask->SetBit(UNIT_FIELD_SUMMON+1);

    updateMask->SetBit(UNIT_FIELD_TARGET);
    updateMask->SetBit(UNIT_FIELD_TARGET+1);

    updateMask->SetBit(UNIT_FIELD_HEALTH);
    updateMask->SetBit(UNIT_FIELD_POWER1);
    updateMask->SetBit(UNIT_FIELD_POWER2);
    updateMask->SetBit(UNIT_FIELD_POWER3);
    updateMask->SetBit(UNIT_FIELD_POWER4);
    updateMask->SetBit(UNIT_FIELD_POWER5);

    updateMask->SetBit(UNIT_FIELD_MAXHEALTH);
    updateMask->SetBit(UNIT_FIELD_MAXPOWER1);
    updateMask->SetBit(UNIT_FIELD_MAXPOWER2);
    updateMask->SetBit(UNIT_FIELD_MAXPOWER3);
    updateMask->SetBit(UNIT_FIELD_MAXPOWER4);
    updateMask->SetBit(UNIT_FIELD_MAXPOWER5);

    updateMask->SetBit(UNIT_FIELD_LEVEL);
    updateMask->SetBit(UNIT_FIELD_FACTIONTEMPLATE);
    updateMask->SetBit(UNIT_FIELD_BYTES_0);
    updateMask->SetBit(UNIT_FIELD_FLAGS);
    for(uint16 i = UNIT_FIELD_AURA; i < UNIT_FIELD_AURASTATE; i ++)
        updateMask->SetBit(i);
    updateMask->SetBit(UNIT_FIELD_BASEATTACKTIME);
    updateMask->SetBit(UNIT_FIELD_OFFHANDATTACKTIME);
    updateMask->SetBit(UNIT_FIELD_RANGEDATTACKTIME);
    updateMask->SetBit(UNIT_FIELD_BOUNDINGRADIUS);
    updateMask->SetBit(UNIT_FIELD_COMBATREACH);
    updateMask->SetBit(UNIT_FIELD_DISPLAYID);
    updateMask->SetBit(UNIT_FIELD_NATIVEDISPLAYID);
    updateMask->SetBit(UNIT_FIELD_MOUNTDISPLAYID);
    updateMask->SetBit(UNIT_FIELD_BYTES_1);
    updateMask->SetBit(UNIT_FIELD_MOUNTDISPLAYID);
    updateMask->SetBit(UNIT_FIELD_PETNUMBER);
    updateMask->SetBit(UNIT_FIELD_PET_NAME_TIMESTAMP);
    updateMask->SetBit(UNIT_DYNAMIC_FLAGS);

    updateMask->SetBit(PLAYER_FLAGS);
    updateMask->SetBit(PLAYER_BYTES);
    updateMask->SetBit(PLAYER_BYTES_2);
    updateMask->SetBit(PLAYER_BYTES_3);
    updateMask->SetBit(PLAYER_GUILDID);
    updateMask->SetBit(PLAYER_GUILDRANK);
    updateMask->SetBit(PLAYER_GUILD_TIMESTAMP);
    updateMask->SetBit(PLAYER_DUEL_TEAM);
    updateMask->SetBit(PLAYER_DUEL_ARBITER);
    updateMask->SetBit(PLAYER_DUEL_ARBITER+1);

    for(uint16 i = 0; i < INVENTORY_SLOT_BAG_END; i++)
    {

        updateMask->SetBit((uint16)(PLAYER_FIELD_INV_SLOT_HEAD + i*2));

        updateMask->SetBit((uint16)(PLAYER_FIELD_INV_SLOT_HEAD + (i*2) + 1));

    }
    //Players visible items are not inventory stuff
    //431) = 884 (0x374) = main weapon
    for(uint16 i = 0; i < EQUIPMENT_SLOT_END; i++)
    {
        updateMask->SetBit((uint16)(PLAYER_VISIBLE_ITEM_1_0 + (i*12)));
        //updateMask->SetBit((uint16)(PLAYER_VISIBLE_ITEM_1_0 + 1 + (i*12)));
    }

    updateMask->SetBit(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY_01);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY_02);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_INFO);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_INFO_01);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_INFO_02);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_INFO_03);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_INFO_04);
    updateMask->SetBit(UNIT_VIRTUAL_ITEM_INFO_05);

}

void Player::BuildCreateUpdateBlockForPlayer( UpdateData *data, Player *target ) const
{

    for(int i = 0; i < EQUIPMENT_SLOT_END; i++)
    {
        if(m_items[i] == NULL)
            continue;

        m_items[i]->BuildCreateUpdateBlockForPlayer( data, target );
    }

    if(target == this)
    {

        for(int i = EQUIPMENT_SLOT_END; i < BANK_SLOT_BAG_END; i++)
        {
            if(m_items[i] == NULL)
                continue;

            m_items[i]->BuildCreateUpdateBlockForPlayer( data, target );
        }
    }

    Unit::BuildCreateUpdateBlockForPlayer( data, target );
}

void Player::DestroyForPlayer( Player *target ) const
{
    Unit::DestroyForPlayer( target );

    for(int i = 0; i < INVENTORY_SLOT_BAG_END; i++)
    {
        if(m_items[i] == NULL)
            continue;

        m_items[i]->DestroyForPlayer( target );
    }

    if(target == this)
    {

        for(int i = EQUIPMENT_SLOT_END; i < BANK_SLOT_BAG_END; i++)
        {
            if(m_items[i] == NULL)
                continue;

            m_items[i]->DestroyForPlayer( target );
        }
    }
}

bool Player::HasSpell(uint32 spell) const
{
    PlayerSpellMap::const_iterator itr = m_spells.find((uint16)spell);
    return (itr != m_spells.end() && itr->second->state != PLAYERSPELL_REMOVED);

    // Look in the effects of spell , if is a Learn Spell Effect, see if is equal to triggerspell
    // If inst, look if have this spell.
    /*for (PlayerSpellMap::const_iterator itr = m_spells.begin(); itr != m_spells.end(); ++itr)
    {
        for(uint8 i=0;i<3;i++)
        {
            if(spellInfo->Effect[i]==36)                    // Learn Spell effect
            {
                if ( itr->first == spellInfo->EffectTriggerSpell[i] )
                    return true;
            }
            else if(itr->first == spellInfo->Id)
                return true;
        }
    }

    return false;*/
}

bool Player::CanLearnProSpell(uint32 spell)
{
    SpellEntry *spellInfo = sSpellStore.LookupEntry(spell);

    if (!spellInfo)
        return false;
    if(spellInfo->Effect[0] != 36)
        return true;

    uint32 skill = spellInfo->EffectMiscValue[1];
    uint32 value = 0;

    if( skill != SKILL_HERBALISM && skill != SKILL_MINING && skill != SKILL_LEATHERWORKING
        && skill != SKILL_BLACKSMITHING && skill != SKILL_ALCHEMY && skill != SKILL_ENCHANTING
        && skill != SKILL_TAILORING && skill != SKILL_ENGINERING && skill != SKILL_SKINNING)
        return true;

    for (PlayerSpellMap::const_iterator itr = m_spells.begin(); itr != m_spells.end(); ++itr)
    {
        if (itr->second->state == PLAYERSPELL_REMOVED) continue;
        SpellEntry *pSpellInfo = sSpellStore.LookupEntry(itr->first);
        if(!pSpellInfo) continue;

        if(pSpellInfo->Effect[1] == 118)
        {
            uint32 pskill = pSpellInfo->EffectMiscValue[1];
            if( pskill != SKILL_HERBALISM && pskill != SKILL_MINING && pskill != SKILL_LEATHERWORKING
                && pskill != SKILL_BLACKSMITHING && pskill != SKILL_ALCHEMY && pskill != SKILL_ENCHANTING
                && pskill != SKILL_TAILORING && pskill != SKILL_ENGINERING && pskill != SKILL_SKINNING)
                continue;

            // not check prof count for not first prof. spells (when skill already known)
            if(pskill == skill)
                return true;

            // count only first rank prof. spells
            if(FindSpellRank(pSpellInfo->Id)==1)
                value += 1;
        }
    }
    if(value >= sWorld.getConfig(CONFIG_MAX_PRIMARY_TRADE_SKILL))
        return false;
    else return true;
}

void Player::DeleteFromDB()
{
    uint32 guid = GetGUIDLow();

    // convert corpse to bones if exist (to prevent exiting Corpse in World without DB entry)
    // bones will be deleted by corpse/bones deleting thread shortly
    SpawnCorpseBones();

    // remove from guild
    if(GetGuildId() != 0)
        objmgr.GetGuildById(GetGuildId())->DelMember(guid);

    // remove signs from petitions (also remove petitions if owner);
    RemovePetitionsAndSigns(GetGUID());

    sDatabase.PExecute("DELETE FROM `character` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_aura` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_spell` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_tutorial` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_inventory` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_social` WHERE `guid` = '%u' OR `friend`='%u'",guid,guid);
    sDatabase.PExecute("DELETE FROM `mail` WHERE `receiver` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_pet` WHERE `owner` = '%u'",guid);

    //loginDatabase.PExecute("UPDATE `realmcharacters` SET `numchars` = `numchars` - 1 WHERE `acctid` = %d AND `realmid` = %d", GetSession()->GetAccountId(), realmID);
    QueryResult *resultCount = sDatabase.PQuery("SELECT COUNT(guid) FROM `character` WHERE `account` = '%d'", GetSession()->GetAccountId());
    uint32 charCount = 0;
    if (resultCount)
    {
        Field *fields = resultCount->Fetch();
        charCount = fields[0].GetUInt32();
        delete resultCount;
        loginDatabase.PExecute("INSERT INTO `realmcharacters` (`numchars`, `acctid`, `realmid`) VALUES (%d, %d, %d) ON DUPLICATE KEY UPDATE `numchars` = '%d'", charCount, GetSession()->GetAccountId(), realmID, charCount);
    }

    for(int i = 0; i < BANK_SLOT_ITEM_END; i++)
    {
        if(m_items[i] == NULL)
            continue;
        m_items[i]->DeleteFromDB();                         // Bag items delete also by virtual call Bag::DeleteFromDB
    }

    sDatabase.PExecute("DELETE FROM `character_queststatus` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_action` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_reputation` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_homebind` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_kill` WHERE `guid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `character_stable` WHERE `owner` = '%u'",guid);
}

void Player::SetMovement(uint8 pType)
{
    WorldPacket data;

    switch(pType)
    {
        case MOVE_ROOT:
        {
            data.Initialize(SMSG_FORCE_MOVE_ROOT);
            data.append(GetPackGUID());
            GetSession()->SendPacket( &data );
        }break;
        case MOVE_UNROOT:
        {
            data.Initialize(SMSG_FORCE_MOVE_UNROOT);
            data.append(GetPackGUID());
            GetSession()->SendPacket( &data );
        }break;
        case MOVE_WATER_WALK:
        {
            data.Initialize(SMSG_MOVE_WATER_WALK);
            data.append(GetPackGUID());
            GetSession()->SendPacket( &data );
        }break;
        case MOVE_LAND_WALK:
        {
            data.Initialize(SMSG_MOVE_LAND_WALK);
            data.append(GetPackGUID());
            GetSession()->SendPacket( &data );
        }break;
        default:break;
    }
}

void Player::SetPlayerSpeed(uint8 SpeedType, float value, bool forced)
{
    WorldPacket data;

    switch(SpeedType)
    {
        case MOVE_RUN:
        {
            SetSpeed( value / SPEED_RUN );
            if(forced) { data.Initialize(SMSG_FORCE_RUN_SPEED_CHANGE); }
            else { data.Initialize(MSG_MOVE_SET_RUN_SPEED); }
            data.append(GetPackGUID());
            data << (uint32)0;
            data << float(value);
            GetSession()->SendPacket( &data );
        }break;
        case MOVE_WALKBACK:
        {
            SetSpeed( value / SPEED_WALKBACK );
            if(forced) { data.Initialize(SMSG_FORCE_RUN_BACK_SPEED_CHANGE); }
            else { data.Initialize(MSG_MOVE_SET_RUN_BACK_SPEED); }
            data.append(GetPackGUID());
            data << (uint32)0;
            data << float(value);
            GetSession()->SendPacket( &data );
        }break;
        case MOVE_SWIM:
        {
            SetSpeed( value / SPEED_SWIM );
            if(forced) { data.Initialize(SMSG_FORCE_SWIM_SPEED_CHANGE); }
            else { data.Initialize(MSG_MOVE_SET_SWIM_SPEED); }
            data.append(GetPackGUID());
            data << (uint32)0;
            data << float(value);
            GetSession()->SendPacket( &data );
        }break;
        case MOVE_SWIMBACK:
        {
            SetSpeed( value / SPEED_SWIMBACK );
            data.Initialize(MSG_MOVE_SET_SWIM_BACK_SPEED);
            data.append(GetPackGUID());
            data << (uint32)0;
            data << float(value);
            GetSession()->SendPacket( &data );
        }break;
        default:break;
    }
}

void Player::BuildPlayerRepop()
{
    // place corpse instead player body
    Corpse* corpse = GetCorpse();
    if(!corpse)
        corpse = CreateCorpse();

    // now show corpse for all
    MapManager::Instance().GetMap(corpse->GetMapId())->Add(corpse);

    // convert player body to ghost
    WorldPacket data;

    SetHealth( 1 );

    SetMovement(MOVE_WATER_WALK);
    SetMovement(MOVE_UNROOT);

    // setting new speed
    if (getRace() == RACE_NIGHTELF)
    {
        SetPlayerSpeed(MOVE_RUN, (float)12.75, true);
        SetPlayerSpeed(MOVE_SWIM, (float)8.85, true);
    }
    else
    {
        SetPlayerSpeed(MOVE_RUN, (float)10.625, true);
        SetPlayerSpeed(MOVE_SWIM, (float)7.375, true);
    }

    //! corpse reclaim delay 30 * 1000ms
    data.Initialize(SMSG_CORPSE_RECLAIM_DELAY );
    data << (uint32)(CORPSE_RECLAIM_DELAY*1000);
    GetSession()->SendPacket( &data );

    // to prevent cheating
    corpse->ResetGhostTime();

    //TODO: Check/research this
    data.Initialize(SMSG_SPELL_START );
    data.append(GetPackGUID());                             //9
    data.append(GetPackGUID());                             //9
    //<< uint16(8326); //2
    data << uint32(20305);                                  //2
    data << uint16(2);
    data << uint32(0) << uint16(0);                         //6
    GetSession()->SendPacket( &data );

    data.Initialize(SMSG_SPELL_GO);
    data.append(GetPackGUID());
    data.append(GetPackGUID());
    data << uint16(8326);
                                                            /// uint8(0x0D) = probably race + 2
    data << uint16(0) << uint8(0x0D) <<  uint8(0x01)<< uint8(0x01) << GetGUID();
    data << uint32(0) << uint16(0x0200) << uint16(0);
    GetSession()->SendPacket( &data );

    data.Initialize(SMSG_UPDATE_AURA_DURATION);
    data << uint32(0x20) << uint8(0);
    GetSession()->SendPacket( &data );

    StopMirrorTimer(FATIGUE_TIMER);                         //disable timers(bars)
    StopMirrorTimer(BREATH_TIMER);
    StopMirrorTimer(FIRE_TIMER);

    SetUInt32Value(UNIT_FIELD_FLAGS, UNIT_FLAG_NONE | UNIT_FLAG_ALLOW_SWIM );
    SetUInt32Value(UNIT_FIELD_AURA + 32, 8326);             // set ghost form
    SetUInt32Value(UNIT_FIELD_AURA + 33, 0x5068 );          //!dono

    SetUInt32Value(UNIT_FIELD_AURAFLAGS + 4, 0xEE);

    SetUInt32Value(UNIT_FIELD_AURASTATE, 0x02);

    SetFloatValue(UNIT_FIELD_BOUNDINGRADIUS,(float)1.0);    //see radius of death player?

    SetUInt32Value(UNIT_FIELD_BYTES_1, 0x1000000);          //Set standing so always be standing

    if (getRace() == RACE_NIGHTELF)
        SetUInt32Value(UNIT_FIELD_DISPLAYID, 10045);        //10045 correct wisp model

    SetUInt32Value(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);
}

void Player::SendDelayResponse(const uint32 ml_seconds)
{
    WorldPacket data;
    data.Initialize( SMSG_QUERY_TIME_RESPONSE );
    data << (uint32)getMSTime();
    GetSession()->SendPacket( &data );
}

void Player::ResurrectPlayer()
{
    // remove death flag + set aura
    RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST);

    // return the PvP enable flag to normal
    SetPvP( GetPvP() );

    setDeathState(ALIVE);

    SetMovement(MOVE_LAND_WALK);
    SetMovement(MOVE_UNROOT);

    SetPlayerSpeed(MOVE_RUN, (float)7.5, true);
    SetPlayerSpeed(MOVE_SWIM, (float)4.9, true);

    SetUInt32Value(CONTAINER_FIELD_SLOT_1+29, 0);

    SetUInt32Value(UNIT_FIELD_AURA+32, 0);
    SetUInt32Value(UNIT_FIELD_AURALEVELS+8, 0xeeeeeeee);
    SetUInt32Value(UNIT_FIELD_AURAAPPLICATIONS+8, 0xeeeeeeee);
    SetUInt32Value(UNIT_FIELD_AURAFLAGS+4, 0);
    SetUInt32Value(UNIT_FIELD_AURASTATE, 0);

    if(getRace() == RACE_NIGHTELF)
    {
        DeMorph();
    }

    m_deathTimer = 0;
}

void Player::KillPlayer()
{
    SetMovement(MOVE_ROOT);

    StopMirrorTimer(FATIGUE_TIMER);                         //disable timers(bars)
    StopMirrorTimer(BREATH_TIMER);
    StopMirrorTimer(FIRE_TIMER);

    setDeathState(CORPSE);
    //SetFlag( UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_IN_PVP );

    SetFlag( UNIT_DYNAMIC_FLAGS, 0x00 );

    // 6 minutes until repop at graveyard
    m_deathTimer = 360000;

    // dead player body showed at this moment, corpse wiil be show at Player ghost repop
    CreateCorpse();
}

Corpse* Player::CreateCorpse()
{
    // prevent existance 2 corpse for player
    SpawnCorpseBones();

    uint32 _uf, _pb, _pb2, _cfb1, _cfb2;

    Corpse* corpse = new Corpse(CORPSE_RESURRECTABLE);
    if(!corpse->Create(objmgr.GenerateLowGuid(HIGHGUID_CORPSE), this, GetMapId(), GetPositionX(),
        GetPositionY(), GetPositionZ(), GetOrientation()))
    {
        delete corpse;
        return NULL;
    }

    _uf = GetUInt32Value(UNIT_FIELD_BYTES_0);
    _pb = GetUInt32Value(PLAYER_BYTES);
    _pb2 = GetUInt32Value(PLAYER_BYTES_2);

    uint8 race       = (uint8)(_uf);
    uint8 skin       = (uint8)(_pb);
    uint8 face       = (uint8)(_pb >> 8);
    uint8 hairstyle  = (uint8)(_pb >> 16);
    uint8 haircolor  = (uint8)(_pb >> 24);
    uint8 facialhair = (uint8)(_pb2);

    _cfb1 = ((0x00) | (race << 8) | (0x00 << 16) | (skin << 24));
    _cfb2 = ((face) | (hairstyle << 8) | (haircolor << 16) | (facialhair << 24));

    corpse->SetUInt32Value( CORPSE_FIELD_BYTES_1, _cfb1 );
    corpse->SetUInt32Value( CORPSE_FIELD_BYTES_2, _cfb2 );
    corpse->SetUInt32Value( CORPSE_FIELD_FLAGS, 4 );
    corpse->SetUInt32Value( CORPSE_FIELD_DISPLAY_ID, GetUInt32Value(UNIT_FIELD_DISPLAYID) );

    uint32 iDisplayID;
    uint16 iIventoryType;
    uint32 _cfi;
    for (int i = 0; i < EQUIPMENT_SLOT_END; i++)
    {
        if(m_items[i])
        {
            iDisplayID = m_items[i]->GetProto()->DisplayInfoID;
            iIventoryType = (uint16)m_items[i]->GetProto()->InventoryType;

            _cfi =  (uint16(iDisplayID)) | (iIventoryType)<< 24;
            corpse->SetUInt32Value(CORPSE_FIELD_ITEM + i,_cfi);
        }
    }

    corpse->SaveToDB();

    // register for player, but not show
    corpse->AddToWorld();
    return corpse;
}

void Player::SpawnCorpseBones()
{
    Corpse* corpse =  GetCorpse();
    if(!corpse) return;

    if( corpse->GetType() == CORPSE_RESURRECTABLE )
    {
        corpse->ConvertCorpseToBones();
        SaveToDB();                                             // prevent loading as ghost without corpse
    }
}

Corpse* Player::GetCorpse() const
{
    return ObjectAccessor::Instance().GetCorpseForPlayer(*this);
}

void Player::DurabilityLossAll(double percent)
{
    for (uint16 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; i++)
        DurabilityLoss(i,percent);
}

void Player::DurabilityLoss(uint8 equip_pos, double percent)
{
    if(!m_items[equip_pos])
        return;

    uint32 pDurability =  m_items[equip_pos]->GetUInt32Value(ITEM_FIELD_DURABILITY);

    if(!pDurability)
        return;

    uint32 pDurabilityLoss = (uint32)(pDurability*percent);

    if(pDurabilityLoss < 1 )
        pDurabilityLoss = 1;

    uint32 pNewDurability = pDurability - pDurabilityLoss;

    // we have durability 25% or 0 we should modify item stats
    // modify item stats _before_ Durability set to 0 to pass _ApplyItemMods internal check
    //        if ( pNewDurability == 0 || pNewDurability * 100 / pDurability < 25)
    if ( pNewDurability == 0 )
        _ApplyItemMods(m_items[equip_pos],equip_pos, false);

    m_items[equip_pos]->SetUInt32Value(ITEM_FIELD_DURABILITY, pNewDurability);
    m_items[equip_pos]->SetState(ITEM_CHANGED, this);
}

void Player::DurabilityRepairAll(bool cost)
{
    for (uint16 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; i++)
        DurabilityRepair(( (INVENTORY_SLOT_BAG_0 << 8) | i ),cost);
}

void Player::DurabilityRepair(uint16 pos, bool cost)
{
    Item* item = GetItemByPos(pos);

    if(!item)
        return;

    uint32 maxDurability = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
    if(!maxDurability)
        return;

    uint32 curDurability = item->GetUInt32Value(ITEM_FIELD_DURABILITY);

    // some simple repair formula depending on durability lost
    if(cost)
    {
        uint32 costs = maxDurability - curDurability;

        if (GetMoney() < costs)
        {
            DEBUG_LOG("You do not have enough money");
            return;
        }

        ModifyMoney( -int32(costs) );
    }

    item->SetUInt32Value(ITEM_FIELD_DURABILITY, maxDurability);
    item->SetState(ITEM_CHANGED, this);

    // reapply mods for total broken and repaired item if equiped
    if(IsEquipmentPos(pos) && !curDurability)
        _ApplyItemMods(item,pos & 255, true);
}

void Player::RepopAtGraveyard()
{
    WorldSafeLocsEntry *ClosestGrave = objmgr.GetClosestGraveYard( m_positionX, m_positionY, m_positionZ, GetMapId(), GetTeam() );

    if(ClosestGrave)
    {
        // stop countdown until repop
        m_deathTimer = 0;

        TeleportTo(ClosestGrave->map_id, ClosestGrave->x, ClosestGrave->y, ClosestGrave->z, GetOrientation());

        if(Corpse* corpse = GetCorpse())
            corpse->UpdateForPlayer(this,true);
    }
}

void Player::JoinedChannel(Channel *c)
{
    m_channels.push_back(c);
}

void Player::LeftChannel(Channel *c)
{
    m_channels.remove(c);
}

void Player::CleanupChannels()
{
    list<Channel *>::iterator i;
    for(i = m_channels.begin(); i != m_channels.end(); i++)
        (*i)->Leave(this,false);
}

void Player::BroadcastPacketToFriendListers(WorldPacket *packet)
{
    Field *fields;
    Player *pfriend;

    QueryResult *result = sDatabase.PQuery("SELECT `guid` FROM `character_social` WHERE `flags` = 'FRIEND' AND `friend` = '%u'", GetGUIDLow());

    if(!result) return;

    do
    {
        WorldPacket data;
        fields = result->Fetch();

        pfriend = ObjectAccessor::Instance().FindPlayer(fields[0].GetUInt64());

        if (pfriend && pfriend->IsInWorld())
            pfriend->GetSession()->SendPacket(packet);

    }while( result->NextRow() );
    delete result;
}

void Player::UpdateDefense()
{
    if(UpdateSkill(SKILL_DEFENSE))
    {
        // update dependent from defense skill part BlockChanceWithoutMods = 5 + (GetDefenceSkillValue() - getLevel()*5)*0.04);
        UpdateBlockPercentage();
    }
}

void Player::ApplyDefenseBonusesMod(float value, bool apply)
{
    ApplyModFloatValue(PLAYER_BLOCK_PERCENTAGE, value * 0.04, apply);
    ApplyModFloatValue(PLAYER_PARRY_PERCENTAGE, value * 0.04, apply);
    ApplyModFloatValue(PLAYER_DODGE_PERCENTAGE, value * 0.04, apply);
}

void Player::UpdateBlockPercentage()
{
    AuraList& mModBlockPercent = GetAurasByType(SPELL_AURA_MOD_BLOCK_PERCENT);
    for(AuraList::iterator i = mModBlockPercent.begin(); i != mModBlockPercent.end(); ++i)
        (*i)->ApplyModifier(false);

    float chance = 5 - (getLevel()*5 - GetPureDefenceSkillValue()) * 0.04;
    chance = chance < 0 ? 0 : chance;

    SetFloatValue(PLAYER_BLOCK_PERCENTAGE, chance);

    for(AuraList::iterator i = mModBlockPercent.begin(); i != mModBlockPercent.end(); ++i)
        (*i)->ApplyModifier(true);

}

//skill+1, checking for max value
bool Player::UpdateSkill(uint32 skill_id)
{
    if(!skill_id) return false;
    uint16 i=0;
    for (; i < PLAYER_MAX_SKILLS; i++)
        if ((GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF) == skill_id) break;
    if(i>=PLAYER_MAX_SKILLS) return false;

    uint32 data = GetUInt32Value(PLAYER_SKILL(i)+1);
    uint32 value = SKILL_VALUE(data);
    uint32 max = SKILL_MAX(data);

    if ((!max) || (!value) || (value >= max)) return false;

    if (value*512 < max*urand(0,512))
    {
        SetUInt32Value(PLAYER_SKILL(i)+1,data+1);
        return true;
    }

    return false;
}

void Player::UpdateSkillPro(uint32 spellid)
{
    SkillLineAbilityEntry *pAbility = sSkillLineAbilityStore.LookupEntry(spellid);
    if(!pAbility)
        return;
    uint32 minValue = pAbility->min_value;
    uint32 maxValue = pAbility->max_value;
    uint32 skill_id = pAbility->skillId;

    if(!skill_id)return;
    uint16 i=0;
    for (; i < PLAYER_MAX_SKILLS; i++)
        if ((GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF) == skill_id) break;
    if(i>=PLAYER_MAX_SKILLS) return;

    uint32 data = GetUInt32Value(PLAYER_SKILL(i)+1);
    uint32 value = SKILL_VALUE(data);
    uint32 max = SKILL_MAX(data);

    if ((!max) || (!value) || (value >= max)) return;
    //generates chance for unsuccess gain
    if (value*512 > max*urand(0,512)) return;
    if(skill_id == SKILL_POISONS && value < 125)
    {
        SetUInt32Value(PLAYER_SKILL(i)+1,data+1);
        return;
    }
    if(skill_id == SKILL_MINING && value>75)
        return;
    if(value >= maxValue+25 )
        return;
    else if(value >= maxValue)
    {
        if(urand(0,100) <30)
            SetUInt32Value(PLAYER_SKILL(i)+1,data+1);
        return;
    }
    else if(value >= minValue)
    {
        if(urand(0,100) <70)
            SetUInt32Value(PLAYER_SKILL(i)+1,data+1);
        return;
    }
    else if(value >= 1)
    {
        SetUInt32Value(PLAYER_SKILL(i)+1,data+1);
        return;
    }
    else return;

}

void Player::UpdateWeaponSkill (WeaponAttackType attType)
{
    // no skill gain in pvp
    Unit *pVictim = getVictim();
    if(pVictim && pVictim->GetTypeId() == TYPEID_PLAYER)
        return;

    switch(attType)
    {
        case BASE_ATTACK:
        {
            Item *tmpitem = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);

            if (!tmpitem || tmpitem->IsBroken())
                UpdateSkill(SKILL_UNARMED);
            else if(tmpitem->GetProto()->SubClass != ITEM_SUBCLASS_WEAPON_FISHING_POLE)
                UpdateSkill(tmpitem->GetSkill());

        };break;
        case OFF_ATTACK:
        {
            Item *tmpitem = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);

            if (tmpitem && !tmpitem->IsBroken())
                UpdateSkill(tmpitem->GetSkill());
        };break;
        case RANGED_ATTACK:
        {
            Item* tmpitem = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);

            if (tmpitem && !tmpitem->IsBroken())
                UpdateSkill(tmpitem->GetSkill());
        };break;
    }
}

void Player::UpdateCombatSkills(Unit *pVictim, WeaponAttackType attType, MeleeHitOutcome outcome, bool defence)
{
    switch(outcome)
    {
        case MELEE_HIT_CRIT:
            return;
        case MELEE_HIT_DODGE:
            return;
        case MELEE_HIT_PARRY:
            return;
        case MELEE_HIT_BLOCK:
            return;

        default:
            break;
    }

    uint32 plevel = getLevel();                             // if defence than pVictim == attacker
    uint32 greylevel = MaNGOS::XP::GetGrayLevel(plevel);
    uint32 moblevel = pVictim->getLevel();
    if(moblevel < greylevel)
        return;

    if (moblevel > plevel + 5)
        moblevel = plevel + 5;

    uint32 lvldif = moblevel - greylevel;
    if(lvldif < 3)
        lvldif = 3;

    uint32 skilldif = 5 * plevel - (defence ? GetPureDefenceSkillValue() : GetPureWeaponSkillValue(attType));
    if(skilldif <= 0)
        return;

    float chance = 3 * lvldif * skilldif / plevel;
    if(!defence)
    {
        if(getClass() == CLASS_WARRIOR || getClass() == CLASS_ROGUE)
            chance *= 0.1 * GetStat(STAT_INTELLECT);
    }

    chance = chance < 1 ? 1 : chance;                       //minimum chance to increase skill is 1%

    if(chance > urand(0,100))
    {
        if(defence)
            UpdateDefense();
        else
            UpdateWeaponSkill(attType);
    }
    else
        return;
}

void Player::ModifySkillBonus(uint32 skillid,int32 val)
{
    for (uint16 i=0; i < PLAYER_MAX_SKILLS; i++)
        if ((GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF) == skillid)
    {

        SetUInt32Value(PLAYER_SKILL(i)+2,((int32)(GetUInt32Value(PLAYER_SKILL(i)+2)))+val);
        return;
    }
}

void Player::UpdateMaxSkills()
{
    for (uint16 i=0; i < PLAYER_MAX_SKILLS; i++)
        if (GetUInt32Value(PLAYER_SKILL(i)))
    {
        uint32 pskill = GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF;
        if(pskill == SKILL_HERBALISM || pskill == SKILL_MINING || pskill ==SKILL_FISHING
            || pskill == SKILL_FIRST_AID || pskill == SKILL_COOKING || pskill == SKILL_LEATHERWORKING
            || pskill == SKILL_BLACKSMITHING || pskill == SKILL_ALCHEMY || pskill == SKILL_ENCHANTING
            || pskill == SKILL_TAILORING || pskill == SKILL_ENGINERING || pskill == SKILL_SKINNING)
            continue;
        uint32 data = GetUInt32Value(PLAYER_SKILL(i)+1);
        uint32 max = data>>16;
        uint32 max_Skill = data%0x10000+getLevel()*5*0x10000;
        if((max_Skill>>16) > 300)
            max_Skill = data%0x10000+300*0x10000;

        if(max!=1 && max != 300)
            SetUInt32Value(PLAYER_SKILL(i)+1,max_Skill);
    }
}

void Player::UpdateSkillsToMaxSkillsForLevel()
{
    for (uint16 i=0; i < PLAYER_MAX_SKILLS; i++)
        if (GetUInt32Value(PLAYER_SKILL(i)))
    {
        uint32 pskill = GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF;
        if(pskill == SKILL_HERBALISM || pskill == SKILL_MINING || pskill ==SKILL_FISHING
            || pskill == SKILL_FIRST_AID || pskill == SKILL_COOKING || pskill == SKILL_LEATHERWORKING
            || pskill == SKILL_BLACKSMITHING || pskill == SKILL_ALCHEMY || pskill == SKILL_ENCHANTING
            || pskill == SKILL_TAILORING || pskill == SKILL_ENGINERING || pskill == SKILL_SKINNING)
            continue;
        uint32 data = GetUInt32Value(PLAYER_SKILL(i)+1);

        uint32 max = data>>16;

        if(max > 1)
        {
            uint32 new_data = max * 0x10000 + max;
            SetUInt32Value(PLAYER_SKILL(i)+1,new_data);
        }
        if(pskill == SKILL_DEFENSE)
        {
            UpdateBlockPercentage();
        }
    }
}

// This functions sets a skill line value (and adds if doesn't exist yet)
// To "remove" a skill line, set it's values to zero
void Player::SetSkill(uint32 id, uint16 currVal, uint16 maxVal)
{
    if(!id) return;
    uint16 i=0;
    for (; i < PLAYER_MAX_SKILLS; i++)
        if ((GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF) == id) break;

    if(i<PLAYER_MAX_SKILLS)                                 //has skill
    {
        if(currVal)
            SetUInt32Value(PLAYER_SKILL(i)+1,currVal+maxVal*0x10000);
        else                                                //remove
        {
            SetUInt64Value(PLAYER_SKILL(i),0);
            SetUInt32Value(PLAYER_SKILL(i)+2,0);
            // remove spells that depend on this skill when removing the skill
            for (PlayerSpellMap::const_iterator itr = m_spells.begin(), next = m_spells.begin(); itr != m_spells.end(); itr = next)
            {
                next++;
                if(itr->second->state == PLAYERSPELL_REMOVED) continue;
                SkillLineAbilityEntry *ability = sSkillLineAbilityStore.LookupEntry(itr->first);
                if (ability && ability->skillId == id)
                    removeSpell(itr->first);
            }
        }
    }else if(currVal)                                       //add
    {

        for (i=0; i < PLAYER_MAX_SKILLS; i++)
            if (!GetUInt32Value(PLAYER_SKILL(i)))
        {
            SkillLineEntry *pSkill = sSkillLineStore.LookupEntry(id);
            if(!pSkill)
            {
                sLog.outError("Skill not found in SkillLineStore: skill #%u", id);
                return;
            }
            // enable unlearn button for professions only
            if (pSkill->categoryId == 11)
                SetUInt32Value(PLAYER_SKILL(i), id | (1 << 16));
            else
                SetUInt32Value(PLAYER_SKILL(i),id);
            SetUInt32Value(PLAYER_SKILL(i)+1,maxVal*0x10000+currVal);
            // apply skill bonuses
            SetUInt32Value(PLAYER_SKILL(i)+2,0);
            AuraList& mModSkill = GetAurasByType(SPELL_AURA_MOD_SKILL);
            for(AuraList::iterator i = mModSkill.begin(); i != mModSkill.end(); ++i)
                if ((*i)->GetModifier()->m_miscvalue == id)
                    (*i)->ApplyModifier(true);
            AuraList& mModSkillTalent = GetAurasByType(SPELL_AURA_MOD_SKILL_TALENT);
            for(AuraList::iterator i = mModSkillTalent.begin(); i != mModSkillTalent.end(); ++i)
                if ((*i)->GetModifier()->m_miscvalue == id)
                    (*i)->ApplyModifier(true);
            return;
        }

    }

}

bool Player::HasSkill(uint32 skill) const
{
    if(!skill)return false;
    for (uint16 i=0; i < PLAYER_MAX_SKILLS; i++)
    {
        if ((GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF) == skill)
        {
            return true;
        }
    }
    return false;
}

uint16 Player::GetSkillValue(uint32 skill) const
{
    if(!skill)return 0;
    for (uint16 i=0; i < PLAYER_MAX_SKILLS; i++)
    {
        if ((GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF) == skill)
        {
            return SKILL_VALUE(GetUInt32Value(PLAYER_SKILL(i)+1))+GetUInt32Value(PLAYER_SKILL(i)+2);
        }
    }
    return 0;
}

uint16 Player::GetPureSkillValue(uint32 skill) const
{
    if(!skill)return 0;
    for (uint16 i=0; i < PLAYER_MAX_SKILLS; i++)
    {
        if ((GetUInt32Value(PLAYER_SKILL(i)) & 0x0000FFFF) == skill)
        {
            return SKILL_VALUE(GetUInt32Value(PLAYER_SKILL(i)+1));
        }
    }
    return 0;
}

void Player::SendInitialActions()
{
    sLog.outDetail( "Initializing Action Buttons for '%u'", GetGUIDLow() );
    WorldPacket data;
    uint16 button=0;

    std::list<struct actions>::iterator itr;
    data.Initialize(SMSG_ACTION_BUTTONS);
    for (itr = m_actions.begin(); itr != m_actions.end();)
    {
        if (itr->button == button)
        {
            data << uint16(itr->action);
            data << uint8(itr->misc);
            data << uint8(itr->type);
            ++itr;
        }
        else
        {
            data << uint32(0);
        }
        button++;
    }

    if (button < 120 )
    {
        for (int temp_counter=(120-button); temp_counter>0; temp_counter--)
        {
            data << uint32(0);
        }
    }
    GetSession()->SendPacket( &data );
    sLog.outDetail( "Action Buttons for '%u' Initialized", GetGUIDLow() );
}

void Player::addAction(const uint8 button, const uint16 action, const uint8 type, const uint8 misc)
{
    // check cheating with adding non-known spells to action bar
    if(type==ACTION_BUTTON_SPELL)
    {
        if(!sSpellStore.LookupEntry(action))
        {
            sLog.outError( "Action %u not added into button %u for player %s: spell not exist", action, button, GetName() );
            return;
        }

        if(!HasSpell(action))
        {
            sLog.outError( "Action %u not added into button %u for player %s: player don't known this spell", action, button, GetName() );
            return;
        }
    }

    bool ButtonExists = false;
    std::list<struct actions>::iterator itr;
    for (itr = m_actions.begin(); itr != m_actions.end(); ++itr)
    {
        if (itr->button == button)
        {
            itr->button=button;
            itr->action=action;
            itr->type=type;
            itr->misc=misc;
            ButtonExists = true;
            break;
        }
    }
    if (!ButtonExists)
    {
        struct actions newaction;
        newaction.button=button;
        newaction.action=action;
        newaction.type=type;
        newaction.misc=misc;
        m_actions.push_back(newaction);
    }
    sLog.outDetail( "Player '%u' Added Action '%u' to Button '%u'", GetGUIDLow(), action, button );
}

void Player::removeAction(uint8 button)
{
    std::list<struct actions>::iterator itr;
    for (itr = m_actions.begin(); itr != m_actions.end(); ++itr)
    {
        if (itr->button == button)
        {
            m_actions.erase(itr);
            break;
        }
    }
    sLog.outDetail( "Action Button '%u' Removed from Player '%u'", button, GetGUIDLow() );
}

void Player::SetDontMove(bool dontMove)
{
    m_dontMove = dontMove;
}

bool Player::IsGroupMember(Player *plyr)
{
    if(!plyr->IsInGroup())
        return false;
    Group *grp = objmgr.GetGroupByLeader(plyr->GetGroupLeader());
    if(grp->GroupCheck(plyr->GetGUID()))
    {
        return true;
    }
    return false;
}

bool Player::SetPosition(float x, float y, float z, float orientation)
{
    Map *m = MapManager::Instance().GetMap(m_mapId);

    const float old_x = m_positionX;
    const float old_y = m_positionY;
    const float old_r = m_orientation;

    if( old_x != x || old_y != y || old_r != orientation)
    {
        m->PlayerRelocation(this, x, y, z, orientation);

        // remove at movement non-move stealth aura
        if(HasFlag(UNIT_FIELD_BYTES_1,0x2000000))
            RemoveAurasDueToSpell(20580);
    }

    // reread after Ma::Relocation
    m = MapManager::Instance().GetMap(GetMapId());
    x = m_positionX;
    y = m_positionY;
    z = m_positionZ;

    float water_z = m->GetWaterLevel(x,y);
    uint8 flag1 = m->GetTerrainType(x,y);

    //!Underwater check
    if ((z < (water_z - 2)) && (flag1 & 0x01))
        m_isunderwater|= 0x01;
    else if (z > (water_z - 2))
        m_isunderwater&= 0x7A;

    //!in lava check
    if ((z < (water_z - 0)) && (flag1 & 0x02))
        m_isunderwater|= 0x80;

    // form checks
    if ( IsUnderWater() )
    {
        // remove travel forms
        if(m_form == FORM_TRAVEL || m_form == FORM_GHOSTWOLF)
            RemoveAurasDueToSpell(m_ShapeShiftForm);
    }
    // IsInWater check ignore bridge and underwater ways case, check additional z
    else if( !IsInWater() && z < water_z + 1 )
    {
        if(m_form == FORM_AQUA)
            RemoveAurasDueToSpell(m_ShapeShiftForm);
    }

    CheckExploreSystem();

    return true;
}

void Player::SendMessageToSet(WorldPacket *data, bool self)
{
    MapManager::Instance().GetMap(m_mapId)->MessageBoardcast(this, data, self);
}

void Player::SendMessageToOwnTeamSet(WorldPacket *data, bool self)
{
    MapManager::Instance().GetMap(m_mapId)->MessageBoardcast(this, data, self,true);
}

void Player::SendDirectMessage(WorldPacket *data)
{
    GetSession()->SendPacket(data);
}

void Player::CheckExploreSystem()
{

    if (!isAlive())
        return;

    if (isInFlight())
        return;

    WorldPacket data;
    uint16 areaFlag=MapManager::Instance().GetMap(GetMapId())->GetAreaFlag(m_positionX,m_positionY);
    if(areaFlag==0xffff)return;
    int offset = areaFlag / 32;

    if(offset >= 64)
    {
        sLog.outError("ERROR: Wrong area flag %u in map data for (X: %f Y: %f) point to field PLAYER_EXPLORED_ZONES_1 + %u ( %u must be < 64 ).",areaFlag,m_positionX,m_positionY,offset,offset);
        return;
    }

    uint32 val = (uint32)(1 << (areaFlag % 32));
    uint32 currFields = GetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset);

    if( !(currFields & val) )
    {
        SetUInt32Value(PLAYER_EXPLORED_ZONES_1 + offset, (uint32)(currFields | val));

        AreaTableEntry *p = GetAreaEntryByAreaFlag(areaFlag);
        if(!p)
        {
            sLog.outError("PLAYER: Player %u discovered unknown area (x: %f y: %f map: %u", GetGUIDLow(), m_positionX,m_positionY,GetMapId());
        }
        else if(p->area_level > 0)
        {
            uint32 area = p->ID;
            if (getLevel() >= sWorld.getConfig(CONFIG_MAX_PLAYER_LEVEL))
            {
                SendExplorationExperience(area,0);
            }
            else
            {
                uint32 XP = uint32(p->area_level*10*sWorld.getRate(RATE_XP_EXPLORE));
                GiveXP( XP, NULL );
                SendExplorationExperience(area,XP);
            }
            sLog.outDetail("PLAYER: Player %u discovered a new area: %u", GetGUIDLow(), area);
        }
    }

}

uint32 Player::TeamForRace(uint8 race)
{
    ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(race);
    if(!rEntry)
    {
        sLog.outError("Race %u not found in DBC: wrong DBC files?",uint32(race));
        return ALLIANCE;
    }

    switch(rEntry->TeamID)
    {
        case 7: return ALLIANCE;
        case 1: return HORDE;
    }

    sLog.outError("Race %u have wrong team id in DBC: wrong DBC files?",uint32(race),rEntry->TeamID);
    return ALLIANCE;
}

void Player::setFactionForRace(uint8 race)
{
    m_team = TeamForRace(race);

    ChrRacesEntry const* rEntry = sChrRacesStore.LookupEntry(race);
    if(!rEntry)
    {
        sLog.outError("Race %u not found in DBC: wrong DBC files?",uint32(race));
        return;
    }

    setFaction( rEntry->FactionID );
}

void Player::UpdateReputation() const
{
    std::list<struct Factions>::const_iterator itr;

    sLog.outDebug( "WORLD: Player::UpdateReputation" );

    for(itr = factions.begin(); itr != factions.end(); ++itr)
    {
        SendSetFactionStanding(&*itr);
    }
}

void Player::SendSetFactionStanding(const Factions* faction) const
{
    WorldPacket data;

    if(faction->Flags & 0x00000001 )                        //If faction is visible then update it
    {
        data.Initialize(SMSG_SET_FACTION_STANDING);
        data << (uint32) faction->Flags;
        data << (uint32) faction->ReputationListID;
        data << (uint32) faction->Standing;
        GetSession()->SendPacket(&data);
    }
}

bool Player::FactionIsInTheList(uint32 faction)
{
    std::list<struct Factions>::iterator itr;

    for(itr = factions.begin(); itr != factions.end(); ++itr)
    {
        if(itr->ReputationListID == faction) return true;
    }
    return false;
}

void Player::SetInitialFactions()
{
    Factions newFaction;
    FactionEntry *factionEntry = NULL;

    for(unsigned int i = 1; i <= sFactionStore.GetNumRows(); i++)
    {
        factionEntry = sFactionStore.LookupEntry(i);

        if( factionEntry && (factionEntry->reputationListID >= 0))
        {
            newFaction.ID = factionEntry->ID;
            newFaction.ReputationListID = factionEntry->reputationListID;
            newFaction.Standing = 0;
            //Set visible to factions of own team
            if( GetTeam() == factionEntry->team ) newFaction.Flags = 1;
            else newFaction.Flags = 0;

            //If the faction's team is enemy of my one we are at war!
            if(GetTeam() == ALLIANCE )
            {
                if( factionEntry->team == HORDE || factionEntry->team == HORDE_FORCES )
                    newFaction.Flags = (newFaction.Flags | 2);
            }
            else
            if(GetTeam() == HORDE    )
            {
                if( factionEntry->team == ALLIANCE || factionEntry->team == ALLIANCE_FORCES )
                    newFaction.Flags = (newFaction.Flags | 2);
            }

            factions.push_back(newFaction);
        }
    }
}

uint32 Player::GetStanding(uint32 faction) const
{
    FactionTemplateEntry *factionTemplateEntry = sFactionTemplateStore.LookupEntry(faction);

    if(!factionTemplateEntry)
    {
        sLog.outError("Player::GetStanding: Can't get reputation of %s for unknown faction (faction template id) #%u.",GetName(),faction);
        return 0;
    }

    FactionEntry *factionEntry = sFactionStore.LookupEntry(factionTemplateEntry->faction);

    // Faction without recorded reputation. Just ignore.
    if(!factionEntry)
        return 0;

    std::list<struct Factions>::const_iterator itr;
    for(itr = factions.begin(); itr != factions.end(); ++itr)
    {
        if(int32(itr->ReputationListID) == factionEntry->reputationListID)
        {
            return itr->Standing;
        }
    }
    return 0;
}

bool Player::SetStanding(uint32 faction, int standing)
{
    FactionTemplateEntry *factionTemplateEntry = sFactionTemplateStore.LookupEntry(faction);

    if(!factionTemplateEntry)
    {
        sLog.outError("Player::SetStanding: Can't update reputation of %s for unknown faction (faction template id) #%u.",GetName(),faction);
        return false;
    }

    FactionEntry *factionEntry = sFactionStore.LookupEntry(factionTemplateEntry->faction);

    // Faction without recorded reputation. Just ignore.
    if(!factionEntry)
        return false;

    return ModifyFactionReputation(factionEntry,standing);
}

bool Player::ModifyFactionReputation(FactionEntry* factionEntry, int32 standing)
{
    std::list<struct Factions>::iterator itr;
    for(itr = factions.begin(); itr != factions.end(); ++itr)
    {
        if(int32(itr->ReputationListID) == factionEntry->reputationListID)
        {
            itr->Standing = (((int32)itr->Standing + standing) > 0 ? itr->Standing + standing: 0);
            itr->Flags = (itr->Flags | 0x00000001);
            SendSetFactionStanding(&*itr);
            return true;
        }
    }
    return false;
}

//Calculates how many reputation points player gains in wich victim's enemy factions
void Player::CalculateReputation(Unit *pVictim)
{
    if( !pVictim ) return;

    if( pVictim->GetTypeId() != TYPEID_PLAYER )
    {
        SetStanding( pVictim->getFaction(), (-100) );
    }
}

//Calculate how many reputation points player gain with the quest
void Player::CalculateReputation(Quest *pQuest, uint64 guid)
{
    Creature *pCreature = ObjectAccessor::Instance().GetCreature(*this, guid);
    if( pCreature )
    {
        int dif = getLevel() - pQuest->GetMinLevel();
        if(dif < 0) dif = 0;
        else if(dif > 5) dif = 5;

        int RepPoints = (uint32)(((5-dif)*0.20)*(100.0f + m_AuraModifiers[SPELL_AURA_MOD_REPUTATION_GAIN]));
        // correct would be multiplicative but currently only one such aura in game

        SetStanding(pCreature->getFaction(), (RepPoints > 0 ? RepPoints : 1) );
    }

    // special quest reputation reward/losts
    if(pQuest->GetRewRepFaction1() && pQuest->GetRewRepValue1() )
        SetStanding(pQuest->GetRewRepFaction1(), pQuest->GetRewRepValue1() );

    if(pQuest->GetRewRepFaction2() && pQuest->GetRewRepValue2() )
        SetStanding(pQuest->GetRewRepFaction2(), pQuest->GetRewRepValue2() );
}

//Update honor fields
void Player::UpdateHonor(void)
{
    WorldPacket data;

    time_t rawtime;
    struct tm * now;
    uint32 today = 0;
    uint32 date = 0;

    uint32 Yestarday = 0;
    uint32 ThisWeekBegin = 0;
    uint32 ThisWeekEnd = 0;
    uint32 LastWeekBegin = 0;
    uint32 LastWeekEnd = 0;

    uint32 lifetime_honorableKills = 0;
    uint32 lifetime_dishonorableKills = 0;
    uint32 today_honorableKills = 0;
    uint32 today_dishonorableKills = 0;

    uint32 yestardayKills = 0;
    uint32 thisWeekKills = 0;
    uint32 lastWeekKills = 0;

    float total_honor = 0;
    float yestardayHonor = 0;
    float thisWeekHonor = 0;
    float lastWeekHonor = 0;

    time( &rawtime );
    now = localtime( &rawtime );

    today = ((uint32)(now->tm_year << 16)|(uint32)(now->tm_yday));

    Yestarday     = today - 1;
    ThisWeekBegin = today - now->tm_wday;
    ThisWeekEnd   = ThisWeekBegin + 7;
    LastWeekBegin = ThisWeekBegin - 7;
    LastWeekEnd   = LastWeekBegin + 7;

    sLog.outDetail("PLAYER: UpdateHonor");

    QueryResult *result = sDatabase.PQuery("SELECT `type`,`honor`,`date` FROM `character_kill` WHERE `guid` = '%u'", GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();
            date = fields[2].GetUInt32();

            if(fields[0].GetUInt32() == HONORABLE_KILL)
            {
                lifetime_honorableKills++;
                //total_honor += fields[1].GetFloat();

                if( date == today)
                {
                    today_honorableKills++;
                }
                if( date == Yestarday)
                {
                    yestardayKills++;
                    yestardayHonor += fields[1].GetFloat();
                }
                if( (date >= ThisWeekBegin) && (date < ThisWeekEnd) )
                {
                    thisWeekKills++;
                    thisWeekHonor += fields[1].GetFloat();
                }
                if( (date >= LastWeekBegin) && (date < LastWeekEnd) )
                {
                    lastWeekKills++;
                    lastWeekHonor += fields[1].GetFloat();
                }

                //All honor points until last week
                if( date < LastWeekEnd )
                {
                    total_honor += fields[1].GetFloat();
                }

            }
            else if(fields[0].GetUInt32() == DISHONORABLE_KILL)
            {
                lifetime_dishonorableKills++;
                //total_honor -= fields[1].GetFloat();

                if( date == today)
                {
                    today_dishonorableKills++;
                }

                //All honor points until last week
                if( date < LastWeekEnd )
                {
                    total_honor -= fields[1].GetFloat();
                }
            }
        }
        while( result->NextRow() );

        delete result;
    }

    //Store Total Honor points...
    SetTotalHonor(total_honor);

    //RIGHEST RANK
    //If the new rank is highest then the old one, then m_highest_rank is updated
    if( CalculateHonorRank(total_honor) > GetHonorHighestRank() )
    {
        SetHonorHighestRank( CalculateHonorRank(total_honor) );
    }

    //RATING
    SetHonorRating( MaNGOS::Honor::CalculeRating(this) );

    //STANDING
    SetHonorLastWeekStanding( MaNGOS::Honor::CalculeStanding(this) );

    //TODO Fix next rank bar... it is not working fine! For while it be set with the total honor points...
    //NEXT RANK BAR
    SetUInt32Value(PLAYER_FIELD_HONOR_BAR, (uint32)( (total_honor < 0) ? 0: total_honor) );

    //RANK (Patent)
    if( CalculateHonorRank(total_honor) )
        SetUInt32Value(PLAYER_BYTES_3, (( CalculateHonorRank(total_honor) << 24) + 0x04000000) + (m_drunk & 0xFFFE) + getGender());
    else
        SetUInt32Value(PLAYER_BYTES_3, (m_drunk & 0xFFFE) + getGender());

    //TODAY
    SetUInt32Value(PLAYER_FIELD_SESSION_KILLS, (today_dishonorableKills << 16) + today_honorableKills );
    //YESTERDAY
    SetUInt32Value(PLAYER_FIELD_YESTERDAY_KILLS, yestardayKills);
    SetUInt32Value(PLAYER_FIELD_YESTERDAY_CONTRIBUTION, (uint32)yestardayHonor);
    //THIS WEEK
    SetUInt32Value(PLAYER_FIELD_THIS_WEEK_KILLS, thisWeekKills);
    SetUInt32Value(PLAYER_FIELD_THIS_WEEK_CONTRIBUTION, (uint32)thisWeekHonor);
    //LAST WEEK
    SetUInt32Value(PLAYER_FIELD_LAST_WEEK_KILLS, lastWeekKills);
    SetUInt32Value(PLAYER_FIELD_LAST_WEEK_CONTRIBUTION, (uint32)lastWeekHonor);
    SetUInt32Value(PLAYER_FIELD_LAST_WEEK_RANK, GetHonorLastWeekStanding());

    //LIFE TIME
    SetUInt32Value(PLAYER_FIELD_SESSION_KILLS, (lifetime_dishonorableKills << 16) + lifetime_honorableKills );
    SetUInt32Value(PLAYER_FIELD_LIFETIME_DISHONORABLE_KILLS, lifetime_dishonorableKills);
    SetUInt32Value(PLAYER_FIELD_LIFETIME_HONORABLE_KILLS, lifetime_honorableKills);
    //TODO: Into what field we need to set it? Fix it!
    SetUInt32Value(PLAYER_FIELD_PVP_MEDALS/*???*/, (GetHonorHighestRank() != 0 ? ((GetHonorHighestRank() << 24) + 0x040F0001) : 0) );
}

uint32 Player::GetHonorRank() const
{
    return CalculateHonorRank(m_total_honor_points);
}

//What is Player's rank... private, scout...
uint32 Player::CalculateHonorRank(float honor_points) const
{
    int rank = 0;

    if(honor_points <=    0.00) rank = 0; else
        if(honor_points <  2000.00) rank = 1;
    else
        rank = ( (int)(honor_points / 5000) + 1);

    return rank;
}

//How many times Player kill pVictim...
int Player::CalculateTotalKills(Player *pVictim) const
{
    int total_kills = 0;

    QueryResult *result = sDatabase.PQuery("SELECT `honor` FROM `character_kill` WHERE `guid` = '%u' AND `creature_template` = '%u'", GetGUIDLow(), pVictim->GetEntry());

    if(result)
    {
        total_kills = result->GetRowCount();
        delete result;
    }
    return total_kills;
}

//How much honor Player gains/loses killing uVictim
void Player::CalculateHonor(Unit *uVictim)
{
    float parcial_honor_points = 0;
    int kill_type = 0;
    bool savekill = false;

    sLog.outDetail("PLAYER: CalculateHonor");

    if( !uVictim ) return;

    if( uVictim->GetTypeId() == TYPEID_UNIT )
    {
        Creature *cVictim = (Creature *)uVictim;
        if( cVictim->isCivilian() )
        {
            parcial_honor_points = MaNGOS::Honor::DishonorableKillPoints( getLevel() );
            kill_type = DISHONORABLE_KILL;
            savekill = true;
        }
    }
    else
    if( uVictim->GetTypeId() == TYPEID_PLAYER )
    {
        Player *pVictim = (Player *)uVictim;

        if( GetTeam() == pVictim->GetTeam() ) return;

        if( getLevel() < (pVictim->getLevel()+5) )
        {
            parcial_honor_points = MaNGOS::Honor::HonorableKillPoints( this, pVictim );
            kill_type = HONORABLE_KILL;
            savekill = true;
        }
    }

    if (savekill)
    {
        time_t rawtime;
        struct tm * now;
        uint32 today = 0;
        time( &rawtime );
        now = localtime( &rawtime );
        today = ((uint32)(now->tm_year << 16)|(uint32)(now->tm_yday));

        sDatabase.PExecute("INSERT INTO `character_kill` (`guid`,`creature_template`,`honor`,`date`,`type`) VALUES (%u, %u, %f, %u, %u)", (uint32)GetGUIDLow(), (uint32)uVictim->GetEntry(), (float)parcial_honor_points, (uint32)today, (uint8)kill_type);

        UpdateHonor();
    }
}

uint32 Player::GetGuildIdFromDB(uint64 guid)
{
    std::ostringstream ss;
    ss<<"SELECT `guildid` FROM `guild_member` WHERE `guid`='"<<guid<<"'";
    QueryResult *result = sDatabase.Query( ss.str().c_str() );
    if( result )
        return (*result)[0].GetUInt32();
    else
        return 0;
}

uint32 Player::GetRankFromDB(uint64 guid)
{
    std::ostringstream ss;
    ss<<"SELECT `rank` FROM `guild_member` WHERE `guid`='"<<guid<<"'";
    QueryResult *result = sDatabase.Query( ss.str().c_str() );
    if( result )
        return (*result)[0].GetUInt32();
    else
        return 0;
}

uint32 Player::GetZoneIdFromDB(uint64 guid)
{
    std::ostringstream ss;
    ss<<"SELECT `map`,`position_x`,`position_y` FROM `character` WHERE `guid`='"<<guid<<"'";
    QueryResult *result = sDatabase.Query( ss.str().c_str() );
    if( !result )
        return 0;

    return MapManager::Instance().GetMap((*result)[0].GetUInt32())->GetZoneId((*result)[0].GetFloat(),(*result)[0].GetFloat());
}

//If players are too far way of duel flag... then player loose the duel
void Player::CheckDuelDistance()
{
    if(!duel) return;

    uint64 duelFlagGUID = GetUInt64Value(PLAYER_DUEL_ARBITER);

    GameObject* obj = ObjectAccessor::Instance().GetGameObject(*this, duelFlagGUID);

    //If the distance of duel flag is > 50
    if( !obj || !IsWithinDist(obj, 50))
    {
        DuelComplete(2);
    }

}

//type: 0=cleanup ; 1=i won ; 2=i fled
void Player::DuelComplete(uint8 type)
{
    // duel not requested
    if(!duel)
        return;

    WorldPacket data;

    data.Initialize(SMSG_DUEL_COMPLETE);
    data << (uint8)((type!=0) ? 1 : 0);
    GetSession()->SendPacket(&data);
    duel->opponent->GetSession()->SendPacket(&data);

    if(type != 0)
    {
        data.Initialize(SMSG_DUEL_WINNER);
        data << (uint8)((type==1) ? 0 : 1);    // 0 = just won; 1 = fled
        data << duel->opponent->GetName();
        data << GetName();
        SendMessageToSet(&data,true);
    }
    
    // cool-down duel spell
    data.Initialize(SMSG_SPELL_COOLDOWN);
    data<<(uint32)7266;
    data<<GetGUID();
    GetSession()->SendPacket(&data);
    data.Initialize(SMSG_SPELL_COOLDOWN);
    data<<(uint32)7266;
    data<<duel->opponent->GetGUID();
    duel->opponent->GetSession()->SendPacket(&data);

    //Remove Duel Flag object
    GameObject* obj = ObjectAccessor::Instance().GetGameObject(*this, GetUInt64Value(PLAYER_DUEL_ARBITER));
    if(obj)
        duel->initiator->RemoveGameObject(obj,true);

    /* remove auras */
    vector<uint32> auras2remove;
    AuraMap& vAuras = duel->opponent->GetAuras();
    for (AuraMap::iterator i = vAuras.begin(); i != vAuras.end(); i++)
    {
        if (!i->second->IsPositive() && i->second->GetCasterGUID() == GetGUID() && i->second->GetAuraApplyTime() >= duel->startTime)
            auras2remove.push_back(i->second->GetId());
    }
    for(int i=0; i<auras2remove.size(); i++)
        duel->opponent->RemoveAurasDueToSpell(auras2remove[i]);

    auras2remove.clear();
    AuraMap& Auras = GetAuras();
    for (AuraMap::iterator i = Auras.begin(); i != Auras.end(); i++)
    {
        if (!i->second->IsPositive() && i->second->GetCasterGUID() == duel->opponent->GetGUID() && i->second->GetAuraApplyTime() >= duel->startTime)
            auras2remove.push_back(i->second->GetId());
    }
    for(int i=0; i<auras2remove.size(); i++)
        RemoveAurasDueToSpell(auras2remove[i]);

    //cleanups
    SetUInt64Value(PLAYER_DUEL_ARBITER, 0);
    SetUInt32Value(PLAYER_DUEL_TEAM, 0);
    duel->opponent->SetUInt64Value(PLAYER_DUEL_ARBITER, 0);
    duel->opponent->SetUInt32Value(PLAYER_DUEL_TEAM, 0);

    delete duel->opponent->duel;
    duel->opponent->duel = NULL;
    delete duel;
    duel = NULL;
}

static uint32 holdrand = 0x89abcdef;

void Rand_Init(uint32 seed)
{
    holdrand = seed;
}

int32 irand(int32 min, int32 max)
{
    assert((max - min) < 32768);

    max++;
    holdrand = (holdrand * 214013) + 2531011;

    return (((holdrand >> 17) * (max - min)) >> 15) + min;
}

//---------------------------------------------------------//
//       Flight callback
void Player::FlightComplete()
{
    clearUnitState(UNIT_STAT_IN_FLIGHT);
    SetMoney( m_dismountCost);
    Unmount();
}

void Player::_ApplyItemMods(Item *item, uint8 slot,bool apply)
{
    if(slot >= INVENTORY_SLOT_BAG_END || !item) return;

    // not apply/remove mods for broken item
    if(item->IsBroken()) return;

    ItemPrototype const *proto = item->GetProto();

    if(!proto) return;

    sLog.outDetail("applying mods for item %u ",item->GetGUIDLow());
    if(proto->ItemSet)
    {
        if (apply)
            AddItemsSetItem(this,item);
        else
            RemoveItemsSetItem(this,proto);
    }

    _RemoveStatsMods();
    AuraList& mModBaseResistancePct = GetAurasByType(SPELL_AURA_MOD_BASE_RESISTANCE_PCT);
    AuraList& mModDamagePercentDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE);
    for(AuraList::iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        (*i)->ApplyModifier(false);
    for(AuraList::iterator i = mModBaseResistancePct.begin(); i != mModBaseResistancePct.end(); ++i)
        (*i)->ApplyModifier(false);

    _ApplyItemBonuses(proto,slot,apply);

    for(AuraList::iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        (*i)->ApplyModifier(true);
    for(AuraList::iterator i = mModBaseResistancePct.begin(); i != mModBaseResistancePct.end(); ++i)
        (*i)->ApplyModifier(true);
    _ApplyStatsMods();

    if(apply)
        CastItemEquipSpell(item);
    else
        for (int i = 0; i < 5; i++)
            if(proto->Spells[i].SpellId)
                RemoveAurasDueToSpell(proto->Spells[i].SpellId );

    for(int enchant_slot =  0 ; enchant_slot < 7; enchant_slot++)
    {
        uint32 Enchant_id = item->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+enchant_slot*3);
        if(Enchant_id)
            AddItemEnchant(item,Enchant_id, apply);
    }

    sLog.outDebug("_ApplyItemMods complete.");
}

void Player::_ApplyItemBonuses(ItemPrototype const *proto,uint8 slot,bool apply)
{
    if(slot >= INVENTORY_SLOT_BAG_END || !proto) return;

    int32 val;
    std::string typestr;
    std::string applystr = apply ? "Add" : "Remove";
    for (int i = 0; i < 10; i++)
    {
        val = proto->ItemStat[i].ItemStatValue ;

        switch (proto->ItemStat[i].ItemStatType)
        {
            case ITEM_STAT_POWER:                           // modify MP
                ApplyMaxPowerMod(POWER_MANA, val, apply);
                typestr = "Mana";
                break;
            case ITEM_STAT_HEALTH:                          // modify HP
                ApplyMaxHealthMod(val, apply);
                typestr = "Health";
                break;
            case ITEM_STAT_AGILITY:                         // modify agility
                ApplyStatMod(STAT_AGILITY,                val, apply);
                ApplyPosStatMod(STAT_AGILITY,             val, apply);
                typestr = "AGILITY";
                break;
            case ITEM_STAT_STRENGTH:                        //modify strength
                ApplyStatMod(STAT_STRENGTH,               val, apply);
                ApplyPosStatMod(STAT_STRENGTH,            val, apply);
                typestr = "STRENGHT";
                break;
            case ITEM_STAT_INTELLECT:                       //modify intellect
                ApplyStatMod(STAT_INTELLECT,               val,    apply);
                ApplyPosStatMod(STAT_INTELLECT,            val,    apply);
                //ApplyMaxPowerMod(POWER_MANA,              val*15, apply);
                typestr = "INTELLECT";
                break;
            case ITEM_STAT_SPIRIT:                          //modify spirit
                ApplyStatMod(STAT_SPIRIT,                 val, apply);
                ApplyPosStatMod(STAT_SPIRIT,              val, apply);
                typestr = "SPIRIT";
                break;
            case ITEM_STAT_STAMINA:                         //modify stamina
                ApplyStatMod(STAT_STAMINA                ,val,   apply);
                ApplyPosStatMod(STAT_STAMINA             ,val,   apply);
                //ApplyMaxHealthMod(                        val*10,apply);
                typestr = "STAMINA";
                break;
        }
        if(val > 0)
            sLog.outDebug("%s %s: \t\t%u", applystr.c_str(), typestr.c_str(), val);

    }

    if (proto->Armor)
    {
        ApplyArmorMod( proto->Armor, apply);
        sLog.outDebug("%s Armor: \t\t%u", applystr.c_str(),  proto->Armor);
    }

    if (proto->Block)
    {
        ApplyBlockValueMod(proto->Block, apply);
        sLog.outDebug("%s Block: \t\t%u", applystr.c_str(),  proto->Block);
    }

    if (proto->HolyRes)
    {
        ApplyResistanceMod(SPELL_SCHOOL_HOLY, proto->HolyRes, apply);
        sLog.outDebug("%s HolyRes: \t\t%u", applystr.c_str(),  proto->HolyRes);
    }

    if (proto->FireRes)
    {
        ApplyResistanceMod(SPELL_SCHOOL_FIRE, proto->FireRes, apply);
        sLog.outDebug("%s FireRes: \t\t%u", applystr.c_str(),  proto->FireRes);
    }

    if (proto->NatureRes)
    {
        ApplyResistanceMod(SPELL_SCHOOL_NATURE, proto->NatureRes, apply);
        sLog.outDebug("%s NatureRes: \t\t%u", applystr.c_str(),  proto->NatureRes);
    }

    if (proto->FrostRes)
    {
        ApplyResistanceMod(SPELL_SCHOOL_FROST, proto->FrostRes, apply);
        sLog.outDebug("%s FrostRes: \t\t%u", applystr.c_str(),  proto->FrostRes);
    }

    if (proto->ShadowRes)
    {
        ApplyResistanceMod(SPELL_SCHOOL_SHADOW, proto->ShadowRes, apply);
        sLog.outDebug("%s ShadowRes: \t\t%u", applystr.c_str(),  proto->ShadowRes);
    }

    if (proto->ArcaneRes)
    {
        ApplyResistanceMod(SPELL_SCHOOL_ARCANE, proto->ArcaneRes, apply);
        sLog.outDebug("%s ArcaneRes: \t\t%u", applystr.c_str(),  proto->ArcaneRes);
    }

    uint8 MINDAMAGEFIELD = 0;
    uint8 MAXDAMAGEFIELD = 0;

    if( slot == EQUIPMENT_SLOT_RANGED && ( proto->InventoryType == INVTYPE_RANGED ||
        proto->InventoryType == INVTYPE_THROWN || proto->InventoryType == INVTYPE_RANGEDRIGHT))
    {
        MINDAMAGEFIELD = UNIT_FIELD_MINRANGEDDAMAGE;
        MAXDAMAGEFIELD = UNIT_FIELD_MAXRANGEDDAMAGE;
        typestr = "Ranged";
    }
    else if(slot==EQUIPMENT_SLOT_MAINHAND)
    {
        MINDAMAGEFIELD = UNIT_FIELD_MINDAMAGE;
        MAXDAMAGEFIELD = UNIT_FIELD_MAXDAMAGE;
        typestr = "Mainhand";
    }
    else if(slot==EQUIPMENT_SLOT_OFFHAND)
    {
        MINDAMAGEFIELD = UNIT_FIELD_MINOFFHANDDAMAGE;
        MAXDAMAGEFIELD = UNIT_FIELD_MAXOFFHANDDAMAGE;
        typestr = "Offhand";
    }

    if (proto->Damage[0].DamageMin > 0 && MINDAMAGEFIELD)
    {
        ApplyModFloatValue(MINDAMAGEFIELD, proto->Damage[0].DamageMin, apply);
        sLog.outDetail("%s %s mindam: %f, now is: %f", applystr.c_str(), typestr.c_str(), proto->Damage[0].DamageMin, GetFloatValue(MINDAMAGEFIELD));
    }

    if (proto->Damage[0].DamageMax  > 0 && MAXDAMAGEFIELD)
    {
        ApplyModFloatValue(MAXDAMAGEFIELD, proto->Damage[0].DamageMax, apply);
        sLog.outDetail("%s %s mindam: %f, now is: %f", applystr.c_str(), typestr.c_str(), proto->Damage[0].DamageMax, GetFloatValue(MAXDAMAGEFIELD));
    }

    if (proto->Delay)
    {
        if(slot == EQUIPMENT_SLOT_RANGED)
        {
            SetAttackTime(RANGED_ATTACK, apply ? proto->Delay: 2000);
            typestr = "Range";
            sLog.outDebug("%s %s Delay: \t\t%u", applystr.c_str(), typestr.c_str(), proto->Delay);
        }
        else if(slot==EQUIPMENT_SLOT_MAINHAND)
        {
            SetAttackTime(BASE_ATTACK, apply ? proto->Delay: 2000);
            typestr = "Mainhand";
            sLog.outDebug("%s %s Delay: \t\t%u", applystr.c_str(), typestr.c_str(), proto->Delay);
        }
        else if(slot==EQUIPMENT_SLOT_OFFHAND)
        {
            SetAttackTime(OFF_ATTACK, apply ? proto->Delay: 2000);
            typestr = "Offhand";
            sLog.outDebug("%s %s Delay: \t\t%u", applystr.c_str(), typestr.c_str(), proto->Delay);
        }
    }
}

void Player::CastItemEquipSpell(Item *item)
{
    if(!item) return;

    ItemPrototype const *proto = item->GetProto();

    if(!proto) return;

    SpellEntry *spellInfo;

    for (int i = 0; i < 5; i++)
    {
        if(!proto->Spells[i].SpellId ) continue;
        if(proto->Spells[i].SpellTrigger != ON_EQUIP) continue;

        spellInfo = sSpellStore.LookupEntry(proto->Spells[i].SpellId);
        if(!spellInfo)
        {
            sLog.outError("WORLD: unknown Item spellid %i", proto->Spells[i].SpellId);
            continue;
        }

        DEBUG_LOG("WORLD: cast Item spellId - %i", proto->Spells[i].SpellId);

        Spell spell(this, spellInfo, true, 0);

        SpellCastTargets targets;
        targets.setUnitTarget( this );
        spell.m_CastItem = item;
        spell.prepare(&targets);
    }
}

void Player::CastItemCombatSpell(Item *item,Unit* Target)
{
    if(!item || item->IsBroken())
        return;

    ItemPrototype const *proto = item->GetProto();
    if(!proto)
        return;

    if (!Target || Target == this )
        return;

    SpellEntry *spellInfo;

    for (int i = 0; i < 5; i++)
    {
        if(!proto->Spells[i].SpellId ) continue;

        spellInfo = sSpellStore.LookupEntry(proto->Spells[i].SpellId);
        if(!spellInfo)
        {
            sLog.outError("WORLD: unknown Item spellid %i", proto->Spells[i].SpellId);
            continue;
        }

        if(proto->Spells[i].SpellTrigger != CHANCE_ON_HIT) continue;

        uint32 chance = spellInfo->procChance <= 100 ? spellInfo->procChance : GetWeaponProcChance();
        if (chance > rand_chance())
            this->CastSpell(Target, spellInfo->Id, true, item);
    }

    // item combat enchantments
    for(int e_slot = 0; e_slot < 7; e_slot++)
    {
        uint32 enchant_id = item->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+e_slot*3);
        SpellItemEnchantmentEntry *pEnchant = sSpellItemEnchantmentStore.LookupEntry(enchant_id);
        if(!pEnchant) continue;
        uint32 enchant_display = pEnchant->display_type;
        uint32 chance = pEnchant->value1 != 0 ? pEnchant->value1 : GetWeaponProcChance();
        uint32 enchant_spell_id = pEnchant->spellid;
        SpellEntry *enchantSpell_info = sSpellStore.LookupEntry(enchant_spell_id);
        if(!enchantSpell_info) continue;
        if(enchant_display!=4 && enchant_display!=2 && this->IsItemSpellToCombat(enchantSpell_info))
            if (chance > rand_chance())
                this->CastSpell(Target, enchantSpell_info->Id, true);
    }
}

// only some item spell/auras effects can be executed when item is equiped.
// If not you can have unexpected beaviur. like item giving damage to player when equip.
bool Player::IsItemSpellToEquip(SpellEntry *spellInfo)
{
    return (GetDuration(spellInfo) == -1);                  // infinite duration -> passive aura
    /*
    for(int j = 0; j< 3; j++)
    {
        if(spellInfo->Effect[j] == 6)
        {
            switch(spellInfo->EffectApplyAuraName[j])
            {
                case 3:
                case 23:
                case 8:
                case 84:
                case 85:
                case 42:
                case 43:
                    return false;
            }
        }
    }

    return true;
    */
}

// only some item spell/auras effects can be executed when in combat.
// If not you can have unexpected beaviur. like having stats always growing each attack.
bool Player::IsItemSpellToCombat(SpellEntry *spellInfo)
{
    return (GetDuration(spellInfo) != -1);                  // infinite duration -> passive aura

    /*
    for(int j = 0; j< 3; j++)
    {
        if(spellInfo->Effect[j] == 6)
        {
            switch(spellInfo->EffectApplyAuraName[j])
            {
                case 3:
                case 23:
                case 8:
                case 84:
                case 85:
                case 42:
                case 43:
                    return true;
            }
        }
    }

    return false;
    */
}

void Player::_RemoveAllItemMods()
{
    sLog.outDebug("_RemoveAllItemMods start.");

    for (int i = 0; i < INVENTORY_SLOT_BAG_END; i++)
    {
        if(m_items[i])
        {
            if(m_items[i]->IsBroken())
                continue;

            ItemPrototype const *proto = m_items[i]->GetProto();
            if(!proto)
                continue;

            if(proto->ItemSet)
                RemoveItemsSetItem(this,proto);

            for (int m = 0; m < 5; m++)
            {
                if(proto->Spells[m].SpellId)
                    RemoveAurasDueToSpell(proto->Spells[m].SpellId );
            }

            for(int enchant_slot =  0 ; enchant_slot < 7; enchant_slot++)
            {
                uint32 Enchant_id = m_items[i]->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+enchant_slot*3);
                if(Enchant_id)
                    AddItemEnchant(m_items[i],Enchant_id, false);
            }
        }
    }

    AuraList& mModDamagePercentDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE);
    for(AuraList::iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        (*i)->ApplyModifier(false);

    _RemoveStatsMods();

    AuraList& mModBaseResistancePct = GetAurasByType(SPELL_AURA_MOD_BASE_RESISTANCE_PCT);
    for(AuraList::iterator i = mModBaseResistancePct.begin(); i != mModBaseResistancePct.end(); ++i)
        (*i)->ApplyModifier(false);

    for (int i = 0; i < INVENTORY_SLOT_BAG_END; i++)
    {
        if(m_items[i])
        {
            if(m_items[i]->IsBroken())
                continue;
            ItemPrototype const *proto = m_items[i]->GetProto();
            if(!proto)
                continue;
            _ApplyItemBonuses(proto,i, false);
        }
    }

    for(AuraList::iterator i = mModBaseResistancePct.begin(); i != mModBaseResistancePct.end(); ++i)
        (*i)->ApplyModifier(true);

    _ApplyStatsMods();

    for(AuraList::iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        (*i)->ApplyModifier(true);

    sLog.outDebug("_RemoveAllItemMods complete.");
}

void Player::_ApplyAllItemMods()
{
    sLog.outDebug("_ApplyAllItemMods start.");

    AuraList& mModDamagePercentDone = GetAurasByType(SPELL_AURA_MOD_DAMAGE_PERCENT_DONE);
    for(AuraList::iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        (*i)->ApplyModifier(false);

    _RemoveStatsMods();

    AuraList& mModBaseResistancePct = GetAurasByType(SPELL_AURA_MOD_BASE_RESISTANCE_PCT);
    for(AuraList::iterator i = mModBaseResistancePct.begin(); i != mModBaseResistancePct.end(); ++i)
        (*i)->ApplyModifier(false);

    for (int i = 0; i < INVENTORY_SLOT_BAG_END; i++)
    {
        if(m_items[i])
        {
            if(m_items[i]->IsBroken())
                continue;

            ItemPrototype const *proto = m_items[i]->GetProto();
            if(!proto)
                continue;

            _ApplyItemBonuses(proto,i, true);
        }
    }

    for(AuraList::iterator i = mModBaseResistancePct.begin(); i != mModBaseResistancePct.end(); ++i)
        (*i)->ApplyModifier(true);

    _ApplyStatsMods();

    for(AuraList::iterator i = mModDamagePercentDone.begin(); i != mModDamagePercentDone.end(); ++i)
        (*i)->ApplyModifier(true);

    for (int i = 0; i < INVENTORY_SLOT_BAG_END; i++)
    {
        if(m_items[i])
        {
            if(m_items[i]->IsBroken())
                continue;

            ItemPrototype const *proto = m_items[i]->GetProto();
            if(!proto)
                continue;

            if(proto->ItemSet)
                AddItemsSetItem(this,m_items[i]);

            CastItemEquipSpell(m_items[i]);

            for(int enchant_slot =  0 ; enchant_slot < 7; enchant_slot++)
            {
                uint32 Enchant_id = m_items[i]->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+enchant_slot*3);
                if(Enchant_id)
                    AddItemEnchant(m_items[i],Enchant_id, true);
            }
        }
    }

    sLog.outDebug("_ApplyAllItemMods complete.");
}

/*Loot type MUST be
1-corpse, go
2-skinning
3-Fishing
*/

void Player::SendLoot(uint64 guid, LootType loot_type)
{
    Loot    *loot = NULL;
    PermissionTypes permission = ALL_PERMISSION;

    if (IS_GAMEOBJECT_GUID(guid))
    {
        GameObject *go =
            ObjectAccessor::Instance().GetGameObject(*this, guid);

        if (!go || !go->IsWithinDistInMap(this,OBJECT_ITERACTION_DISTANCE))
            return;

        loot = &go->loot;

        if(go->getLootState() == GO_CLOSED)
        {
            uint32 lootid =  go->lootid;

            if(lootid)
                FillLoot(this,loot,lootid,LootTemplates_Gameobject);

            if(loot_type == LOOT_FISHING)
                go->getFishLoot(loot);

            go->SetLootState(GO_OPEN);
        }
    }
    else if (IS_ITEM_GUID(guid))
    {
        Item *item = this->GetItemByPos( this->GetPosByGuid( guid ));

        if (!item)
            return;

        loot = &item->loot;

        if(!item->m_lootGenerated)
        {
            item->m_lootGenerated = true;
            FillLoot(this,loot,item->GetEntry(),LootTemplates_Item);
        }
    }
    else
    {
        Creature *creature =
            ObjectAccessor::Instance().GetCreature(*this, guid);

        // must be in range and creature must be alive for pickpocket and must be dead for another loot
        if (!creature || creature->isAlive()!=(loot_type == LOOT_PICKPOKETING) || !creature->IsWithinDistInMap(this,OBJECT_ITERACTION_DISTANCE))
            return;

        if(loot_type == LOOT_PICKPOKETING && IsFriendlyTo(creature))
            return;

        loot   = &creature->loot;

        uint32 lootid = creature->GetCreatureInfo()->lootid;

        if(loot_type == LOOT_PICKPOKETING)
        {
            if ( !creature->lootForPickPocketed )
            {
                creature->lootForPickPocketed = true;
                loot->clear();

                if (!creature->HasFlag(UNIT_NPC_FLAGS,UNIT_NPC_FLAG_VENDOR) && lootid)
                    FillLoot(this,loot,lootid,LootTemplates_Pickpocketing);
                // Generate extra money for pick pocket loot
                loot->gold = uint32((10* (rand() % ( (creature->getLevel() / 2) + 1) + rand() % ( (getLevel() / 2) + 1 )))*sWorld.getRate(RATE_DROP_MONEY));
            }
        }
        else
        {
            // the player whose group may loot the corpse
            Player *recipient = creature->GetLootRecipient();
            if (!recipient)
            {
                creature->SetLootRecipient(this);
                recipient = this;
            }

            if (creature->lootForPickPocketed)
            {
                creature->lootForPickPocketed = false;
                loot->clear();
            }

            if(!creature->lootForBody)
            {
                creature->lootForBody = true;
                if (!creature->HasFlag(UNIT_NPC_FLAGS,UNIT_NPC_FLAG_VENDOR) && lootid)
                    FillLoot(this,loot,lootid,LootTemplates_Creature);

                creature->generateMoneyLoot();

                if (recipient->IsInGroup())
                {
                    // round robin style looting applies for all low
                    // quality items in each loot metho except free for all
                    Group *group = objmgr.GetGroupByLeader(recipient->GetGroupLeader());
                    uint32 siz = group->GetMembersCount();
                    uint32 pos = 0;
                    for (pos = 0; pos<siz; pos++)
                        if (group->GetMemberGUID(pos) == group->GetLooterGuid())
                            break;
                    group->SetLooterGuid(group->GetMemberGUID((pos+1)%siz));

                    switch (group->GetLootMethod())
                    {
                        case GROUP_LOOT:
                            // GroupLoot delete items over threshold (threshold even not implemented), and roll them. Items with quality<threshold, round robin
                            group->GroupLoot(recipient->GetGUID(), loot, creature);
                            break;
                        case NEED_BEFORE_GREED:
                            group->NeedBeforeGreed(recipient->GetGUID(), loot, creature);
                            break;
                        default:
                            break;
                    }
                }
            }

            if (loot_type == LOOT_SKINNING)
                FillLoot(this,loot,creature->GetCreatureInfo()->SkinLootId,LootTemplates_Skinning);

            if (!IsInGroup() && recipient == this)
                permission = ALL_PERMISSION;
            else
            {
                if (IsInGroup())
                {
                    Group *group = objmgr.GetGroupByLeader(recipient->GetGroupLeader());
                    if ((GetGroupLeader() == recipient->GetGroupLeader()) && (group->GetLooterGuid() == GetGUID() || loot->released || group->GetLootMethod() == FREE_FOR_ALL))
                        permission = ALL_PERMISSION;
                    else
                    if (GetGroupLeader() == recipient->GetGroupLeader())
                        permission = GROUP_PERMISSION;
                    else
                        permission = NONE_PERMISSION;
                }
                else
                    permission = NONE_PERMISSION;
            }
        }
    }

    m_lootGuid = guid;

    QuestItemList *q_list = NULL;
    if (permission != NONE_PERMISSION)
    {
        QuestItemMap::iterator itr = loot->PlayerQuestItems.find(this);
        if (itr == loot->PlayerQuestItems.end())
            q_list = FillQuestLoot(this, loot);
        else
            q_list = itr->second;
    }

    // LOOT_PICKPOKETING unsupported by client, sending LOOT_SKINNING instead
    if(loot_type == LOOT_PICKPOKETING)
        loot_type = LOOT_SKINNING;

    WorldPacket data;
    data.Initialize (SMSG_LOOT_RESPONSE);

    data << guid;
    data << uint8(loot_type);
    data << LootView(*loot, q_list, permission);

    SendDirectMessage(&data);

    // add 'this' player as one of the players that are looting 'loot'
    if (permission != NONE_PERMISSION)
        loot->AddLooter(this);
}

void Player::SendNotifyLootMoneyRemoved()
{
    WorldPacket data;
    data.Initialize( SMSG_LOOT_CLEAR_MONEY );
    GetSession()->SendPacket( &data );
}

void Player::SendNotifyLootItemRemoved(uint8 lootSlot)
{
    WorldPacket data;
    data.Initialize( SMSG_LOOT_REMOVED );
    data << uint8(lootSlot);
    GetSession()->SendPacket( &data );
}

void Player::SendUpdateWordState(uint16 Field, uint16 Value)
{
    WorldPacket data;
    data.Initialize(SMSG_UPDATE_WORLD_STATE);               //0x2D4
    data << uint32(Field);
    data << uint32(Value);
    GetSession()->SendPacket(&data);
}

void Player::SendInitWorldStates(uint32 MapID)
{
    // TODO Figure out the unknown data.

    if ((MapID == 0) || (MapID == 1))
    {
        sLog.outDebug("Sending SMSG_INIT_WORLD_STATES to Map:%u",MapID);

        uint16 NumberOfFields = 108;
        WorldPacket data;
        data.Initialize (SMSG_INIT_WORLD_STATES);           //0x2C5
        data <<
            (uint32)MapID <<
            (uint16)NumberOfFields <<
        //field (uint16)  value (uint16)
            (uint16)0x07AE<< (uint16)0x01<<
            (uint16)0x0532<< (uint16)0x01<<
            (uint16)0x0531<< (uint16)0x00<<
            (uint16)0x052E<< (uint16)0x00<<
            (uint16)0x06F9<< (uint16)0x00<<
            (uint16)0x06F3<< (uint16)0x00<<
            (uint16)0x06F1<< (uint16)0x00<<
            (uint16)0x06EE<< (uint16)0x00<<
            (uint16)0x06ED<< (uint16)0x00<<
            (uint16)0x0571<< (uint16)0x00<<
            (uint16)0x0570<< (uint16)0x00<<
            (uint16)0x0567<< (uint16)0x01<<
            (uint16)0x0566<< (uint16)0x01<<
            (uint16)0x0550<< (uint16)0x01<<
            (uint16)0x0544<< (uint16)0x00<<
            (uint16)0x0536<< (uint16)0x00<<
            (uint16)0x0535<< (uint16)0x01<<
            (uint16)0x03C6<< (uint16)0x00<<
            (uint16)0x03C4<< (uint16)0x00<<
            (uint16)0x03C2<< (uint16)0x00<<
            (uint16)0x07A8<< (uint16)0x00<<
            (uint16)0x07A3<< (uint16)0x270F<<
            (uint16)0x0574<< (uint16)0x00<<
            (uint16)0x0573<< (uint16)0x00<<
            (uint16)0x0572<< (uint16)0x00<<
            (uint16)0x056F<< (uint16)0x00<<
            (uint16)0x056E<< (uint16)0x00<<
            (uint16)0x056D<< (uint16)0x00<<
            (uint16)0x056C<< (uint16)0x00<<
            (uint16)0x056B<< (uint16)0x00<<
            (uint16)0x056A<< (uint16)0x01<<
            (uint16)0x0569<< (uint16)0x01<<
            (uint16)0x0568<< (uint16)0x01<<
            (uint16)0x0565<< (uint16)0x00<<
            (uint16)0x0564<< (uint16)0x00<<
            (uint16)0x0563<< (uint16)0x00<<
            (uint16)0x0562<< (uint16)0x00<<
            (uint16)0x0561<< (uint16)0x00<<
            (uint16)0x0560<< (uint16)0x00<<
            (uint16)0x055F<< (uint16)0x00<<
            (uint16)0x055E<< (uint16)0x00<<
            (uint16)0x055D<< (uint16)0x00<<
            (uint16)0x055C<< (uint16)0x00<<
            (uint16)0x055B<< (uint16)0x00<<
            (uint16)0x055A<< (uint16)0x00<<
            (uint16)0x0559<< (uint16)0x00<<
            (uint16)0x0558<< (uint16)0x00<<
            (uint16)0x0557<< (uint16)0x00<<
            (uint16)0x0556<< (uint16)0x00<<
            (uint16)0x0555<< (uint16)0x00<<
            (uint16)0x0554<< (uint16)0x01<<
            (uint16)0x0553<< (uint16)0x01<<
            (uint16)0x0552<< (uint16)0x01<<
            (uint16)0x0551<< (uint16)0x01<<
            (uint16)0x054F<< (uint16)0x00<<
            (uint16)0x054E<< (uint16)0x00<<
            (uint16)0x054D<< (uint16)0x01<<
            (uint16)0x054C<< (uint16)0x00<<
            (uint16)0x054B<< (uint16)0x00<<
            (uint16)0x0545<< (uint16)0x00<<
            (uint16)0x0543<< (uint16)0x01<<
            (uint16)0x0542<< (uint16)0x00<<
            (uint16)0x0540<< (uint16)0x00<<
            (uint16)0x053F<< (uint16)0x00<<
            (uint16)0x053E<< (uint16)0x00<<
            (uint16)0x053D<< (uint16)0x00<<
            (uint16)0x053C<< (uint16)0x00<<
            (uint16)0x053B<< (uint16)0x00<<
            (uint16)0x053A<< (uint16)0x01<<
            (uint16)0x0539<< (uint16)0x00<<
            (uint16)0x0538<< (uint16)0x00<<
            (uint16)0x0537<< (uint16)0x00<<
            (uint16)0x0534<< (uint16)0x00<<
            (uint16)0x0533<< (uint16)0x00<<
            (uint16)0x0530<< (uint16)0x00<<
            (uint16)0x052F<< (uint16)0x00<<
            (uint16)0x052D<< (uint16)0x01<<
            (uint16)0x0516<< (uint16)0x01<<
            (uint16)0x0515<< (uint16)0x00<<
            (uint16)0x03B6<< (uint16)0x00<<
            (uint16)0x0745<< (uint16)0x02<<
            (uint16)0x0736<< (uint16)0x01<<
            (uint16)0x0735<< (uint16)0x01<<
            (uint16)0x0734<< (uint16)0x01<<
            (uint16)0x0733<< (uint16)0x01<<
            (uint16)0x0732<< (uint16)0x01<<
            (uint16)0x0702<< (uint16)0x00<<
            (uint16)0x0701<< (uint16)0x00<<
            (uint16)0x0700<< (uint16)0x00<<
            (uint16)0x06FE<< (uint16)0x00<<
            (uint16)0x06FD<< (uint16)0x00<<
            (uint16)0x06FC<< (uint16)0x00<<
            (uint16)0x06FB<< (uint16)0x00<<
            (uint16)0x06F8<< (uint16)0x00<<
            (uint16)0x06F7<< (uint16)0x00<<
            (uint16)0x06F6<< (uint16)0x00<<
            (uint16)0x06F4<< (uint16)0x7D0<<
            (uint16)0x06F2<< (uint16)0x00<<
            (uint16)0x06F0<< (uint16)0x00<<
            (uint16)0x06EF<< (uint16)0x00<<
            (uint16)0x06EC<< (uint16)0x00<<
            (uint16)0x06EA<< (uint16)0x00<<
            (uint16)0x06E9<< (uint16)0x00<<
            (uint16)0x06E8<< (uint16)0x00<<
            (uint16)0x06E7<< (uint16)0x00<<
            (uint16)0x0518<< (uint16)0x00<<
            (uint16)0x0517<< (uint16)0x00<<
            (uint16)0x0703<< (uint16)0x00;
        GetSession()->SendPacket(&data);
    }

    //BattleGround currently only map 489
    else if (MapID == 489)                                  // && and guid is in a current Battlefield)
    {
        sLog.outDebug("Sending SMSG_INIT_WORLD_STATES to Map:%u",MapID);

        uint16 NumberOfFields = 114;
        WorldPacket data;
        data.Initialize (SMSG_INIT_WORLD_STATES);
        data <<

            (uint32)MapID<<
            (uint16)NumberOfFields <<
        //field (uint16)  value (uint16)
            (uint16)0x07AE<< (uint16)0x01<<
            (uint16)0x0532<< (uint16)0x01<<
            (uint16)0x0531<< (uint16)0x00<<
            (uint16)0x052E<< (uint16)0x00<<
            (uint16)0x06F9<< (uint16)0x00<<
            (uint16)0x06F3<< (uint16)0x00<<
            (uint16)0x06F1<< (uint16)0x00<<
            (uint16)0x06EE<< (uint16)0x00<<
            (uint16)0x06ED<< (uint16)0x00<<
            (uint16)0x0571<< (uint16)0x00<<
            (uint16)0x0570<< (uint16)0x00<<
            (uint16)0x0567<< (uint16)0x01<<
            (uint16)0x0566<< (uint16)0x01<<
            (uint16)0x0550<< (uint16)0x01<<
            (uint16)0x0544<< (uint16)0x00<<
            (uint16)0x0536<< (uint16)0x00<<
            (uint16)0x0535<< (uint16)0x01<<
            (uint16)0x03C6<< (uint16)0x00<<
            (uint16)0x03C4<< (uint16)0x00<<
            (uint16)0x03C2<< (uint16)0x00<<
            (uint16)0x07A8<< (uint16)0x00<<
            (uint16)0x07A3<< (uint16)0x270F <<
            (uint16)0x060B<< (uint16)0x02<<
            (uint16)0x0574<< (uint16)0x00<<
            (uint16)0x0573<< (uint16)0x00<<
            (uint16)0x0572<< (uint16)0x00<<
            (uint16)0x056F<< (uint16)0x00<<
            (uint16)0x056E<< (uint16)0x00<<
            (uint16)0x056D<< (uint16)0x00<<
            (uint16)0x056C<< (uint16)0x00<<
            (uint16)0x056B<< (uint16)0x00<<
            (uint16)0x056A<< (uint16)0x01<<
            (uint16)0x0569<< (uint16)0x01<<
            (uint16)0x0568<< (uint16)0x01<<
            (uint16)0x0565<< (uint16)0x00<<
            (uint16)0x0564<< (uint16)0x00<<
            (uint16)0x0563<< (uint16)0x00<<
            (uint16)0x0562<< (uint16)0x00<<
            (uint16)0x0561<< (uint16)0x00<<
            (uint16)0x0560<< (uint16)0x00<<
            (uint16)0x055F<< (uint16)0x00<<
            (uint16)0x055E<< (uint16)0x00<<
            (uint16)0x055D<< (uint16)0x00<<
            (uint16)0x055C<< (uint16)0x00<<
            (uint16)0x055B<< (uint16)0x00<<
            (uint16)0x055A<< (uint16)0x00<<
            (uint16)0x0559<< (uint16)0x00<<
            (uint16)0x0558<< (uint16)0x00<<
            (uint16)0x0557<< (uint16)0x00<<
            (uint16)0x0556<< (uint16)0x00<<
            (uint16)0x0555<< (uint16)0x00<<
            (uint16)0x0554<< (uint16)0x01<<
            (uint16)0x0553<< (uint16)0x01<<
            (uint16)0x0552<< (uint16)0x01<<
            (uint16)0x0551<< (uint16)0x01<<
            (uint16)0x054F<< (uint16)0x00<<
            (uint16)0x054E<< (uint16)0x00<<
            (uint16)0x054D<< (uint16)0x01<<
            (uint16)0x054C<< (uint16)0x00<<
            (uint16)0x054B<< (uint16)0x00<<
            (uint16)0x0545<< (uint16)0x00<<
            (uint16)0x0543<< (uint16)0x01<<
            (uint16)0x0542<< (uint16)0x00<<
            (uint16)0x0540<< (uint16)0x00<<
            (uint16)0x053F<< (uint16)0x00<<
            (uint16)0x053E<< (uint16)0x00<<
            (uint16)0x053D<< (uint16)0x00<<
            (uint16)0x053C<< (uint16)0x00<<
            (uint16)0x053B<< (uint16)0x00<<
            (uint16)0x053A<< (uint16)0x01<<
            (uint16)0x0539<< (uint16)0x00<<
            (uint16)0x0538<< (uint16)0x00<<
            (uint16)0x0537<< (uint16)0x00<<
            (uint16)0x0534<< (uint16)0x00<<
            (uint16)0x0533<< (uint16)0x00<<
            (uint16)0x0530<< (uint16)0x00<<
            (uint16)0x052F<< (uint16)0x00<<
            (uint16)0x052D<< (uint16)0x01<<
            (uint16)0x0516<< (uint16)0x01<<
            (uint16)0x0515<< (uint16)0x00<<
            (uint16)0x03B6<< (uint16)0x00<<
            (uint16)0x0745<< (uint16)0x02<<
            (uint16)0x0736<< (uint16)0x01<<
            (uint16)0x0735<< (uint16)0x01<<
            (uint16)0x0734<< (uint16)0x01<<
            (uint16)0x0733<< (uint16)0x01<<
            (uint16)0x0732<< (uint16)0x01<<
            (uint16)0x0702<< (uint16)0x00<<
            (uint16)0x0701<< (uint16)0x00<<
            (uint16)0x0700<< (uint16)0x00<<
            (uint16)0x06FE<< (uint16)0x00<<
            (uint16)0x06FD<< (uint16)0x00<<
            (uint16)0x06FC<< (uint16)0x00<<
            (uint16)0x06FB<< (uint16)0x00<<
            (uint16)0x06F8<< (uint16)0x00<<
            (uint16)0x06F7<< (uint16)0x00<<
            (uint16)0x06F6<< (uint16)0x00<<
            (uint16)0x06F4<< (uint16)0x07D0 <<
            (uint16)0x06F2<< (uint16)0x00<<
            (uint16)0x06F0<< (uint16)0x00<<
            (uint16)0x06EF<< (uint16)0x00<<
            (uint16)0x06EC<< (uint16)0x00<<
            (uint16)0x06EA<< (uint16)0x00<<
            (uint16)0x06E9<< (uint16)0x00<<
            (uint16)0x06E8<< (uint16)0x00<<
            (uint16)0x06E7<< (uint16)0x00<<
            (uint16)0x0641<< (uint16)0x03<<
            (uint16)0x062E<< (uint16)0x00<<
            (uint16)0x062D<< (uint16)0x00<<
            (uint16)0x060A<< (uint16)0x00<<
            (uint16)0x0609<< (uint16)0x00<<
            (uint16)0x0518<< (uint16)0x00<<
            (uint16)0x0517<< (uint16)0x00<<
            (uint16)0x0703<< (uint16)0x00;
        GetSession()->SendPacket(&data);
    }
}

void Player::AddWeather()
{
    uint32 zoneid = GetZoneId();
    if(!sWorld.FindWeather(zoneid))
    {
        Weather *wth = new Weather(this);
        sWorld.AddWeather(wth);
    }
}

uint32 Player::GetXPRestBonus(uint32 xp)
{
    float rested_xp = 2 * GetRestBonus();                   //xp for each rested bonus

    float rest_xp_percent = rested_xp / ((float)xp / 100);  //% rest bonuse from total rest bonus
    if(rest_xp_percent>100)rest_xp_percent=100;

    sLog.outDetail("XP_GAIN: %f, rest_xp_percent=%f",(float)xp,rest_xp_percent);

    rested_xp    = ((float)xp / 100 * rest_xp_percent);

    SetRestBonus( GetRestBonus() - ( (float)(xp + rested_xp) / 2 ));

    sLog.outDetail("Player gain %u xp (+ %u Rested Bonus). Rested bonus=%f",xp+(uint32)rested_xp,(uint32)rested_xp,GetRestBonus());
    return (uint32)rested_xp;
}

uint8 Player::CheckFishingAble() const
{
    uint32 zone = GetZoneId();
    uint32 fish_value = GetSkillValue(SKILL_FISHING);
    uint32 ZoneMaxSkill;
    switch(zone)
    {
        case 1:
            ZoneMaxSkill=50;
            break;
        case 2:
            ZoneMaxSkill=100;
            break;
        case 8:
            ZoneMaxSkill=225;
            break;
        case 9:
            ZoneMaxSkill=50;
            break;
        case 10:
            ZoneMaxSkill=50;
            break;
        case 11:
            ZoneMaxSkill=150;
            break;
        case 12:
            ZoneMaxSkill=50;
            break;
        case 14:
            ZoneMaxSkill=50;
            break;
        case 15:
            ZoneMaxSkill=225;
            break;
        case 16:
            ZoneMaxSkill=275;
            break;
        case 17:
            ZoneMaxSkill=275;
            break;
        case 18:
            ZoneMaxSkill=50;
            break;
        case 28:
            ZoneMaxSkill=290;
            break;
        case 33:
            ZoneMaxSkill=225;
            break;
        case 35:
            ZoneMaxSkill=225;
            break;
        case 37:
            ZoneMaxSkill=225;
            break;
        case 38:
            ZoneMaxSkill=100;
            break;
        case 40:
            ZoneMaxSkill=100;
            break;
        case 43:
            ZoneMaxSkill=225;
            break;
        case 44:
            ZoneMaxSkill=125;
            break;
        case 45:
            ZoneMaxSkill=200;
            break;
        case 47:
            ZoneMaxSkill=250;
            break;
        case 55:
            ZoneMaxSkill=200;
            break;
        case 57:
            ZoneMaxSkill=50;
            break;
        case 60:
            ZoneMaxSkill=50;
            break;
        case 61:
            ZoneMaxSkill=50;
            break;
        case 62:
            ZoneMaxSkill=50;
            break;
        case 63:
            ZoneMaxSkill=50;
            break;
        case 64:
            ZoneMaxSkill=50;
            break;
        case 68:
            ZoneMaxSkill=150;
            break;
        case 69:
            ZoneMaxSkill=125;
            break;
        case 71:
            ZoneMaxSkill=225;
            break;
        case 74:
            ZoneMaxSkill=225;
            break;
        case 75:
            ZoneMaxSkill=225;
            break;
        case 76:
            ZoneMaxSkill=225;
            break;
        case 85:
            ZoneMaxSkill=50;
            break;
        case 86:
            ZoneMaxSkill=50;
            break;
        case 87:
            ZoneMaxSkill=50;
            break;
        case 88:
            ZoneMaxSkill=50;
            break;
        case 89:
            ZoneMaxSkill=50;
            break;
        case 92:
            ZoneMaxSkill=50;
            break;
        case 100:
            ZoneMaxSkill=225;
            break;
        case 102:
            ZoneMaxSkill=225;
            break;
        case 104:
            ZoneMaxSkill=225;
            break;
        case 115:
            ZoneMaxSkill=100;
            break;
        case 116:
            ZoneMaxSkill=225;
            break;
        case 117:
            ZoneMaxSkill=225;
            break;
        case 122:
            ZoneMaxSkill=225;
            break;
        case 129:
            ZoneMaxSkill=225;
            break;
        case 130:
            ZoneMaxSkill=100;
            break;
        case 139:
            ZoneMaxSkill=300;
            break;
        case 141:
            ZoneMaxSkill=50;
            break;
        case 146:
            ZoneMaxSkill=50;
            break;
        case 150:
            ZoneMaxSkill=150;
            break;
        case 162:
            ZoneMaxSkill=50;
            break;
        case 163:
            ZoneMaxSkill=50;
            break;
        case 168:
            ZoneMaxSkill=50;
            break;
        case 169:
            ZoneMaxSkill=50;
            break;
        case 172:
            ZoneMaxSkill=100;
            break;
        case 187:
            ZoneMaxSkill=50;
            break;
        case 188:
            ZoneMaxSkill=50;
            break;
        case 193:
            ZoneMaxSkill=290;
            break;
        case 202:
            ZoneMaxSkill=290;
            break;
        case 211:
            ZoneMaxSkill=50;
            break;
        case 221:
            ZoneMaxSkill=50;
            break;
        case 223:
            ZoneMaxSkill=50;
            break;
        case 226:
            ZoneMaxSkill=100;
            break;
        case 227:
            ZoneMaxSkill=100;
            break;
        case 237:
            ZoneMaxSkill=100;
            break;
        case 249:
            ZoneMaxSkill=280;
            break;
        case 256:
            ZoneMaxSkill=50;
            break;
        case 258:
            ZoneMaxSkill=50;
            break;
        case 259:
            ZoneMaxSkill=50;
            break;
        case 265:
            ZoneMaxSkill=50;
            break;
        case 266:
            ZoneMaxSkill=50;
            break;
        case 267:
            ZoneMaxSkill=150;
            break;
        case 271:
            ZoneMaxSkill=150;
            break;
        case 272:
            ZoneMaxSkill=150;
            break;
        case 279:
            ZoneMaxSkill=200;
            break;
        case 284:
            ZoneMaxSkill=200;
            break;
        case 295:
            ZoneMaxSkill=150;
            break;
        case 297:
            ZoneMaxSkill=225;
            break;
        case 298:
            ZoneMaxSkill=150;
            break;
        case 299:
            ZoneMaxSkill=150;
            break;
        case 300:
            ZoneMaxSkill=225;
            break;
        case 301:
            ZoneMaxSkill=225;
            break;
        case 302:
            ZoneMaxSkill=225;
            break;
        case 305:
            ZoneMaxSkill=100;
            break;
        case 306:
            ZoneMaxSkill=100;
            break;
        case 307:
            ZoneMaxSkill=250;
            break;
        case 309:
            ZoneMaxSkill=100;
            break;
        case 310:
            ZoneMaxSkill=225;
            break;
        case 311:
            ZoneMaxSkill=225;
            break;
        case 312:
            ZoneMaxSkill=225;
            break;
        case 314:
            ZoneMaxSkill=200;
            break;
        case 317:
            ZoneMaxSkill=200;
            break;
        case 323:
            ZoneMaxSkill=100;
            break;
        case 324:
            ZoneMaxSkill=200;
            break;
        case 327:
            ZoneMaxSkill=200;
            break;
        case 328:
            ZoneMaxSkill=200;
            break;
        case 331:
            ZoneMaxSkill=150;
            break;
        case 350:
            ZoneMaxSkill=250;
            break;
        case 351:
            ZoneMaxSkill=250;
            break;
        case 353:
            ZoneMaxSkill=250;
            break;
        case 356:
            ZoneMaxSkill=250;
            break;
        case 361:
            ZoneMaxSkill=250;
            break;
        case 363:
            ZoneMaxSkill=50;
            break;
        case 367:
            ZoneMaxSkill=50;
            break;
        case 368:
            ZoneMaxSkill=50;
            break;
        case 373:
            ZoneMaxSkill=50;
            break;
        case 374:
            ZoneMaxSkill=50;
            break;
        case 375:
            ZoneMaxSkill=300;
            break;
        case 382:
            ZoneMaxSkill=125;
            break;
        case 384:
            ZoneMaxSkill=125;
            break;
        case 385:
            ZoneMaxSkill=125;
            break;
        case 386:
            ZoneMaxSkill=125;
            break;
        case 387:
            ZoneMaxSkill=125;
            break;
        case 388:
            ZoneMaxSkill=125;
            break;
        case 391:
            ZoneMaxSkill=125;
            break;
        case 392:
            ZoneMaxSkill=125;
            break;
        case 393:
            ZoneMaxSkill=50;
            break;
        case 401:
            ZoneMaxSkill=125;
            break;
        case 405:
            ZoneMaxSkill=200;
            break;
        case 406:
            ZoneMaxSkill=135;
            break;
        case 414:
            ZoneMaxSkill=150;
            break;
        case 415:
            ZoneMaxSkill=150;
            break;
        case 416:
            ZoneMaxSkill=150;
            break;
        case 418:
            ZoneMaxSkill=150;
            break;
        case 420:
            ZoneMaxSkill=150;
            break;
        case 421:
            ZoneMaxSkill=150;
            break;
        case 422:
            ZoneMaxSkill=150;
            break;
        case 424:
            ZoneMaxSkill=150;
            break;
        case 429:
            ZoneMaxSkill=150;
            break;
        case 433:
            ZoneMaxSkill=150;
            break;
        case 434:
            ZoneMaxSkill=150;
            break;
        case 437:
            ZoneMaxSkill=150;
            break;
        case 441:
            ZoneMaxSkill=150;
            break;
        case 442:
            ZoneMaxSkill=100;
            break;
        case 443:
            ZoneMaxSkill=100;
            break;
        case 445:
            ZoneMaxSkill=100;
            break;
        case 448:
            ZoneMaxSkill=100;
            break;
        case 449:
            ZoneMaxSkill=100;
            break;
        case 452:
            ZoneMaxSkill=100;
            break;
        case 453:
            ZoneMaxSkill=100;
            break;
        case 454:
            ZoneMaxSkill=100;
            break;
        case 456:
            ZoneMaxSkill=100;
            break;
        case 460:
            ZoneMaxSkill=135;
            break;
        case 463:
            ZoneMaxSkill=275;
            break;
        case 464:
            ZoneMaxSkill=135;
            break;
        case 478:
            ZoneMaxSkill=50;
            break;
        case 490:
            ZoneMaxSkill=275;
            break;
        case 493:
            ZoneMaxSkill=300;
            break;
        case 496:
            ZoneMaxSkill=225;
            break;
        case 497:
            ZoneMaxSkill=225;
            break;
        case 501:
            ZoneMaxSkill=225;
            break;
        case 502:
            ZoneMaxSkill=225;
            break;
        case 504:
            ZoneMaxSkill=225;
            break;
        case 508:
            ZoneMaxSkill=225;
            break;
        case 509:
            ZoneMaxSkill=225;
            break;
        case 510:
            ZoneMaxSkill=225;
            break;
        case 511:
            ZoneMaxSkill=225;
            break;
        case 513:
            ZoneMaxSkill=225;
            break;
        case 516:
            ZoneMaxSkill=225;
            break;
        case 517:
            ZoneMaxSkill=225;
            break;
        case 518:
            ZoneMaxSkill=200;
            break;
        case 537:
            ZoneMaxSkill=250;
            break;
        case 538:
            ZoneMaxSkill=250;
            break;
        case 542:
            ZoneMaxSkill=250;
            break;
        case 543:
            ZoneMaxSkill=250;
            break;
        case 556:
            ZoneMaxSkill=50;
            break;
        case 576:
            ZoneMaxSkill=150;
            break;
        case 598:
            ZoneMaxSkill=200;
            break;
        case 602:
            ZoneMaxSkill=200;
            break;
        case 604:
            ZoneMaxSkill=200;
            break;
        case 618:
            ZoneMaxSkill=300;
            break;
        case 636:
            ZoneMaxSkill=135;
            break;
        case 656:
            ZoneMaxSkill=300;
            break;
        case 657:
            ZoneMaxSkill=225;
            break;
        case 702:
            ZoneMaxSkill=50;
            break;
        case 719:
            ZoneMaxSkill=135;
            break;
        case 720:
            ZoneMaxSkill=135;
            break;
        case 797:
            ZoneMaxSkill=225;
            break;
        case 799:
            ZoneMaxSkill=150;
            break;
        case 810:
            ZoneMaxSkill=50;
            break;
        case 814:
            ZoneMaxSkill=50;
            break;
        case 815:
            ZoneMaxSkill=125;
            break;
        case 818:
            ZoneMaxSkill=50;
            break;
        case 878:
            ZoneMaxSkill=275;
            break;
        case 879:
            ZoneMaxSkill=150;
            break;
        case 896:
            ZoneMaxSkill=150;
            break;
        case 917:
            ZoneMaxSkill=100;
            break;
        case 919:
            ZoneMaxSkill=100;
            break;
        case 922:
            ZoneMaxSkill=100;
            break;
        case 923:
            ZoneMaxSkill=50;
            break;
        case 927:
            ZoneMaxSkill=50;
            break;
        case 968:
            ZoneMaxSkill=250;
            break;
        case 977:
            ZoneMaxSkill=250;
            break;
        case 978:
            ZoneMaxSkill=250;
            break;
        case 979:
            ZoneMaxSkill=250;
            break;
        case 983:
            ZoneMaxSkill=250;
            break;
        case 988:
            ZoneMaxSkill=250;
            break;
        case 997:
            ZoneMaxSkill=125;
            break;
        case 998:
            ZoneMaxSkill=125;
            break;
        case 1001:
            ZoneMaxSkill=125;
            break;
        case 1002:
            ZoneMaxSkill=125;
            break;
        case 1008:
            ZoneMaxSkill=250;
            break;
        case 1017:
            ZoneMaxSkill=150;
            break;
        case 1018:
            ZoneMaxSkill=150;
            break;
        case 1020:
            ZoneMaxSkill=150;
            break;
        case 1021:
            ZoneMaxSkill=150;
            break;
        case 1022:
            ZoneMaxSkill=150;
            break;
        case 1023:
            ZoneMaxSkill=150;
            break;
        case 1024:
            ZoneMaxSkill=150;
            break;
        case 1025:
            ZoneMaxSkill=150;
            break;
        case 1039:
            ZoneMaxSkill=150;
            break;
        case 1056:
            ZoneMaxSkill=290;
            break;
        case 1097:
            ZoneMaxSkill=150;
            break;
        case 1099:
            ZoneMaxSkill=300;
            break;
        case 1101:
            ZoneMaxSkill=250;
            break;
        case 1102:
            ZoneMaxSkill=250;
            break;
        case 1106:
            ZoneMaxSkill=250;
            break;
        case 1112:
            ZoneMaxSkill=250;
            break;
        case 1116:
            ZoneMaxSkill=250;
            break;
        case 1117:
            ZoneMaxSkill=250;
            break;
        case 1119:
            ZoneMaxSkill=250;
            break;
        case 1120:
            ZoneMaxSkill=250;
            break;
        case 1121:
            ZoneMaxSkill=250;
            break;
        case 1126:
            ZoneMaxSkill=225;
            break;
        case 1136:
            ZoneMaxSkill=250;
            break;
        case 1156:
            ZoneMaxSkill=225;
            break;
        case 1176:
            ZoneMaxSkill=250;
            break;
        case 1222:
            ZoneMaxSkill=275;
            break;
        case 1227:
            ZoneMaxSkill=275;
            break;
        case 1228:
            ZoneMaxSkill=275;
            break;
        case 1229:
            ZoneMaxSkill=275;
            break;
        case 1230:
            ZoneMaxSkill=275;
            break;
        case 1231:
            ZoneMaxSkill=275;
            break;
        case 1234:
            ZoneMaxSkill=275;
            break;
        case 1256:
            ZoneMaxSkill=275;
            break;
        case 1296:
            ZoneMaxSkill=50;
            break;
        case 1297:
            ZoneMaxSkill=50;
            break;
        case 1336:
            ZoneMaxSkill=250;
            break;
        case 1337:
            ZoneMaxSkill=250;
            break;
        case 1338:
            ZoneMaxSkill=100;
            break;
        case 1339:
            ZoneMaxSkill=200;
            break;
        case 1477:
            ZoneMaxSkill=275;
            break;
        case 1519:
            ZoneMaxSkill=50;
            break;
        case 1557:
            ZoneMaxSkill=175;
            break;
        case 1577:
            ZoneMaxSkill=225;
            break;
        case 1578:
            ZoneMaxSkill=225;
            break;
        case 1581:
            ZoneMaxSkill=100;
            break;
        case 1617:
            ZoneMaxSkill=50;
            break;
        case 1638:
            ZoneMaxSkill=50;
            break;
        case 1662:
            ZoneMaxSkill=50;
            break;
        case 1681:
            ZoneMaxSkill=200;
            break;
        case 1682:
            ZoneMaxSkill=200;
            break;
        case 1684:
            ZoneMaxSkill=200;
            break;
        case 1701:
            ZoneMaxSkill=125;
            break;
        case 1738:
            ZoneMaxSkill=225;
            break;
        case 1739:
            ZoneMaxSkill=225;
            break;
        case 1740:
            ZoneMaxSkill=225;
            break;
        case 1760:
            ZoneMaxSkill=225;
            break;
        case 1762:
            ZoneMaxSkill=250;
            break;
        case 1764:
            ZoneMaxSkill=225;
            break;
        case 1765:
            ZoneMaxSkill=225;
            break;
        case 1767:
            ZoneMaxSkill=275;
            break;
        case 1770:
            ZoneMaxSkill=275;
            break;
        case 1777:
            ZoneMaxSkill=225;
            break;
        case 1778:
            ZoneMaxSkill=225;
            break;
        case 1780:
            ZoneMaxSkill=225;
            break;
        case 1797:
            ZoneMaxSkill=225;
            break;
        case 1798:
            ZoneMaxSkill=225;
            break;
        case 1883:
            ZoneMaxSkill=250;
            break;
        case 1884:
            ZoneMaxSkill=250;
            break;
        case 1939:
            ZoneMaxSkill=250;
            break;
        case 1940:
            ZoneMaxSkill=250;
            break;
        case 1942:
            ZoneMaxSkill=250;
            break;
        case 1977:
            ZoneMaxSkill=225;
            break;
        case 1997:
            ZoneMaxSkill=275;
            break;
        case 1998:
            ZoneMaxSkill=275;
            break;
        case 2017:
            ZoneMaxSkill=300;
            break;
        case 2077:
            ZoneMaxSkill=100;
            break;
        case 2078:
            ZoneMaxSkill=100;
            break;
        case 2079:
            ZoneMaxSkill=225;
            break;
        case 2097:
            ZoneMaxSkill=175;
            break;
        case 2100:
            ZoneMaxSkill=245;
            break;
        case 2158:
            ZoneMaxSkill=250;
            break;
        case 2246:
            ZoneMaxSkill=300;
            break;
        case 2256:
            ZoneMaxSkill=300;
            break;
        case 2270:
            ZoneMaxSkill=300;
            break;
        case 2272:
            ZoneMaxSkill=300;
            break;
        case 2277:
            ZoneMaxSkill=300;
            break;
        case 2279:
            ZoneMaxSkill=300;
            break;
        case 2298:
            ZoneMaxSkill=300;
            break;
        case 2302:
            ZoneMaxSkill=225;
            break;
        case 2317:
            ZoneMaxSkill=250;
            break;
        case 2318:
            ZoneMaxSkill=225;
            break;
        case 2321:
            ZoneMaxSkill=275;
            break;
        case 2322:
            ZoneMaxSkill=50;
            break;
        case 2323:
            ZoneMaxSkill=250;
            break;
        case 2324:
            ZoneMaxSkill=200;
            break;
        case 2325:
            ZoneMaxSkill=150;
            break;
        case 2326:
            ZoneMaxSkill=100;
            break;
        case 2364:
            ZoneMaxSkill=100;
            break;
        case 2365:
            ZoneMaxSkill=150;
            break;
        case 2398:
            ZoneMaxSkill=100;
            break;
        case 2399:
            ZoneMaxSkill=50;
            break;
        case 2400:
            ZoneMaxSkill=250;
            break;
        case 2401:
            ZoneMaxSkill=200;
            break;
        case 2402:
            ZoneMaxSkill=100;
            break;
        case 2403:
            ZoneMaxSkill=225;
            break;
        case 2405:
            ZoneMaxSkill=200;
            break;
        case 2408:
            ZoneMaxSkill=200;
            break;
        case 2457:
            ZoneMaxSkill=150;
            break;
        case 2477:
            ZoneMaxSkill=300;
            break;
        case 2481:
            ZoneMaxSkill=275;
            break;
        case 2521:
            ZoneMaxSkill=250;
            break;
        case 2522:
            ZoneMaxSkill=250;
            break;
        case 2558:
            ZoneMaxSkill=300;
            break;
        case 2562:
            ZoneMaxSkill=300;
            break;
        case 2597:
            ZoneMaxSkill=300;
            break;
        case 2618:
            ZoneMaxSkill=275;
            break;
        case 2619:
            ZoneMaxSkill=300;
            break;
        case 2620:
            ZoneMaxSkill=290;
            break;
        case 2624:
            ZoneMaxSkill=300;
            break;
        case 2631:
            ZoneMaxSkill=300;
            break;
        case 2797:
            ZoneMaxSkill=150;
            break;
        case 2837:
            ZoneMaxSkill=300;
            break;
        case 2897:
            ZoneMaxSkill=150;
            break;
        default:
            ZoneMaxSkill=50;
            break;
    }
    if((ZoneMaxSkill-50) > fish_value )
        return 0;
    else if(ZoneMaxSkill-50 <= fish_value && fish_value < ZoneMaxSkill-25)
        return 1;
    else if(ZoneMaxSkill-25 <= fish_value && fish_value < ZoneMaxSkill)
        return 2;
    else if(ZoneMaxSkill <= fish_value && fish_value < ZoneMaxSkill + 25)
        return 3;
    else return 4;
}

void Player::SetBindPoint(uint64 guid)
{
    WorldPacket data;
    data.Initialize( SMSG_BINDER_CONFIRM );
    data << guid;
    GetSession()->SendPacket( &data );
}

void Player::SendTalentWipeConfirm(uint64 guid)
{
    WorldPacket data;
    data.Initialize( MSG_TALENT_WIPE_CONFIRM );
    data << guid;
    data << (uint32)resetTalentsCost();
    GetSession()->SendPacket( &data );
}

/*********************************************************/
/***                    STORAGE SYSTEM                 ***/
/*********************************************************/

void Player::SetVirtualItemSlot( uint8 i, Item* item)
{
    assert(i < 3);
    SetUInt32Value(UNIT_VIRTUAL_ITEM_INFO + 2*i,    item ? item->GetGUIDLow()              : 0);
    SetUInt32Value(UNIT_VIRTUAL_ITEM_INFO + 2*i +1, item ? item->GetProto()->Sheath        : 0);
    SetUInt32Value(UNIT_VIRTUAL_ITEM_SLOT_DISPLAY+i,item ? item->GetProto()->DisplayInfoID : 0);
    if(i < 2 && item)
    {
        if(!item->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+3))
            return;
        uint32 charges = item->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+3+2);
        if(charges == 0)
            return;
        if(charges > 1)
            item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT+3+2,charges-1);
        else if(charges <= 1)
        {
            AddItemEnchant(item,item->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+3),false);
            for(int y=0;y<3;y++)
                item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT+3+y,0);
        }
    }
}

void Player::SetSheath( uint32 sheathed )
{
    Item* item;
    switch (sheathed)
    {
        case 0:                                             // no prepeared weapon
            SetVirtualItemSlot(0,NULL);
            SetVirtualItemSlot(1,NULL);
            SetVirtualItemSlot(2,NULL);
            break;
        case 1:                                             // prepeared melee weapon
        {
            item = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND);
            SetVirtualItemSlot(0,item && !item->IsBroken() ? item : NULL);
            item = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND);
            SetVirtualItemSlot(1,item && !item->IsBroken() ? item : NULL);
            SetVirtualItemSlot(2,NULL);
        };  break;
        case 2:                                             // prepeared ranged weapon
            SetVirtualItemSlot(0,NULL);
            SetVirtualItemSlot(1,NULL);
            item = GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED);
            SetVirtualItemSlot(2,item && !item->IsBroken() ? item : NULL);
            break;
        default:
            SetVirtualItemSlot(0,NULL);
            SetVirtualItemSlot(1,NULL);
            SetVirtualItemSlot(2,NULL);
            break;
    }
}

uint8 Player::FindEquipSlot( uint32 type, uint32 slot, bool swap ) const
{
    uint8 slots[4];
    slots[0] = NULL_SLOT;
    slots[1] = NULL_SLOT;
    slots[2] = NULL_SLOT;
    slots[3] = NULL_SLOT;
    switch( type )
    {
        case INVTYPE_HEAD:
            slots[0] = EQUIPMENT_SLOT_HEAD;
            break;
        case INVTYPE_NECK:
            slots[0] = EQUIPMENT_SLOT_NECK;
            break;
        case INVTYPE_SHOULDERS:
            slots[0] = EQUIPMENT_SLOT_SHOULDERS;
            break;
        case INVTYPE_BODY:
            slots[0] = EQUIPMENT_SLOT_BODY;
            break;
        case INVTYPE_CHEST:
            slots[0] = EQUIPMENT_SLOT_CHEST;
            break;
        case INVTYPE_ROBE:
            slots[0] = EQUIPMENT_SLOT_CHEST;
            break;
        case INVTYPE_WAIST:
            slots[0] = EQUIPMENT_SLOT_WAIST;
            break;
        case INVTYPE_LEGS:
            slots[0] = EQUIPMENT_SLOT_LEGS;
            break;
        case INVTYPE_FEET:
            slots[0] = EQUIPMENT_SLOT_FEET;
            break;
        case INVTYPE_WRISTS:
            slots[0] = EQUIPMENT_SLOT_WRISTS;
            break;
        case INVTYPE_HANDS:
            slots[0] = EQUIPMENT_SLOT_HANDS;
            break;
        case INVTYPE_FINGER:
            slots[0] = EQUIPMENT_SLOT_FINGER1;
            slots[1] = EQUIPMENT_SLOT_FINGER2;
            break;
        case INVTYPE_TRINKET:
            slots[0] = EQUIPMENT_SLOT_TRINKET1;
            slots[1] = EQUIPMENT_SLOT_TRINKET2;
            break;
        case INVTYPE_CLOAK:
            slots[0] =  EQUIPMENT_SLOT_BACK;
            break;
        case INVTYPE_WEAPON:
        {
            slots[0] = EQUIPMENT_SLOT_MAINHAND;

            // suggest offhand slot only if know dual wielding
            // (this will be replace mainhand weapon at auto equip instead unwonted "you don't known dual weilding" ...
            if(CanDualWield())
                slots[1] = EQUIPMENT_SLOT_OFFHAND;
        };break;
        case INVTYPE_SHIELD:
            slots[0] = EQUIPMENT_SLOT_OFFHAND;
            break;
        case INVTYPE_RANGED:
            slots[0] = EQUIPMENT_SLOT_RANGED;
            break;
        case INVTYPE_2HWEAPON:
            slots[0] = EQUIPMENT_SLOT_MAINHAND;
            break;
        case INVTYPE_TABARD:
            slots[0] = EQUIPMENT_SLOT_TABARD;
            break;
        case INVTYPE_WEAPONMAINHAND:
            slots[0] = EQUIPMENT_SLOT_MAINHAND;
            break;
        case INVTYPE_WEAPONOFFHAND:
            slots[0] = EQUIPMENT_SLOT_OFFHAND;
            break;
        case INVTYPE_HOLDABLE:
            slots[0] = EQUIPMENT_SLOT_OFFHAND;
            break;
        case INVTYPE_THROWN:
            slots[0] = EQUIPMENT_SLOT_RANGED;
            break;
        case INVTYPE_RANGEDRIGHT:
            slots[0] = EQUIPMENT_SLOT_RANGED;
            break;
        case INVTYPE_BAG:
            slots[0] = INVENTORY_SLOT_BAG_1;
            slots[1] = INVENTORY_SLOT_BAG_2;
            slots[2] = INVENTORY_SLOT_BAG_3;
            slots[3] = INVENTORY_SLOT_BAG_4;
            break;
        case INVTYPE_RELIC:
            slots[0] = EQUIPMENT_SLOT_RANGED;
            break;
        default :
            return NULL_SLOT;
    }

    if( slot != NULL_SLOT )
    {
        if( swap || !GetItemByPos( INVENTORY_SLOT_BAG_0, slot ) )
        {
            for (int i = 0; i < 4; i++)
            {
                if ( slots[i] == slot )
                    return slot;
            }
        }
    }
    else
    {
        // search empty slot at first
        for (int i = 0; i < 4; i++)
        {
            if ( slots[i] != NULL_SLOT && !GetItemByPos( INVENTORY_SLOT_BAG_0, slots[i] ) )
                return slots[i];
        }

        // if not found empty and can swap return first appropriate
        for (int i = 0; i < 4; i++)
        {
            if ( slots[i] != NULL_SLOT && swap )
                return slots[i];
        }
    }

    // no free position
    return NULL_SLOT;
}

Item* Player::CreateItem( uint32 item, uint32 count ) const
{
    ItemPrototype const *pProto = objmgr.GetItemPrototype( item );
    if( pProto )
    {
        Item *pItem = NewItemOrBag( pProto );
        if ( count > pProto->Stackable )
            count = pProto->Stackable;
        if ( count < 1 )
            count = 1;
        if( pItem->Create(objmgr.GenerateLowGuid(HIGHGUID_ITEM), item, const_cast<Player*>(this)) )
        {
            pItem->SetCount( count );
            return pItem;
        }
        else
            delete pItem;
    }
    return NULL;
}

uint32 Player::GetItemCount( uint32 item ) const
{
    Item *pItem;
    uint32 count = 0;
    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        pItem = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pItem && pItem->GetEntry() == item )
            count += pItem->GetCount();
    }
    Bag *pBag;
    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pBag )
            count += pBag->GetItemCount(item);
    }
    return count;
}

uint32 Player::GetBankItemCount( uint32 item ) const
{
    Item *pItem;
    uint32 count = 0;
    for(int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
    {
        pItem = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pItem && pItem->GetEntry() == item )
            count += pItem->GetCount();
    }
    Bag *pBag;
    for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
    {
        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pBag )
            count += pBag->GetItemCount(item);
    }
    return count;
}

uint16 Player::GetPosByGuid( uint64 guid ) const
{
    Item *pItem;
    uint16 pos;
    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        pItem = GetItemByPos( pos );
        if( pItem && pItem->GetGUID() == guid )
            return pos;
    }
    Bag *pBag;
    ItemPrototype const *pBagProto;
    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        pBag = (Bag*)GetItemByPos( pos );
        if( pBag )
        {
            pBagProto = pBag->GetProto();
            if( pBagProto )
            {
                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                {
                    pos = ((i << 8) | j);
                    pItem = GetItemByPos( pos );
                    if( pItem && pItem->GetGUID() == guid )
                        return pos;
                }
            }
        }
    }
    for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
    {
        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        pBag = (Bag*)GetItemByPos( pos );
        if( pBag )
        {
            pBagProto = pBag->GetProto();
            if( pBagProto )
            {
                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                {
                    pos = ((i << 8) | j);
                    pItem = GetItemByPos( pos );
                    if( pItem && pItem->GetGUID() == guid )
                        return pos;
                }
            }
        }
    }
    return 0;
}

Item* Player::GetItemByPos( uint16 pos ) const
{
    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;
    return GetItemByPos( bag, slot );
}

Item* Player::GetItemByPos( uint8 bag, uint8 slot ) const
{
    if( bag == INVENTORY_SLOT_BAG_0 && ( slot < BANK_SLOT_BAG_END ) )
        return m_items[slot];
    else if(bag >= INVENTORY_SLOT_BAG_START && bag < INVENTORY_SLOT_BAG_END
        || bag >= BANK_SLOT_BAG_START && bag < BANK_SLOT_BAG_END )
    {
        Bag *pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
        if ( pBag )
            return pBag->GetItemByPos(slot);
    }
    return NULL;
}

bool Player::HasBankBagSlot( uint8 slot ) const
{
    uint32 maxslot = ((GetUInt32Value(PLAYER_BYTES_2) & 0x70000) >> 16) + BANK_SLOT_BAG_START;
    if( slot < maxslot )
        return true;
    return false;
}

bool Player::IsInventoryPos( uint16 pos ) const
{
    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;
    if( bag == INVENTORY_SLOT_BAG_0 && slot == NULL_SLOT )
        return true;
    if( bag == INVENTORY_SLOT_BAG_0 && ( slot >= INVENTORY_SLOT_ITEM_START && slot < INVENTORY_SLOT_ITEM_END ) )
        return true;
    if( bag >= INVENTORY_SLOT_BAG_START && bag < INVENTORY_SLOT_BAG_END )
        return true;
    return false;
}

bool Player::IsEquipmentPos( uint16 pos ) const
{
    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;
    if( bag == INVENTORY_SLOT_BAG_0 && ( slot < EQUIPMENT_SLOT_END ) )
        return true;
    if( bag == INVENTORY_SLOT_BAG_0 && ( slot >= INVENTORY_SLOT_BAG_START && slot < INVENTORY_SLOT_BAG_END ) )
        return true;
    return false;
}

bool Player::IsBankPos( uint16 pos ) const
{
    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;
    if( bag == INVENTORY_SLOT_BAG_0 && ( slot >= BANK_SLOT_ITEM_START && slot < BANK_SLOT_ITEM_END ) )
        return true;
    if( bag == INVENTORY_SLOT_BAG_0 && ( slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END ) )
        return true;
    if( bag >= BANK_SLOT_BAG_START && bag < BANK_SLOT_BAG_END )
        return true;
    return false;
}

bool Player::HasItemCount( uint32 item, uint32 count ) const
{
    Item *pItem;
    uint32 tempcount = 0;
    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        pItem = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pItem && pItem->GetEntry() == item )
        {
            tempcount += pItem->GetCount();
            if( tempcount >= count )
                return true;
        }
    }
    Bag *pBag;
    ItemPrototype const *pBagProto;
    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pBag )
        {
            pBagProto = pBag->GetProto();
            if( pBagProto )
            {
                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                {
                    pItem = GetItemByPos( i, j );
                    if( pItem && pItem->GetEntry() == item )
                    {
                        tempcount += pItem->GetCount();
                        if( tempcount >= count )
                            return true;
                    }
                }
            }
        }
    }
    return false;
}

uint8 Player::CanStoreNewItem( uint8 bag, uint8 slot, uint16 &dest, uint32 item, uint32 count, bool swap ) const
{
    dest = 0;
    Item *pItem = CreateItem( item, count );
    if( pItem )
    {
        uint8 result = CanStoreItem( bag, slot, dest, pItem, swap );
        delete pItem;
        return result;
    }
    if( !swap )
        return EQUIP_ERR_ITEM_NOT_FOUND;
    else
        return EQUIP_ERR_ITEMS_CANT_BE_SWAPPED;
}

uint8 Player::CanStoreItem( uint8 bag, uint8 slot, uint16 &dest, Item *pItem, bool swap ) const
{
    dest = 0;
    if( pItem )
    {
        sLog.outDebug( "STORAGE: CanStoreItem bag = %u, slot = %u, item = %u, count = %u", bag, slot, pItem->GetEntry(), pItem->GetCount());
        ItemPrototype const *pProto = pItem->GetProto();
        if( pProto )
        {
            Item *pItem2;
            Bag *pBag;
            ItemPrototype const *pBagProto;
            uint16 pos;
            if(pItem->IsBindedNotWith(GetGUID()))
                return EQUIP_ERR_DONT_OWN_THAT_ITEM;
            if( bag == 0 )
            {
                // check count of items
                if( !swap && pProto->MaxCount > 0 )
                {
                    uint32 curcount = 0;
                    for(int i = EQUIPMENT_SLOT_START; i < BANK_SLOT_BAG_END; i++)
                    {
                        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i );
                        pItem2 = GetItemByPos( pos );
                        if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() )
                        {
                            curcount += pItem2->GetCount();
                            if( curcount + pItem->GetCount() > pProto->MaxCount )
                                return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                        }
                    }
                    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
                    {
                        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( pos );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ((i << 8) | j );
                                    pItem2 = GetItemByPos( pos );
                                    if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() )
                                    {
                                        curcount += pItem2->GetCount();
                                        if( curcount + pItem->GetCount() > pProto->MaxCount )
                                            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                                    }
                                }
                            }
                        }
                    }
                    for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
                    {
                        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( pos );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ((i << 8) | j );
                                    pItem2 = GetItemByPos( pos );
                                    if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() )
                                    {
                                        curcount += pItem2->GetCount();
                                        if( curcount + pItem->GetCount() > pProto->MaxCount )
                                            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                                    }
                                }
                            }
                        }
                    }
                }

                // search stack for merge to
                if( pProto->Stackable > 1 )
                {
                    for(int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
                    {
                        pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        pItem2 = GetItemByPos( pos );
                        if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                        {
                            dest = pos;
                            return EQUIP_ERR_OK;
                        }
                    }
                    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
                    {
                        pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( pos );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ( (i << 8) | j );
                                    pItem2 = GetItemByPos( pos );
                                    if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                                    {
                                        dest = pos;
                                        return EQUIP_ERR_OK;
                                    }
                                }
                            }
                        }
                    }
                }

                // search free slot - ammo special case
                if( pProto->Class == ITEM_CLASS_PROJECTILE )
                {
                    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
                    {
                        pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto && pBagProto->SubClass == pProto->SubClass )
                            {
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                                    pItem2 = GetItemByPos( i, j );
                                    if( !pItem2 )
                                    {
                                        dest = ( (i << 8) | j );
                                        return EQUIP_ERR_OK;
                                    }
                                }
                            }
                        }
                    }
                }

                // search free slot
                for(int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
                {
                    pItem2 = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                    if( !pItem2 )
                    {
                        dest = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        return EQUIP_ERR_OK;
                    }
                }

                for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
                {
                    pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                    if( pBag )
                    {
                        pBagProto = pBag->GetProto();
                        if( pBagProto && pBagProto->Class != ITEM_CLASS_QUIVER )
                        {
                            for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                            {
                                pItem2 = GetItemByPos( i, j );
                                if( !pItem2 )
                                {
                                    dest = ( (i << 8) | j );
                                    return EQUIP_ERR_OK;
                                }
                            }
                        }
                    }
                }
                return EQUIP_ERR_INVENTORY_FULL;
            }
            else                                            // in specific bag
            {
                if( slot == NULL_SLOT )
                {
                    if( pProto->InventoryType == INVTYPE_BAG )
                    {
                        Bag *pBag = (Bag*)pItem;
                        if( pBag && !pBag->IsEmpty() )
                            return EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG;
                    }

                    // search stack in bag for merge to
                    if( pProto->Stackable > 1 )
                    {
                        if( bag == INVENTORY_SLOT_BAG_0 )
                        {
                            for(int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
                            {
                                pItem2 = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                                if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                                {
                                    dest = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                                    return EQUIP_ERR_OK;
                                }
                            }
                        }
                        else
                        {
                            pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
                            if( pBag )
                            {
                                pBagProto = pBag->GetProto();
                                if( pBagProto )
                                {
                                    for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                    {
                                        pItem2 = GetItemByPos( bag, j );
                                        if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                                        {
                                            dest = ( (bag << 8) | j );
                                            return EQUIP_ERR_OK;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if( bag == INVENTORY_SLOT_BAG_0 )
                    {
                        for(int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
                        {
                            pItem2 = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                            if( !pItem2 )
                            {
                                dest = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                                return EQUIP_ERR_OK;
                            }
                        }
                    }
                    else
                    {
                        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                if( pBagProto->Class == ITEM_CLASS_QUIVER && pBagProto->SubClass != pProto->SubClass )
                                    return EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE;
                                if( pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass > ITEM_SUBCLASS_CONTAINER && pBagProto->SubClass != pProto->SubClass )
                                    return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pItem2 = GetItemByPos( bag, j );
                                    if( !pItem2 )
                                    {
                                        dest = ( (bag << 8) | j );
                                        return EQUIP_ERR_OK;
                                    }
                                }
                            }
                        }
                    }
                    return EQUIP_ERR_BAG_FULL;
                }
                else                                        // specific bag and slot
                {
                    if( pProto->InventoryType == INVTYPE_BAG )
                    {
                        Bag *pBag = (Bag*)pItem;
                        if( pBag && !pBag->IsEmpty() )
                            return EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG;
                    }
                    pItem2 = GetItemByPos( bag, slot );
                    if( pItem2 && !swap )
                    {
                        if( pProto->Stackable > 1 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() < pProto->Stackable )
                        {
                            dest = ( (bag << 8) | slot );
                            return EQUIP_ERR_OK;
                        }
                        else
                            return EQUIP_ERR_COULDNT_SPLIT_ITEMS;
                    }
                    else
                    {
                        if( bag == INVENTORY_SLOT_BAG_0 )
                        {
                            dest = ( (bag << 8) | slot );
                            return EQUIP_ERR_OK;
                        }
                        else
                        {
                            pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
                            if( pBag )
                            {
                                pBagProto = pBag->GetProto();
                                if( pBagProto )
                                {
                                    if( pBagProto->Class == ITEM_CLASS_QUIVER && pBagProto->SubClass != pProto->SubClass )
                                        return EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE;
                                    if( pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass > ITEM_SUBCLASS_CONTAINER && pBagProto->SubClass != pProto->SubClass )
                                        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
                                    dest = ( (bag << 8) | slot );
                                    return EQUIP_ERR_OK;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if( !swap )
        return EQUIP_ERR_ITEM_NOT_FOUND;
    else
        return EQUIP_ERR_ITEMS_CANT_BE_SWAPPED;
    return 0;
}

uint8 Player::CanEquipItem( uint8 slot, uint16 &dest, Item *pItem, bool swap, bool check_alive ) const
{
    dest = 0;
    if( pItem )
    {
        sLog.outDebug( "STORAGE: CanEquipItem slot = %u, item = %u, count = %u", slot, pItem->GetEntry(), pItem->GetCount());
        ItemPrototype const *pProto = pItem->GetProto();
        if( pProto )
        {
            if( isInCombat()&& pProto->Class != ITEM_CLASS_WEAPON && pProto->Class != ITEM_CLASS_PROJECTILE &&
                pProto->SubClass != ITEM_SUBCLASS_ARMOR_SHIELD && pProto->InventoryType != INVTYPE_RELIC)
                return EQUIP_ERR_CANT_DO_IN_COMBAT;

            uint32 type = pProto->InventoryType;
            uint8 eslot = FindEquipSlot( type, slot, swap );
            if( eslot == NULL_SLOT )
                return EQUIP_ERR_ITEM_CANT_BE_EQUIPPED;

            uint8 msg = CanUseItem( pItem , check_alive );
            if( msg != EQUIP_ERR_OK )
                return msg;
            if( !swap && GetItemByPos( INVENTORY_SLOT_BAG_0, eslot ) )
                return EQUIP_ERR_NO_EQUIPMENT_SLOT_AVAILABLE;

            if(eslot == EQUIPMENT_SLOT_OFFHAND)
            {
                if( type == INVTYPE_WEAPON || type == INVTYPE_WEAPONOFFHAND )
                {
                    if(!CanDualWield())
                        return EQUIP_ERR_CANT_DUAL_WIELD;
                }

                Item *mainItem = GetItemByPos( INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND );
                if(mainItem)
                {
                    if(mainItem->GetProto()->InventoryType == INVTYPE_2HWEAPON)
                        return EQUIP_ERR_CANT_EQUIP_WITH_TWOHANDED;
                }else
                // not let equip offhand item if mainhand not equiped
                return EQUIP_ERR_ITEM_CANT_BE_EQUIPPED;

            }

            if( type == INVTYPE_2HWEAPON )
            {
                uint8 twinslot = ( eslot == EQUIPMENT_SLOT_MAINHAND ? EQUIPMENT_SLOT_OFFHAND : EQUIPMENT_SLOT_MAINHAND );
                Item *twinItem = GetItemByPos( INVENTORY_SLOT_BAG_0, twinslot );
                if( twinItem )
                    return EQUIP_ERR_ITEM_CANT_BE_EQUIPPED;
            }
            dest = ((INVENTORY_SLOT_BAG_0 << 8) | eslot);
            return EQUIP_ERR_OK;
        }
    }
    if( !swap )
        return EQUIP_ERR_ITEM_NOT_FOUND;
    else
        return EQUIP_ERR_ITEMS_CANT_BE_SWAPPED;
}

uint8 Player::CanUnequipItem( uint16 pos, bool swap ) const
{
    // Applied only to equiped items
    if(!IsEquipmentPos(pos) )
        return EQUIP_ERR_OK;

    Item* pItem = GetItemByPos(pos);

    // Applied only to existed equiped item
    if( !pItem )
        return EQUIP_ERR_OK;

    sLog.outDebug( "STORAGE: CanUnequipItem slot = %u, item = %u, count = %u", pos, pItem->GetEntry(), pItem->GetCount());

    ItemPrototype const *pProto = pItem->GetProto();
    if( !pProto )
        return EQUIP_ERR_ITEM_NOT_FOUND;

    if( isInCombat()&& pProto->Class != ITEM_CLASS_WEAPON && pProto->Class != ITEM_CLASS_PROJECTILE &&
        pProto->SubClass != ITEM_SUBCLASS_ARMOR_SHIELD && pProto->InventoryType != INVTYPE_RELIC )
        return EQUIP_ERR_CANT_DO_IN_COMBAT;

    // All equiped items can swaped (not in combat case)
    if(swap)
        return EQUIP_ERR_OK;

    uint8 slot = pos & 255;

    // can't unequip mainhand item if offhand item equiped
    if(slot == EQUIPMENT_SLOT_MAINHAND && GetItemByPos( INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_OFFHAND ))
        return EQUIP_ERR_CANT_DO_RIGHT_NOW;

    return EQUIP_ERR_OK;
}

uint8 Player::CanBankItem( uint8 bag, uint8 slot, uint16 &dest, Item *pItem, bool swap, bool check_alive ) const
{
    dest = 0;
    if( pItem )
    {
        sLog.outDebug( "STORAGE: CanBankItem bag = %u, slot = %u, item = %u, count = %u", bag, slot, pItem->GetEntry(), pItem->GetCount());
        ItemPrototype const *pProto = pItem->GetProto();
        if( pProto )
        {
            Item *pItem2;
            Bag *pBag;
            ItemPrototype const *pBagProto;
            uint16 pos;
            if( pItem->IsBindedNotWith(GetGUID()) )
                return EQUIP_ERR_DONT_OWN_THAT_ITEM;
            if( bag == 0 )
            {
                if( !swap && pProto->MaxCount > 0 )
                {
                    uint32 curcount = 0;
                    for(int i = EQUIPMENT_SLOT_START; i < BANK_SLOT_BAG_END; i++)
                    {
                        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i );
                        pItem2 = GetItemByPos( pos );
                        if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() )
                        {
                            curcount += pItem2->GetCount();
                            if( curcount + pItem->GetCount() > pProto->MaxCount )
                                return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                        }
                    }
                    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
                    {
                        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( pos );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ((i << 8) | j );
                                    pItem2 = GetItemByPos( pos );
                                    if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() )
                                    {
                                        curcount += pItem2->GetCount();
                                        if( curcount + pItem->GetCount() > pProto->MaxCount )
                                            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                                    }
                                }
                            }
                        }
                    }
                    for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
                    {
                        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( pos );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ((i << 8) | j );
                                    pItem2 = GetItemByPos( pos );
                                    if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() )
                                    {
                                        curcount += pItem2->GetCount();
                                        if( curcount + pItem->GetCount() > pProto->MaxCount )
                                            return EQUIP_ERR_CANT_CARRY_MORE_OF_THIS;
                                    }
                                }
                            }
                        }
                    }
                }
                if( pProto->Stackable > 1 )
                {
                    for(int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
                    {
                        pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        pItem2 = GetItemByPos( pos );
                        if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                        {
                            dest = pos;
                            return EQUIP_ERR_OK;
                        }
                    }
                    for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
                    {
                        pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( pos );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ( (i << 8) | j );
                                    pItem2 = GetItemByPos( pos );
                                    if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                                    {
                                        dest = pos;
                                        return EQUIP_ERR_OK;
                                    }
                                }
                            }
                        }
                    }
                }
                if( pProto->Class == ITEM_CLASS_PROJECTILE )
                {
                    for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
                    {
                        pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto && pBagProto->SubClass == pProto->SubClass )
                            {
                                for(int j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pos = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                                    pItem2 = GetItemByPos( i, j );
                                    if( !pItem2 )
                                    {
                                        dest = ( (i << 8) | j );
                                        return EQUIP_ERR_OK;
                                    }
                                }
                            }
                        }
                    }
                }
                for(int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
                {
                    pItem2 = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                    if( !pItem2 )
                    {
                        dest = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                        return EQUIP_ERR_OK;
                    }
                }
                for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
                {
                    pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                    if( pBag )
                    {
                        pBagProto = pBag->GetProto();
                        if( pBagProto && pBagProto->Class != ITEM_CLASS_QUIVER )
                        {
                            for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                            {
                                pItem2 = GetItemByPos( i, j );
                                if( !pItem2 )
                                {
                                    dest = ( (i << 8) | j );
                                    return EQUIP_ERR_OK;
                                }
                            }
                        }
                    }
                }
                return EQUIP_ERR_BANK_FULL;
            }
            else
            {
                if( slot == NULL_SLOT )
                {
                    if( pProto->InventoryType == INVTYPE_BAG )
                    {
                        Bag *pBag = (Bag*)pItem;
                        if( pBag && !pBag->IsEmpty() )
                            return EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG;
                    }
                    if( pProto->Stackable > 1 )
                    {
                        if( bag == INVENTORY_SLOT_BAG_0 )
                        {
                            for(int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
                            {
                                pItem2 = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                                if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                                {
                                    dest = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                                    return EQUIP_ERR_OK;
                                }
                            }
                        }
                        else
                        {
                            pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
                            if( pBag )
                            {
                                pBagProto = pBag->GetProto();
                                if( pBagProto )
                                {
                                    for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                    {
                                        pItem2 = GetItemByPos( bag, j );
                                        if( pItem2 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                                        {
                                            dest = ( (bag << 8) | j );
                                            return EQUIP_ERR_OK;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if( bag == INVENTORY_SLOT_BAG_0 )
                    {
                        for(int i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; i++)
                        {
                            pItem2 = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
                            if( !pItem2 )
                            {
                                dest = ( (INVENTORY_SLOT_BAG_0 << 8) | i );
                                return EQUIP_ERR_OK;
                            }
                        }
                    }
                    else
                    {
                        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
                        if( pBag )
                        {
                            pBagProto = pBag->GetProto();
                            if( pBagProto )
                            {
                                if( pBagProto->Class == ITEM_CLASS_QUIVER && pBagProto->SubClass != pProto->SubClass )
                                    return EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE;
                                if( pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass > ITEM_SUBCLASS_CONTAINER && pBagProto->SubClass != pProto->SubClass )
                                    return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
                                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                                {
                                    pItem2 = GetItemByPos( bag, j );
                                    if( !pItem2 )
                                    {
                                        dest = ( (bag << 8) | j );
                                        return EQUIP_ERR_OK;
                                    }
                                }
                            }
                        }
                    }
                    return EQUIP_ERR_BAG_FULL;
                }
                else
                {
                    if( pProto->InventoryType == INVTYPE_BAG )
                    {
                        Bag *pBag = (Bag*)pItem;
                        if( pBag )
                        {
                            if( slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END )
                            {
                                if( !HasBankBagSlot( slot ) )
                                    return EQUIP_ERR_MUST_PURCHASE_THAT_BAG_SLOT;
                                if( uint8 cantuse = CanUseItem( pItem, check_alive ) != EQUIP_ERR_OK )
                                    return cantuse;
                            }
                            else
                            {
                                if( !pBag->IsEmpty() )
                                    return EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG;
                            }
                        }
                    }
                    else
                    {
                        if( slot >= BANK_SLOT_BAG_START && slot < BANK_SLOT_BAG_END )
                            return EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT;
                    }
                    pItem2 = GetItemByPos( bag, slot );
                    if( pItem2 && !swap )
                    {
                        if( pProto->Stackable > 1 && pItem2->GetEntry() == pItem->GetEntry() && pItem2->GetCount() + pItem->GetCount() <= pProto->Stackable )
                        {
                            dest = ( (bag << 8) | slot );
                            return EQUIP_ERR_OK;
                        }
                        else
                            return EQUIP_ERR_COULDNT_SPLIT_ITEMS;
                    }
                    else
                    {
                        if( bag == INVENTORY_SLOT_BAG_0 )
                        {
                            dest = ( (bag << 8) | slot );
                            return EQUIP_ERR_OK;
                        }
                        else
                        {
                            pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
                            if( pBag )
                            {
                                pBagProto = pBag->GetProto();
                                if( pBagProto )
                                {
                                    if( pBagProto->Class == ITEM_CLASS_QUIVER && pBagProto->SubClass != pProto->SubClass )
                                        return EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE;
                                    if( pBagProto->Class == ITEM_CLASS_CONTAINER && pBagProto->SubClass > ITEM_SUBCLASS_CONTAINER && pBagProto->SubClass != pProto->SubClass )
                                        return EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG;
                                    dest = ( (bag << 8) | slot );
                                    return EQUIP_ERR_OK;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if( !swap )
        return EQUIP_ERR_ITEM_NOT_FOUND;
    else
        return EQUIP_ERR_ITEMS_CANT_BE_SWAPPED;
    return 0;
}

uint8 Player::CanUseItem( Item *pItem, bool check_alive ) const
{
    if( pItem )
    {
        sLog.outDebug( "STORAGE: CanUseItem item = %u", pItem->GetEntry());
        if( !isAlive() && check_alive )
            return EQUIP_ERR_YOU_ARE_DEAD;
        //if( isStunned() )
        //    return EQUIP_ERR_YOU_ARE_STUNNED;
        ItemPrototype const *pProto = pItem->GetProto();
        if( pProto )
        {
            if( pItem->IsBindedNotWith(GetGUID()) )
                return EQUIP_ERR_DONT_OWN_THAT_ITEM;
            if( (pProto->AllowableClass & getClassMask()) == 0 || (pProto->AllowableRace & getRaceMask()) == 0 )
                return EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM;
            if( pItem->GetSkill() != 0  )
            {
                if( GetSkillValue( pItem->GetSkill() ) == 0 )
                    return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
            }
            if( pProto->RequiredSkill != 0  )
            {
                if( GetSkillValue( pProto->RequiredSkill ) == 0 )
                    return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
                else if( GetSkillValue( pProto->RequiredSkill ) < pProto->RequiredSkillRank )
                    return EQUIP_ERR_SKILL_ISNT_HIGH_ENOUGH;
            }
            if( pProto->RequiredSpell != 0 && !HasSpell( pProto->RequiredSpell ) )
                return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
            if( GetHonorRank() < pProto->RequiredHonorRank )
                return EQUIP_ITEM_RANK_NOT_ENOUGH;
            /*if( GetREputation() < pProto->RequiredReputation )
                return EQUIP_ITEM_REPUTATION_NOT_ENOUGH;
            */
            if( getLevel() < pProto->RequiredLevel )
                return EQUIP_ERR_YOU_MUST_REACH_LEVEL_N;
            return EQUIP_ERR_OK;
        }
    }
    return EQUIP_ERR_ITEM_NOT_FOUND;
}

bool Player::CanUseItem( ItemPrototype const *pProto )
{
    // Used by group, function NeedBeforeGreed, to know if a prototype can be used by a player

    if( pProto )
    {
        if( (pProto->AllowableClass & getClassMask()) == 0 || (pProto->AllowableRace & getRaceMask()) == 0 )
            return false;
        if( pProto->RequiredSkill != 0  )
        {
            if( GetSkillValue( pProto->RequiredSkill ) == 0 )
                return false;
            else if( GetSkillValue( pProto->RequiredSkill ) < pProto->RequiredSkillRank )
                return false;
        }
        if( pProto->RequiredSpell != 0 && !HasSpell( pProto->RequiredSpell ) )
            return false;
        if( GetHonorRank() < pProto->RequiredHonorRank )
            return false;
        if( getLevel() < pProto->RequiredLevel )
            return false;
        return true;
    }
    return false;
}

uint8 Player::CanUseAmmo( uint32 item ) const
{
    sLog.outDebug( "STORAGE: CanUseAmmo item = %u", item);
    if( !isAlive() )
        return EQUIP_ERR_YOU_ARE_DEAD;
    //if( isStunned() )
    //    return EQUIP_ERR_YOU_ARE_STUNNED;
    ItemPrototype const *pProto = objmgr.GetItemPrototype( item );
    if( pProto )
    {
        if( pProto->InventoryType!= INVTYPE_AMMO )
            return EQUIP_ERR_ONLY_AMMO_CAN_GO_HERE;
        if( (pProto->AllowableClass & getClassMask()) == 0 || (pProto->AllowableRace & getRaceMask()) == 0 )
            return EQUIP_ERR_YOU_CAN_NEVER_USE_THAT_ITEM;
        if( pProto->RequiredSkill != 0  )
        {
            if( GetSkillValue( pProto->RequiredSkill ) == 0 )
                return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
            else if( GetSkillValue( pProto->RequiredSkill ) < pProto->RequiredSkillRank )
                return EQUIP_ERR_SKILL_ISNT_HIGH_ENOUGH;
        }
        if( pProto->RequiredSpell != 0 && !HasSpell( pProto->RequiredSpell ) )
            return EQUIP_ERR_NO_REQUIRED_PROFICIENCY;
        if( GetHonorRank() < pProto->RequiredHonorRank )
            return EQUIP_ITEM_RANK_NOT_ENOUGH;
        /*if( GetREputation() < pProto->RequiredReputation )
        return EQUIP_ITEM_REPUTATION_NOT_ENOUGH;
        */
        if( getLevel() < pProto->RequiredLevel )
            return EQUIP_ERR_YOU_MUST_REACH_LEVEL_N;
        return EQUIP_ERR_OK;
    }
    return EQUIP_ERR_ITEM_NOT_FOUND;
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Player::StoreNewItem( uint16 pos, uint32 item, uint32 count, bool update ,bool fromLoot )
{
    Item *pItem = CreateItem( item, count );
    if( pItem )
    {
        ItemPrototype const *pProto = pItem->GetProto();
        ItemAddedQuestCheck( item, count );
        if(fromLoot)
            pItem->SetItemRandomProperties();
        Item * retItem = StoreItem( pos, pItem, update );
        
        return retItem;
    }
    return NULL;
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Player::StoreItem( uint16 pos, Item *pItem, bool update )
{
    if( pItem )
    {
        if( pItem->GetProto()->Bonding == BIND_WHEN_PICKED_UP || pItem->GetProto()->Class == ITEM_CLASS_QUEST)
            pItem->SetBinding( true );

        uint8 bag = pos >> 8;
        uint8 slot = pos & 255;

        sLog.outDebug( "STORAGE: StoreItem bag = %u, slot = %u, item = %u, count = %u", bag, slot, pItem->GetEntry(), pItem->GetCount());

        Item *pItem2 = GetItemByPos( bag, slot );

        if( !pItem2 )
        {
            if( bag == INVENTORY_SLOT_BAG_0 )
            {
                m_items[slot] = pItem;
                SetUInt64Value( (uint16)(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2) ), pItem->GetGUID() );
                pItem->SetUInt64Value( ITEM_FIELD_CONTAINED, GetGUID() );
                pItem->SetUInt64Value( ITEM_FIELD_OWNER, GetGUID() );

                pItem->SetSlot( slot );
                pItem->SetContainer( NULL );

                if( IsInWorld() && update )
                {
                    pItem->AddToWorld();
                    pItem->SendUpdateToPlayer( this );
                }

                pItem->SetState(ITEM_CHANGED, this);
            }
            else
            {
                Bag *pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
                if( pBag )
                {
                    pBag->StoreItem( slot, pItem, update );
                    if( IsInWorld() && update )
                    {
                        pItem->AddToWorld();
                        pItem->SendUpdateToPlayer( this );
                    }
                    pItem->SetState(ITEM_CHANGED, this);
                    pBag->SetState(ITEM_CHANGED, this);
                }
            }
        }
        else
        {
            pItem2->SetCount( pItem2->GetCount() + pItem->GetCount() );
            if( IsInWorld() && update )
                pItem2->SendUpdateToPlayer( this );

            // delete item (it not in any slot currently)
            //pItem->DeleteFromDB();
            if( IsInWorld() && update )
            {
                pItem->RemoveFromWorld();
                pItem->DestroyForPlayer( this );
            }
            pItem->SetState(ITEM_REMOVED, this);
            pItem2->SetState(ITEM_CHANGED, this);

            return pItem2;
        }
    }
    return pItem;
}

void Player::EquipItem( uint16 pos, Item *pItem, bool update )
{
    if( pItem )
    {
        VisualizeItem( pos, pItem);
        uint8 slot = pos & 255;

        if(isAlive())
            _ApplyItemMods(pItem, slot, true);

        if( IsInWorld() && update )
        {
            pItem->AddToWorld();
            pItem->SendUpdateToPlayer( this );
        }
    }
}

void Player::QuickEquipItem( uint16 pos, Item *pItem)
{
    if( pItem )
    {
        VisualizeItem( pos, pItem);

        if( IsInWorld() )
        {
            pItem->AddToWorld();
            pItem->SendUpdateToPlayer( this );
        }
    }
}

void Player::VisualizeItem( uint16 pos, Item *pItem)
{
    if(!pItem)
        return;

    // check also  BIND_WHEN_PICKED_UP for .additem or .additemset case by GM (not binded at adding to inventory)
    if( pItem->GetProto()->Bonding == BIND_WHEN_EQUIPED || pItem->GetProto()->Bonding == BIND_WHEN_PICKED_UP )
        pItem->SetBinding( true );

    uint8 bag = pos >> 8;
    uint8 slot = pos & 255;

    sLog.outDebug( "STORAGE: EquipItem bag = %u, slot = %u, item = %u", bag, slot, pItem->GetEntry());

    m_items[slot] = pItem;
    SetUInt64Value( (uint16)(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2) ), pItem->GetGUID() );
    pItem->SetUInt64Value( ITEM_FIELD_CONTAINED, GetGUID() );
    pItem->SetUInt64Value( ITEM_FIELD_OWNER, GetGUID() );
    pItem->SetSlot( slot );
    pItem->SetContainer( NULL );

    if( slot < EQUIPMENT_SLOT_END )
    {
        int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * 12);
        SetUInt32Value(VisibleBase, pItem->GetEntry());
        SetUInt32Value(VisibleBase + 1, pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT));
        SetUInt32Value(VisibleBase + 2, pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT + 1*3));
        SetUInt32Value(VisibleBase + 3, pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT + 2*3));
        SetUInt32Value(VisibleBase + 4, pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT + 3*3));
        SetUInt32Value(VisibleBase + 5, pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT + 4*3));
        SetUInt32Value(VisibleBase + 6, pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT + 5*3));
        SetUInt32Value(VisibleBase + 7, pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT + 6*3));
        SetUInt32Value(VisibleBase + 8, pItem->GetUInt32Value(ITEM_FIELD_RANDOM_PROPERTIES_ID));
    }

    pItem->SetState(ITEM_CHANGED, this);
}

// Return stored item (if stored to stack, it can diff. from pItem). And pItem ca be deleted in this case.
Item* Player::BankItem( uint16 pos, Item *pItem, bool update )
{
    return StoreItem( pos, pItem, update);
}

void Player::RemoveItem( uint8 bag, uint8 slot, bool update )
{
    // note: removeitem does not actualy change the item
    // it only takes the item out of storage temporarily
    // note2: if removeitem is to be used for delinking
    // the item must be removed from the player's updatequeue

    Item *pItem = GetItemByPos( bag, slot );
    if( pItem )
    {
        sLog.outDebug( "STORAGE: RemoveItem bag = %u, slot = %u, item = %u", bag, slot, pItem->GetEntry());

        if( bag == INVENTORY_SLOT_BAG_0 )
        {
            if ( slot < INVENTORY_SLOT_BAG_END )
                _ApplyItemMods(pItem, slot, false);

            m_items[slot] = NULL;
            SetUInt64Value((uint16)(PLAYER_FIELD_INV_SLOT_HEAD + (slot*2)), 0);

            if ( slot < EQUIPMENT_SLOT_END )
            {
                int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * 12);
                for (int i = VisibleBase; i < VisibleBase + 12; ++i)
                    SetUInt32Value(i, 0);
            }
        }
        else
        {
            Bag *pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
            if( pBag )
                pBag->RemoveItem(slot, update);
        }
        pItem->SetUInt64Value( ITEM_FIELD_CONTAINED, 0 );
        // pItem->SetUInt64Value( ITEM_FIELD_OWNER, 0 ); not clear owner at remove (it will be set at store). This used in mail and auction code
        pItem->SetSlot( NULL_SLOT );
        if( IsInWorld() && update )
            pItem->SendUpdateToPlayer( this );
    }
}

void Player::RemoveItemCount( uint32 item, uint32 count, bool update )
{
    sLog.outDebug( "STORAGE: RemoveItemCount item = %u, count = %u", item, count);
    Item *pItem;
    uint32 remcount = 0;
    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        pItem = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pItem && pItem->GetEntry() == item )
        {
            if( pItem->GetCount() + remcount <= count )
            {
                remcount += pItem->GetCount();
                RemoveItem( INVENTORY_SLOT_BAG_0, i, update );

                if(remcount >=count)
                    return;
            }
            else
            {
                pItem->SetCount( pItem->GetCount() - count + remcount );
                if( IsInWorld() && update )
                    pItem->SendUpdateToPlayer( this );
                pItem->SetState(ITEM_CHANGED, this);
                return;
            }
        }
    }
    Bag *pBag;
    ItemPrototype const *pBagProto;
    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pBag )
        {
            pBagProto = pBag->GetProto();
            if( pBagProto )
            {
                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                {
                    pItem = GetItemByPos( i, j );
                    if( pItem && pItem->GetEntry() == item )
                    {
                        if( pItem->GetCount() + remcount <= count )
                        {
                            remcount += pItem->GetCount();
                            RemoveItem( i, j, update );

                            if(remcount >=count)
                                return;
                        }
                        else
                        {
                            pItem->SetCount( pItem->GetCount() - count + remcount );
                            if( IsInWorld() && update )
                                pItem->SendUpdateToPlayer( this );
                            pItem->SetState(ITEM_CHANGED, this);
                            return;
                        }
                    }
                }
            }
        }
    }
}

void Player::DestroyItem( uint8 bag, uint8 slot, bool update )
{
    Item *pItem = GetItemByPos( bag, slot );
    if( pItem )
    {
        sLog.outDebug( "STORAGE: DestroyItem bag = %u, slot = %u, item = %u", bag, slot, pItem->GetEntry());

        //pItem->SetOwnerGUID(0);
        pItem->SetSlot( NULL_SLOT );
        pItem->SetUInt64Value( ITEM_FIELD_CONTAINED, 0 );
        ItemPrototype const *pProto = pItem->GetProto();

        for(std::list<struct EnchantDuration*>::iterator itr = m_enchantDuration.begin(),next;itr != m_enchantDuration.end();)
        {
            if((*itr)->item == pItem)
                m_enchantDuration.erase(itr++);
            else
                ++itr;
        }

        if( bag == INVENTORY_SLOT_BAG_0 )
        {
            ItemRemovedQuestCheck( pItem->GetEntry(), pItem->GetCount() );

            SetUInt64Value((uint16)(PLAYER_FIELD_INV_SLOT_HEAD + (slot*2)), 0);

            if ( slot < EQUIPMENT_SLOT_END )
            {
                _ApplyItemMods(pItem, slot, false);
                int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * 12);
                for (int i = VisibleBase; i < VisibleBase + 12; ++i)
                    SetUInt32Value(i, 0);
                for(int enchant_slot = 0 ; enchant_slot < 7; enchant_slot++)
                {
                    uint32 Enchant_id = pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+enchant_slot*3);
                    if( Enchant_id)
                    {
                        SpellItemEnchantmentEntry *pEnchant = sSpellItemEnchantmentStore.LookupEntry(Enchant_id);
                        if(!pEnchant)
                            continue;
                        uint32 enchant_display = pEnchant->display_type;
                        uint32 enchant_value1 = pEnchant->value1;
                        //uint32 enchant_value2 = pEnchant->value2;
                        uint32 enchant_spell_id = pEnchant->spellid;
                        //uint32 enchant_aura_id = pEnchant->aura_id;
                        //uint32 enchant_description = pEnchant->description;
                        //SpellEntry *enchantSpell_info = sSpellStore.LookupEntry(enchant_spell_id);
                        if(enchant_display ==4)
                            SetArmor(GetArmor()-enchant_value1);
                        else if(enchant_display ==2)
                        {
                            SetFloatValue(UNIT_FIELD_MINDAMAGE,GetFloatValue(UNIT_FIELD_MINDAMAGE)-enchant_value1);
                            SetFloatValue(UNIT_FIELD_MAXDAMAGE,GetFloatValue(UNIT_FIELD_MAXDAMAGE)-enchant_value1);
                        }
                        else
                        {
                            RemoveAurasDueToSpell(enchant_spell_id);
                        }
                    }
                }
            }

            m_items[slot] = NULL;
            
            if( IsInWorld() && update )
            {
                pItem->RemoveFromWorld();
                pItem->DestroyForPlayer( this );
            }
        }
        else
        {
            Bag *pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, bag );
            if( pBag )
            {
                if( pProto && pProto->Class == ITEM_CLASS_QUEST )
                    ItemRemovedQuestCheck( pItem->GetEntry(), pItem->GetCount() );
                
                pBag->RemoveItem(slot, update);

                if( IsInWorld() && update )
                {
                    pItem->RemoveFromWorld();
                    pItem->DestroyForPlayer(this);
                }
            }
        }
        pItem->DeleteFromDB();
    }
}

void Player::DestroyItemCount( uint32 item, uint32 count, bool update )
{
    sLog.outDebug( "STORAGE: DestroyItemCount item = %u, count = %u", item, count);
    Item *pItem;
    ItemPrototype const *pProto;
    uint32 remcount = 0;

    // in inventory
    for(int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        pItem = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pItem && pItem->GetEntry() == item )
        {
            if( pItem->GetCount() + remcount <= count )
            {
                remcount += pItem->GetCount();
                DestroyItem( INVENTORY_SLOT_BAG_0, i, update);

                if(remcount >=count)
                    return;
            }
            else
            {
                pProto = pItem->GetProto();
                ItemRemovedQuestCheck( pItem->GetEntry(), count - remcount );
                pItem->SetCount( pItem->GetCount() - count + remcount );
                if( IsInWorld() & update )
                    pItem->SendUpdateToPlayer( this );
                pItem->SetState(ITEM_CHANGED, this);
                return;
            }
        }
    }

    // in inventory bags
    Bag *pBag;
    ItemPrototype const *pBagProto;
    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        pBag = (Bag*)GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pBag )
        {
            pBagProto = pBag->GetProto();
            if( pBagProto )
            {
                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                {
                    pItem = pBag->GetItemByPos(j);
                    if( pItem && pItem->GetEntry() == item )
                    {
                        if( pItem->GetCount() + remcount <= count )
                        {
                            remcount += pItem->GetCount();
                            DestroyItem( i, j, update );

                            if(remcount >=count)
                                return;
                        }
                        else
                        {
                            pProto = pItem->GetProto();
                            ItemRemovedQuestCheck( pItem->GetEntry(), count - remcount );
                            pItem->SetCount( pItem->GetCount() - count + remcount );
                            if( IsInWorld() && update )
                                pItem->SendUpdateToPlayer( this );
                            pItem->SetState(ITEM_CHANGED, this);
                            return;
                        }
                    }
                }
            }
        }
    }

    // in equipment and bag list
    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        pItem = GetItemByPos( INVENTORY_SLOT_BAG_0, i );
        if( pItem && pItem->GetEntry() == item )
        {
            if( pItem->GetCount() + remcount <= count )
            {
                remcount += pItem->GetCount();
                DestroyItem( INVENTORY_SLOT_BAG_0, i, update);

                if(remcount >=count)
                    return;
            }
            else
            {
                pProto = pItem->GetProto();
                ItemRemovedQuestCheck( pItem->GetEntry(), count - remcount );
                pItem->SetCount( pItem->GetCount() - count + remcount );
                if( IsInWorld() & update )
                    pItem->SendUpdateToPlayer( this );
                pItem->SetState(ITEM_CHANGED, this);
                return;
            }
        }
    }
}

void Player::DestroyItemCount( Item* pItem, uint32 &count, bool update )
{
    if(!pItem)
        return;

    sLog.outDebug( "STORAGE: DestroyItemCount item (GUID: %u, Entry: %u) count = %u", pItem->GetGUIDLow(),pItem->GetEntry(), count);

    if( pItem->GetCount() <= count )
    {
        count-= pItem->GetCount();

        uint16 pos = GetPosByGuid(pItem->GetGUID());
        DestroyItem( (pos >> 8),(pos & 255), update);
    }
    else
    {
        ItemPrototype const* pProto  = pItem->GetProto();
        ItemRemovedQuestCheck( pItem->GetEntry(), count);
        pItem->SetCount( pItem->GetCount() - count );
        count = 0;
        if( IsInWorld() & update )
            pItem->SendUpdateToPlayer( this );
        pItem->SetState(ITEM_CHANGED, this);
    }
}

void Player::SplitItem( uint16 src, uint16 dst, uint32 count )
{
    uint8 srcbag = src >> 8;
    uint8 srcslot = src & 255;

    uint8 dstbag = dst >> 8;
    uint8 dstslot = dst & 255;

    Item *pSrcItem = GetItemByPos( srcbag, srcslot );
    if( pSrcItem )
    {
        // not let split all items (can be only at cheating)
        if(pSrcItem->GetCount() == count)
        {
            SendEquipError( EQUIP_ERR_COULDNT_SPLIT_ITEMS, pSrcItem, NULL );
            return;
        }

        // not let split more existed items (can be only at cheating)
        if(pSrcItem->GetCount() < count)
        {
            SendEquipError( EQUIP_ERR_TRIED_TO_SPLIT_MORE_THAN_COUNT, pSrcItem, NULL );
            return;
        }

        sLog.outDebug( "STORAGE: SplitItem bag = %u, slot = %u, item = %u, count = %u", dstbag, dstslot, pSrcItem->GetEntry(), count);
        Item *pNewItem = CreateItem( pSrcItem->GetEntry(), count );
        if( pNewItem )
        {
            uint16 dest;
            uint8 msg;
            if( IsInventoryPos( dst ) )
            {
                msg = CanStoreItem( dstbag, dstslot, dest, pNewItem, false );
                if( msg == EQUIP_ERR_OK )
                {
                    pSrcItem->SetCount( pSrcItem->GetCount() - count );
                    if( IsInWorld() )
                        pSrcItem->SendUpdateToPlayer( this );
                    pSrcItem->SetState(ITEM_CHANGED, this);
                    StoreItem( dest, pNewItem, true);
                }
                else
                {
                    delete pNewItem;
                    SendEquipError( msg, pSrcItem, NULL );
                }
            }
            else if( IsBankPos ( dst ) )
            {
                msg = CanBankItem( dstbag, dstslot, dest, pNewItem, false );
                if( msg == EQUIP_ERR_OK )
                {
                    pSrcItem->SetCount( pSrcItem->GetCount() - count );
                    if( IsInWorld() )
                        pSrcItem->SendUpdateToPlayer( this );
                    pSrcItem->SetState(ITEM_CHANGED, this);
                    BankItem( dest, pNewItem, true);
                }
                else
                {
                    delete pNewItem;
                    SendEquipError( msg, pSrcItem, NULL );
                }
            }
            else if( IsEquipmentPos ( dst ) )
            {
                msg = CanEquipItem( dstslot, dest, pNewItem, false );
                if( msg == EQUIP_ERR_OK )
                {
                    pSrcItem->SetCount( pSrcItem->GetCount() - count );
                    if( IsInWorld() )
                        pSrcItem->SendUpdateToPlayer( this );
                    pSrcItem->SetState(ITEM_CHANGED, this);
                    EquipItem( dest, pNewItem, true);
                }
                else
                {
                    delete pNewItem;
                    SendEquipError( msg, pSrcItem, NULL );
                }
            }
            return;
        }
    }
    SendEquipError( EQUIP_ERR_ITEM_NOT_FOUND, pSrcItem, NULL );
}

void Player::SwapItem( uint16 src, uint16 dst )
{
    uint8 srcbag = src >> 8;
    uint8 srcslot = src & 255;

    uint8 dstbag = dst >> 8;
    uint8 dstslot = dst & 255;

    Item *pSrcItem = GetItemByPos( srcbag, srcslot );
    Item *pDstItem = GetItemByPos( dstbag, dstslot );

    if( pSrcItem )
    {
        sLog.outDebug( "STORAGE: SwapItem bag = %u, slot = %u, item = %u", dstbag, dstslot, pSrcItem->GetEntry());

        if(!isAlive() )
        {
            SendEquipError( EQUIP_ERR_YOU_ARE_DEAD, pSrcItem, pDstItem );
            return;
        }

        if(IsEquipmentPos ( src ))
        {
            uint8 msg = CanUnequipItem( src, pDstItem != NULL );
            if(msg != EQUIP_ERR_OK)
            {
                SendEquipError( msg, pSrcItem, pDstItem );
                return;
            }
        }

        if( srcslot == dstbag )
        {
            SendEquipError( EQUIP_ERR_NONEMPTY_BAG_OVER_OTHER_BAG, pSrcItem, pDstItem );
            return;
        }

        uint16 dest;
        uint8 msg;
        if( !pDstItem )
        {
            if( IsInventoryPos( dst ) )
            {
                msg = CanStoreItem( dstbag, dstslot, dest, pSrcItem, false );
                if( msg == EQUIP_ERR_OK )
                {
                    RemoveItem(srcbag, srcslot, true);
                    StoreItem( dest, pSrcItem, true);
                    return;
                }
                else
                    SendEquipError( msg, pSrcItem, NULL );
            }
            else if( IsBankPos ( dst ) )
            {
                msg = CanBankItem( dstbag, dstslot, dest, pSrcItem, false);
                if( msg == EQUIP_ERR_OK )
                {
                    RemoveItem(srcbag, srcslot, true);
                    BankItem( dest, pSrcItem, true);
                    return;
                }
                else
                    SendEquipError( msg, pSrcItem, NULL );
            }
            else if( IsEquipmentPos ( dst ) )
            {
                msg = CanEquipItem( dstslot, dest, pSrcItem, false );
                if( msg == EQUIP_ERR_OK )
                {
                    RemoveItem(srcbag, srcslot, true);
                    EquipItem( dest, pSrcItem, true);
                    return;
                }
                else
                    SendEquipError( msg, pSrcItem, NULL );
            }
        }
        else
        {
            if( IsInventoryPos( dst ) )
            {
                if( CanStoreItem( dstbag, dstslot, dest, pSrcItem, false ) == EQUIP_ERR_OK )
                {
                    if( pSrcItem->GetCount() + pDstItem->GetCount() <= pSrcItem->GetProto()->Stackable )
                    {
                        RemoveItem(srcbag, srcslot, true);
                        StoreItem( dest, pSrcItem, true);
                    }
                    else
                    {
                        pSrcItem->SetCount( pSrcItem->GetCount() + pDstItem->GetCount() - pSrcItem->GetProto()->Stackable );
                        pDstItem->SetCount( pSrcItem->GetProto()->Stackable );
                        pSrcItem->SetState(ITEM_CHANGED, this);
                        pDstItem->SetState(ITEM_CHANGED, this);
                        if( IsInWorld() )
                        {
                            pSrcItem->SendUpdateToPlayer( this );
                            pDstItem->SendUpdateToPlayer( this );
                        }
                    }
                    return;
                }
            }
            else if( IsBankPos ( dst ) )
            {
                if( CanBankItem( dstbag, dstslot, dest, pSrcItem, false ) == EQUIP_ERR_OK )
                {
                    if( pSrcItem->GetCount() + pDstItem->GetCount() <= pSrcItem->GetProto()->Stackable )
                    {
                        RemoveItem(srcbag, srcslot, true);
                        BankItem( dest, pSrcItem, true);
                    }
                    else
                    {
                        pSrcItem->SetCount( pSrcItem->GetCount() + pDstItem->GetCount() - pSrcItem->GetProto()->Stackable );
                        pDstItem->SetCount( pSrcItem->GetProto()->Stackable );
                        pSrcItem->SetState(ITEM_CHANGED, this);
                        pDstItem->SetState(ITEM_CHANGED, this);
                        if( IsInWorld() )
                        {
                            pSrcItem->SendUpdateToPlayer( this );
                            pDstItem->SendUpdateToPlayer( this );
                        }
                    }
                    return;
                }
            }
            else if( IsEquipmentPos ( dst ) )
            {
                if( CanEquipItem( dstslot, dest, pSrcItem, false ) == EQUIP_ERR_OK )
                {
                    if( pSrcItem->GetCount() + pDstItem->GetCount() <= pSrcItem->GetProto()->Stackable )
                    {
                        RemoveItem(srcbag, srcslot, true);
                        EquipItem( dest, pSrcItem, true);
                    }
                    else
                    {
                        pSrcItem->SetCount( pSrcItem->GetCount() + pDstItem->GetCount() - pSrcItem->GetProto()->Stackable );
                        pDstItem->SetCount( pSrcItem->GetProto()->Stackable );
                        pSrcItem->SetState(ITEM_CHANGED, this);
                        pDstItem->SetState(ITEM_CHANGED, this);
                        if( IsInWorld() )
                        {
                            pSrcItem->SendUpdateToPlayer( this );
                            pDstItem->SendUpdateToPlayer( this );
                        }
                    }
                    return;
                }
            }
            if( IsInventoryPos( dst ) )
                msg = CanStoreItem( dstbag, dstslot, dest, pSrcItem, true );
            else if( IsBankPos( dst ) )
                msg = CanBankItem( dstbag, dstslot, dest, pSrcItem, true );
            else if( IsEquipmentPos( dst ) )
            {
                msg = CanEquipItem( dstslot, dest, pSrcItem, true );
                if( msg == EQUIP_ERR_OK )
                    msg = CanUnequipItem( dest, true );
            }

            if( msg == EQUIP_ERR_OK )
            {
                uint16 dest2;
                if( IsInventoryPos( src ) )
                {
                    msg = CanStoreItem( srcbag, srcslot, dest2, pDstItem, true );
                    if( msg != EQUIP_ERR_OK )
                    {
                        SendEquipError( EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG, pSrcItem, pDstItem );
                        return;
                    }
                }
                else if( IsBankPos( src ) )
                {
                    msg = CanBankItem( srcbag, srcslot, dest2, pDstItem, true );
                    if( msg != EQUIP_ERR_OK )
                    {
                        SendEquipError( EQUIP_ERR_ITEM_DOESNT_GO_INTO_BAG, pSrcItem, pDstItem );
                        return;
                    }
                }
                else if( IsEquipmentPos( src ) )
                {
                    msg = CanEquipItem( srcslot, dest2, pDstItem, true);
                    if( msg == EQUIP_ERR_OK )
                        msg = CanUnequipItem( dest2, true);

                    if( msg != EQUIP_ERR_OK )
                    {
                        SendEquipError( EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT, pSrcItem, pDstItem );
                        return;
                    }
                }
                RemoveItem(dstbag, dstslot, false);
                RemoveItem(srcbag, srcslot, false);
                if( IsInventoryPos( dst ) )
                    StoreItem(dest, pSrcItem, true);
                else if( IsBankPos( dst ) )
                    BankItem(dest, pSrcItem, true);
                else if( IsEquipmentPos( dst ) )
                    EquipItem(dest, pSrcItem, true);
                if( IsInventoryPos( src ) )
                    StoreItem(dest2, pDstItem, true);
                else if( IsBankPos( src ) )
                    BankItem(dest2, pDstItem, true);
                else if( IsEquipmentPos( src ) )
                    EquipItem(dest2, pDstItem, true);
                return;
            }
            else
                SendEquipError( msg, pSrcItem, pDstItem );
            return;
        }
    }
}

void Player::AddItemToBuyBackSlot( uint32 slot, Item *pItem )
{
    if( pItem )
    {
        if( slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END )
        {
            RemoveItemFromBuyBackSlot( slot );
            sLog.outDebug( "STORAGE: AddItemToBuyBackSlot item = %u, slot = %u", pItem->GetEntry(), slot);
            uint32 eslot = slot - BUYBACK_SLOT_START;

            m_buybackitems[eslot] = pItem;
            time_t base = time(NULL);
            time_t etime = base + (30 * 3600);

            SetUInt64Value( PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + eslot * 2, pItem->GetGUID() );
            ItemPrototype const *pProto = pItem->GetProto();
            if( pProto )
                SetUInt32Value( PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, pProto->SellPrice * pItem->GetCount() );
            else
                SetUInt32Value( PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, 0 );
            SetUInt32Value( PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + eslot, (uint32)etime );
        }
    }
}

Item* Player::GetItemFromBuyBackSlot( uint32 slot )
{
    sLog.outDebug( "STORAGE: GetItemFromBuyBackSlot slot = %u", slot);
    if( slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END )
        return m_buybackitems[slot - BUYBACK_SLOT_START];
    return NULL;
}

void Player::RemoveItemFromBuyBackSlot( uint32 slot )
{
    sLog.outDebug( "STORAGE: RemoveItemFromBuyBackSlot slot = %u", slot);
    if( slot >= BUYBACK_SLOT_START && slot < BUYBACK_SLOT_END )
    {
        uint32 eslot = slot - BUYBACK_SLOT_START;
        Item *pItem = m_buybackitems[eslot];
        if( pItem )
            pItem->RemoveFromWorld();

        m_buybackitems[eslot] = NULL;
        SetUInt64Value( PLAYER_FIELD_VENDORBUYBACK_SLOT_1 + eslot * 2, 0 );
        SetUInt32Value( PLAYER_FIELD_BUYBACK_PRICE_1 + eslot, 0 );
        SetUInt32Value( PLAYER_FIELD_BUYBACK_TIMESTAMP_1 + eslot, 0 );
    }
}

void Player::SendEquipError( uint8 msg, Item* pItem, Item *pItem2 )
{
    sLog.outDetail( "WORLD: Sent SMSG_INVENTORY_CHANGE_FAILURE" );
    WorldPacket data;
    data.Initialize( SMSG_INVENTORY_CHANGE_FAILURE );
    data << msg;
    if( msg == EQUIP_ERR_YOU_MUST_REACH_LEVEL_N )
        data << (pItem && pItem->GetProto() ? pItem->GetProto()->RequiredLevel : uint32(0));
    data << (pItem ? pItem->GetGUID() : uint64(0));
    data << (pItem2 ? pItem2->GetGUID() : uint64(0));
    data << uint8(0);
    GetSession()->SendPacket(&data);
}

void Player::SendBuyError( uint8 msg, Creature* pCreature, uint32 item, uint32 param )
{
    sLog.outDetail( "WORLD: Sent SMSG_BUY_FAILED" );
    WorldPacket data;
    data.Initialize( SMSG_BUY_FAILED );
    data << (pCreature ? pCreature->GetGUID() : uint64(0));
    data << item;
    if( param > 0 )
        data << param;
    data << msg;
    GetSession()->SendPacket(&data);
}

void Player::SendSellError( uint8 msg, Creature* pCreature, uint64 guid, uint32 param )
{
    sLog.outDetail( "WORLD: Sent SMSG_SELL_ITEM" );
    WorldPacket data;
    data.Initialize( SMSG_SELL_ITEM );
    data << (pCreature ? pCreature->GetGUID() : uint64(0));
    data << guid;
    if( param > 0 )
        data << param;
    data << msg;
    GetSession()->SendPacket(&data);
}

void Player::ClearTrade()
{
    tradeGold = 0;
    acceptTrade = false;
    for(int i=0; i<7; i++)
        tradeItems[i] = NULL_SLOT;
}

void Player::TradeCancel(bool sendback)
{
    if(pTrader)
    {
        // prevent loop cancel message (already processed)
        if(!sendback)
            pTrader->pTrader = NULL;

        WorldSession* ws = pTrader->GetSession();
        pTrader = NULL;
        ws->SendCancelTrade();
    }
    ClearTrade();
}

void Player::UpdateEnchantTime(uint32 time)
{
    for(std::list<struct EnchantDuration*>::iterator itr = m_enchantDuration.begin(),next;itr != m_enchantDuration.end();itr=next)
    {
        next=itr;
        next++;
        if(*itr)
        {
            if((*itr)->item)
            {
                if(!(*itr)->item->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+(*itr)->slot*3))
                {
                    delete *itr;
                    m_enchantDuration.erase(itr);
                    continue;
                }
                if((*itr)->leftduration <= time)
                {
                    AddItemEnchant((*itr)->item,(*itr)->item->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+(*itr)->slot*3),false);
                    for(int y=0;y<3;y++)
                        (*itr)->item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT+(*itr)->slot*3+y,0);
                    delete *itr;
                    m_enchantDuration.erase(itr);
                    continue;
                }
                else if((*itr)->leftduration > time)
                {
                    (*itr)->leftduration -= time;
                }
            }
        }
    }
}

// duration == 0 will remove item enchant
void Player::AddEnchantDuration(Item *item,uint32 slot,uint32 duration)
{
    if(!item)
        return;
    for(std::list<struct EnchantDuration*>::iterator itr = m_enchantDuration.begin(),next;itr != m_enchantDuration.end();itr=next)
    {
        next=itr;
        next++;
        if(*itr)
        {
            if((*itr)->item)
            {
                if((*itr)->item == item && (*itr)->slot == slot)
                {
                    delete *itr;
                    m_enchantDuration.erase(itr);
                    break;
                }
            }
        }
    }
    if(item && duration > 0 )
    {
        GetSession()->SendItemEnchantTimeUpdate(item->GetGUID(),slot,uint32(duration/1000));
        EnchantDuration *ed = new EnchantDuration();
        ed->item = item;
        ed->leftduration = duration;
        ed->slot = slot;
        m_enchantDuration.push_back(ed);
    }
}

void Player::ReducePoisonCharges(uint32 enchantId)
{
    if(!enchantId)
        return;
    uint32 pEnchantId = 0;
    uint32 charges = 0;
    Item *pItem;
    uint16 pos;

    for(int i = EQUIPMENT_SLOT_MAINHAND; i < EQUIPMENT_SLOT_RANGED; i++)
    {
        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);

        pItem = GetItemByPos( pos );
        if(!pItem)
            continue;
        for(int x=0;x<7;x++)
        {
            charges = pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+x*3+2);
            if(charges == 0)
                continue;
            if(charges <= 1)
            {
                AddItemEnchant(pItem,enchantId,false);
                for(int y=0;y<3;y++)
                    pItem->SetUInt32Value(ITEM_FIELD_ENCHANTMENT+x*3+y,0);
                break;
            }
            else
            {
                pItem->SetUInt32Value(ITEM_FIELD_ENCHANTMENT+x*3+2,charges-1);
                break;
            }
        }
    }
}

void Player::SaveEnchant()
{
    uint32 duration = 0;

    for(std::list<struct EnchantDuration*>::iterator itr = m_enchantDuration.begin();itr != m_enchantDuration.end();itr++)
    {
        if(*itr)
        {
            if((*itr)->item)
            {
                if((*itr)->leftduration > 0)
                {
                    (*itr)->item->SetUInt32Value(ITEM_FIELD_ENCHANTMENT+(*itr)->slot*3+1,(*itr)->leftduration);
                    (*itr)->item->SetState(ITEM_CHANGED, this);
                }
            }
        }
    }
}

void Player::LoadEnchant()
{
    uint32 duration = 0;
    Item *pItem;
    uint16 pos;

    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; i++)
    {
        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);

        pItem = GetItemByPos( pos );
        if(!pItem)
            continue;
        if(pItem->GetProto()->Class != ITEM_CLASS_WEAPON && pItem->GetProto()->Class != ITEM_CLASS_ARMOR)
            continue;
        for(int x=0;x<7;x++)
        {
            duration = pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+x*3+1);
            if( duration == 0 )
                continue;
            else if( duration > 0 )
                AddEnchantDuration(pItem,x,duration);
        }
    }
    Bag *pBag;
    ItemPrototype const *pBagProto;
    for(int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
    {
        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        pBag = (Bag*)GetItemByPos( pos );
        if( pBag )
        {
            pBagProto = pBag->GetProto();
            if( pBagProto )
            {
                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                {
                    pos = ((i << 8) | j);
                    pItem = GetItemByPos( pos );
                    if(!pItem)
                        continue;
                    if(pItem->GetProto()->Class != ITEM_CLASS_WEAPON && pItem->GetProto()->Class != ITEM_CLASS_ARMOR)
                        continue;
                    for(int x=0;x<7;x++)
                    {
                        duration = pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+x*3+1);
                        if( duration == 0 )
                            continue;
                        else if( duration > 0 )
                            AddEnchantDuration(pItem,x,duration);
                    }
                }
            }
        }
    }
    for(int i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; i++)
    {
        pos = ((INVENTORY_SLOT_BAG_0 << 8) | i);
        pBag = (Bag*)GetItemByPos( pos );
        if( pBag )
        {
            pBagProto = pBag->GetProto();
            if( pBagProto )
            {
                for(uint32 j = 0; j < pBagProto->ContainerSlots; j++)
                {
                    pos = ((i << 8) | j);
                    pItem = GetItemByPos( pos );
                    if(!pItem)
                        continue;
                    if(pItem->GetProto()->Class != ITEM_CLASS_WEAPON && pItem->GetProto()->Class != ITEM_CLASS_ARMOR)
                        continue;
                    for(int x=0;x<7;x++)
                    {
                        duration = pItem->GetUInt32Value(ITEM_FIELD_ENCHANTMENT+x*3+1);
                        if( duration == 0 )
                            continue;
                        else if( duration > 0 )
                            AddEnchantDuration(pItem,x,duration);
                    }
                }
            }
        }
    }

}

/*********************************************************/
/***                    QUEST SYSTEM                   ***/
/*********************************************************/

void Player::PrepareQuestMenu( uint64 guid )
{
    Object *pObject;
    Creature *pCreature = ObjectAccessor::Instance().GetCreature(*this, guid);
    if( pCreature )
        pObject = (Object*)pCreature;
    else
    {
        GameObject *pGameObject = ObjectAccessor::Instance().GetGameObject(*this, guid);
        if( pGameObject )
            pObject = (Object*)pGameObject;
        else
            return;
    }

    QuestMenu *qm = PlayerTalkClass->GetQuestMenu();
    qm->ClearMenu();

    for( std::list<uint32>::iterator i = pObject->mInvolvedQuests.begin( ); i != pObject->mInvolvedQuests.end( ); i++ )
    {
        uint32 quest_id = *i;
        uint32 status = GetQuestStatus( quest_id );
        if ( status == QUEST_STATUS_COMPLETE && !GetQuestRewardStatus( quest_id ) )
            qm->AddMenuItem(quest_id, DIALOG_STATUS_REWARD_REP);
        else if ( status == QUEST_STATUS_INCOMPLETE )
            qm->AddMenuItem(quest_id, DIALOG_STATUS_INCOMPLETE);
        else if (status == QUEST_STATUS_AVAILABLE )
            qm->AddMenuItem(quest_id, DIALOG_STATUS_CHAT);
    }

    for( std::list<uint32>::iterator i = pObject->mQuests.begin( ); i != pObject->mQuests.end( ); i++ )
    {
        uint32 quest_id = *i;
        Quest* pQuest = objmgr.QuestTemplates[quest_id];
        
        uint32 status = GetQuestStatus( quest_id );

        if ((strlen(pQuest->GetObjectives()) == 0) && (CanTakeQuest(pQuest, false)))
        {
            // perhaps find a better check for quests that autocomplete
            qm->AddMenuItem(quest_id, DIALOG_STATUS_REWARD_REP);
        } else if ( status == QUEST_STATUS_NONE && CanTakeQuest( pQuest, false ) )
            qm->AddMenuItem(quest_id, DIALOG_STATUS_AVAILABLE);
    }
}

void Player::SendPreparedQuest( uint64 guid )
{
    QuestMenu* pQuestMenu = PlayerTalkClass->GetQuestMenu();
    if( !pQuestMenu || pQuestMenu->MenuItemCount() < 1 )
        return;

    uint32 status = pQuestMenu->GetItem(0).m_qIcon;
    if ( pQuestMenu->MenuItemCount() == 1 )
    {
        // Auto open -- maybe also should verify there is no greeting
        uint32 quest_id = pQuestMenu->GetItem(0).m_qId;
        Quest *pQuest = objmgr.QuestTemplates[quest_id];
        if ( pQuest )
        {
            if( status == DIALOG_STATUS_REWARD_REP && !GetQuestRewardStatus( quest_id ) )
                PlayerTalkClass->SendQuestGiverRequestItems( pQuest, guid, CanRewardQuest(pQuest,false), true );
            else if( status == DIALOG_STATUS_INCOMPLETE )
                PlayerTalkClass->SendQuestGiverRequestItems( pQuest, guid, false, true );
            else
            {
                // perhaps find a better auto-complete test
                if ((strlen(pQuest->GetObjectives()) == 0) && (CanTakeQuest(pQuest, false)))
                {
                    //if (CanCompleteQuest(quest_id))
                    //    PlayerTalkClass->SendQuestGiverOfferReward(quest_id, guid, true, NULL, 0);
                    //else
                    PlayerTalkClass->SendQuestGiverRequestItems( pQuest, guid, CanCompleteQuest(quest_id), true);
                }
                else
                {
                    PlayerTalkClass->SendQuestGiverQuestDetails( pQuest, guid, true );
                }
            }
        }
    }
    else
    {
        QEmote qe;
        qe._Delay = 0;
        qe._Emote = 0;
        std::string title = "";
        Creature *pCreature = ObjectAccessor::Instance().GetCreature(*this, guid);
        if( pCreature )
        {
            uint32 textid = pCreature->GetNpcTextId();
            GossipText * gossiptext = objmgr.GetGossipText(textid);
            if( !gossiptext )
            {
                qe._Delay = 0; //TEXTEMOTE_MESSAGE;              //zyg: player emote
                qe._Emote = 0; //TEXTEMOTE_HELLO;                //zyg: NPC emote
                title = "";
            }
            else
            {
                qe = gossiptext->Options[0].Emotes[0];
                title = gossiptext->Options[0].Text_0;
                if( &title == NULL )
                    title = "";
            }
        }
        PlayerTalkClass->SendQuestGiverQuestList( qe, title, guid );
    }
}

Quest *Player::GetActiveQuest( uint32 quest_id ) const
{
    StatusMap::const_iterator itr = mQuestStatus.find(quest_id);

    return itr != mQuestStatus.end() && itr->second.m_status != QUEST_STATUS_NONE ?  itr->second.m_quest : NULL;
}

Quest* Player::GetNextQuest( uint64 guid, Quest *pQuest )
{
    if( pQuest )
    {
        Object *pObject;
        Creature *pCreature = ObjectAccessor::Instance().GetCreature(*this, guid);
        if( pCreature ) {
            pObject = (Object*)pCreature;
        }
        else
        {
            GameObject *pGameObject = ObjectAccessor::Instance().GetGameObject(*this, guid);
            if( pGameObject )
                pObject = (Object*)pGameObject;
            else
                return NULL;
        }

        uint32 nextQuestID = pQuest->GetNextQuestId();
        list<uint32>::iterator iter = find(pObject->mQuests.begin(), pObject->mQuests.end(), nextQuestID);
        if (iter != pObject->mQuests.end()) {
            return objmgr.QuestTemplates[nextQuestID];
        }
    }
    return NULL;
}

bool Player::CanSeeStartQuest( uint32 quest_id )
{
    if( quest_id )
    {
        if( SatisfyQuestRace( quest_id, false ) && SatisfyQuestClass( quest_id, false ) && SatisfyQuestExclusiveGroup( quest_id, false )
            && SatisfyQuestSkill( quest_id, false ) && SatisfyQuestReputation( quest_id, false )
            && SatisfyQuestPreviousQuest( quest_id, false ) && SatisfyQuestHaveQuest(quest_id, false) )
            return ( getLevel() + 7 >= objmgr.QuestTemplates[quest_id]->GetMinLevel() );
    }
    return false;
}

bool Player::CanTakeQuest( Quest *pQuest, bool msg )
{
    if( pQuest)
    {
        uint32 quest_id = pQuest->GetQuestId();
        return ( SatisfyQuestStatus( quest_id, msg ) && SatisfyQuestExclusiveGroup( quest_id, msg )
            && SatisfyQuestRace( quest_id, msg ) && SatisfyQuestLevel( quest_id, msg ) && SatisfyQuestClass( quest_id, msg )
            && SatisfyQuestSkill( quest_id, msg ) && SatisfyQuestReputation( quest_id, msg )
            && SatisfyQuestPreviousQuest( quest_id, msg ) && SatisfyQuestTimed( quest_id, msg )
            && SatisfyQuestHaveQuest( quest_id, msg ) );
    }
    return false;
}

bool Player::CanAddQuest( Quest *pQuest, bool msg )
{
    if( pQuest )
    {
        if(!GetQuestSlot( 0 ))
            return false;

        if( !SatisfyQuestLog( msg ) )
            return false;

        uint32 srcitem = pQuest->GetSrcItemId();
        if( srcitem > 0 )
        {
            uint32 count = pQuest->GetSrcItemCount();
            uint16 dest;
            if( count <= 0 )
                count = 1;
            uint8 msg = CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, srcitem, count, false );
            if( msg != EQUIP_ERR_OK )
            {
                SendEquipError( msg, NULL, NULL );
                return false;
            }
        }
        return true;
    }
    return false;
}

bool Player::CanCompleteQuest( uint32 quest_id )
{
    if( quest_id )
    {
        QuestStatus qStatus = mQuestStatus[quest_id].m_status;
        if( qStatus == QUEST_STATUS_COMPLETE )
            return true;

        Quest* qInfo = objmgr.QuestTemplates[quest_id];

        if ((mQuestStatus[quest_id].m_status == QUEST_STATUS_INCOMPLETE) ||
            (strlen(qInfo->GetObjectives()) == 0))
        {

            //if ( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_DELIVER ) )
            //{
            for(int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
            {
                if( qInfo->ReqItemCount[i]!= 0 && mQuestStatus[quest_id].m_itemcount[i] < qInfo->ReqItemCount[i] )
                // Need to do something so repeatable quests can be counted:
                //if (GetItemCount(qInfo->ReqItemId[i]) < qInfo->ReqItemCount[i])
                    return false;
            }
            //}

            if ( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_KILL_OR_CAST ) )
            {
                for(int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
                {
                    // skip GO activate objectives
                    if( qInfo->ReqCreatureOrGOId[i] <= 0 )
                        continue;

                    if( qInfo->ReqCreatureOrGOCount[i] != 0 && mQuestStatus[quest_id].m_creatureOrGOcount[i] < qInfo->ReqCreatureOrGOCount[i] )
                        return false;
                }
            }

            if ( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_EXPLORATION ) && !mQuestStatus[quest_id].m_explored )
                return false;

            if ( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_TIMED ) && mQuestStatus[quest_id].m_timer == 0 )
                return false;

            if ( qInfo->GetRewOrReqMoney() < 0 )
            {
                if ( int32(GetMoney()) < -qInfo->GetRewOrReqMoney() )
                    return false;
            }
            return true;
        }
    }
    return false;
}

bool Player::CanRewardQuest( Quest *pQuest, bool msg )
{
    if( pQuest )
    {
        // auto complete quest, return true (maybe need a better check)
        if ((strlen(pQuest->GetObjectives()) == 0) && CanTakeQuest(pQuest, false))
            return true;

        // not completed quest (only cheating case, then ignore without message)
        if(GetQuestStatus(pQuest->GetQuestId()) != QUEST_STATUS_COMPLETE)
            return false;

        // rewarded and not repeatable quest (only cheating case, then ignore without message)
        if(GetQuestRewardStatus(pQuest->GetQuestId()) && !pQuest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_REPEATABLE))
            return false;

        // prevent recive reward with quest items in bank
        if ( pQuest->HasSpecialFlag( QUEST_SPECIAL_FLAGS_DELIVER ) )
        {
            for(int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
            {
                if( pQuest->ReqItemCount[i]!= 0 &&
                    GetItemCount(pQuest->ReqItemId[i]) < pQuest->ReqItemCount[i] )
                {
                    if(msg)
                        SendEquipError( EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL );
                    return false;
                }
            }
        }

        return true;
    }
    return false;
}

bool Player::CanRewardQuest( Quest *pQuest, uint32 reward, bool msg )
{
    if( pQuest )
    {
        // prevent recive reward with quest items in bank or for not completed quest
        if(!CanRewardQuest(pQuest,msg))
            return false;

        uint16 dest;
        uint8 msg;

        if ( pQuest->GetRewChoiceItemsCount() > 0 )
        {
            if( pQuest->RewChoiceItemId[reward] )
            {
                msg = CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, pQuest->RewChoiceItemId[reward], pQuest->RewChoiceItemCount[reward], false );
                if( msg != EQUIP_ERR_OK )
                {
                    SendEquipError( msg, NULL, NULL );
                    return false;
                }
            }
        }

        if ( pQuest->GetRewItemsCount() > 0 )
        {
            for (int i = 0; i < pQuest->GetRewItemsCount(); i++)
            {
                if( pQuest->RewItemId[i] )
                {
                    msg = CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, pQuest->RewItemId[i], pQuest->RewItemCount[i], false );
                    if( msg != EQUIP_ERR_OK )
                    {
                        SendEquipError( msg, NULL, NULL );
                        return false;
                    }
                }
            }
        }
        return true;
    }
    return false;
}

void Player::AddQuest( Quest *pQuest )
{
    if( pQuest )
    {
        uint16 log_slot = GetQuestSlot( 0 );
        assert(log_slot);

        uint32 quest_id = pQuest->GetQuestId();
        
        mQuestStatus[quest_id].m_quest = pQuest;
        mQuestStatus[quest_id].m_status = QUEST_STATUS_INCOMPLETE;
        mQuestStatus[quest_id].m_rewarded = false;
        mQuestStatus[quest_id].m_explored = false;

        if ( pQuest->HasSpecialFlag( QUEST_SPECIAL_FLAGS_DELIVER ) )
        {
            for(int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
                mQuestStatus[quest_id].m_itemcount[i] = 0;
        }
        if ( pQuest->HasSpecialFlag( QUEST_SPECIAL_FLAGS_KILL_OR_CAST ) )
        {
            for(int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
                mQuestStatus[quest_id].m_creatureOrGOcount[i] = 0;
        }

        GiveQuestSourceItem( quest_id );
        AdjustQuestReqItemCount( quest_id );

        SetUInt32Value(log_slot + 0, quest_id);
        SetUInt32Value(log_slot + 1, 0);

        if( pQuest->HasSpecialFlag( QUEST_SPECIAL_FLAGS_TIMED ) )
        {
            uint32 limittime = pQuest->GetLimitTime();
            AddTimedQuest( quest_id );
            mQuestStatus[quest_id].m_timer = limittime * 1000;
            uint32 qtime = static_cast<uint32>(time(NULL)) + limittime;
            SetUInt32Value( log_slot + 2, qtime );
        }
        else
        {
            mQuestStatus[quest_id].m_timer = 0;
            SetUInt32Value( log_slot + 2, 0 );
        }
    }
}

void Player::CompleteQuest( uint32 quest_id )
{
    if( quest_id )
    {
        SetQuestStatus( quest_id, QUEST_STATUS_COMPLETE);

        uint16 log_slot = GetQuestSlot( quest_id );
        if( log_slot )
        {
            uint32 state = GetUInt32Value( log_slot + 1 );
            state |= 1 << 24;
            SetUInt32Value( log_slot + 1, state );
        }

        SendQuestComplete( quest_id );
    }
}

void Player::IncompleteQuest( uint32 quest_id )
{
    if( quest_id )
    {
        SetQuestStatus( quest_id, QUEST_STATUS_INCOMPLETE );

        uint16 log_slot = GetQuestSlot( quest_id );
        if( log_slot )
        {
            uint32 state = GetUInt32Value( log_slot + 1 );
            state &= ~(1 << 24);
            SetUInt32Value( log_slot + 1, state );
        }
    }
}

void Player::RewardQuest( Quest *pQuest, uint32 reward, Object* questGiver )
{
    if( pQuest )
    {
        uint32 quest_id = pQuest->GetQuestId();
        
        uint16 dest;
        for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++ )
        {
            if ( pQuest->ReqItemId[i] )
                DestroyItemCount( pQuest->ReqItemId[i], pQuest->ReqItemCount[i], true);
        }

        //if( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_TIMED ) )
        //    SetTimedQuest( 0 );
        if (find(m_timedquests.begin(), m_timedquests.end(), pQuest->GetQuestId()) != m_timedquests.end())
            m_timedquests.remove(pQuest->GetQuestId());

        if ( pQuest->GetRewChoiceItemsCount() > 0 )
        {
            if( pQuest->RewChoiceItemId[reward] )
            {
                if( CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, pQuest->RewChoiceItemId[reward], pQuest->RewChoiceItemCount[reward], false ) == EQUIP_ERR_OK )
                    StoreNewItem( dest, pQuest->RewChoiceItemId[reward], pQuest->RewChoiceItemCount[reward], true);
            }
        }

        if ( pQuest->GetRewItemsCount() > 0 )
        {
            for (int i=0; i < pQuest->GetRewItemsCount(); i++)
            {
                if( pQuest->RewItemId[i] )
                {
                    if( CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, pQuest->RewItemId[i], pQuest->RewItemCount[i], false ) == EQUIP_ERR_OK )
                        StoreNewItem( dest, pQuest->RewItemId[i], pQuest->RewItemCount[i], true);
                }
            }
        }

        if( pQuest->GetRewSpell() > 0 )
            CastSpell( this, pQuest->GetRewSpell(), true);

        uint16 log_slot = GetQuestSlot( quest_id );
        if( log_slot )
        {
            SetUInt32Value(log_slot + 0, 0);
            SetUInt32Value(log_slot + 1, 0);
            SetUInt32Value(log_slot + 2, 0);
        }

        // Not give XP in case already completed once repeatable quest
        uint32 XP = mQuestStatus[quest_id].m_completed_once && pQuest->HasSpecialFlag( QUEST_SPECIAL_FLAGS_REPEATABLE )
            ? 0 : uint32(pQuest->XPValue( this )*sWorld.getRate(RATE_XP_QUEST));

        if ( getLevel() < sWorld.getConfig(CONFIG_MAX_PLAYER_LEVEL) )
            GiveXP( XP , NULL );
        else
            ModifyMoney( MaNGOS::XP::xp_to_money(XP) );

        ModifyMoney( pQuest->GetRewOrReqMoney() );

        if ( !pQuest->IsRepeatable() ) {
            SetQuestStatus(quest_id, QUEST_STATUS_COMPLETE);
            mQuestStatus[quest_id].m_rewarded = true;
        }
        else
        {
            SetQuestStatus(quest_id, QUEST_STATUS_NONE);
        }

        SendQuestReward( pQuest, XP, questGiver );
        mQuestStatus[quest_id].m_completed_once = true;     // in repeated quest case prevent recive XP at second complete
    }
}

void Player::FailQuest( uint32 quest_id )
{
    if( quest_id )
    {
        IncompleteQuest( quest_id );

        uint16 log_slot = GetQuestSlot( quest_id );
        if( log_slot )
        {
            SetUInt32Value( log_slot + 2, 1 );

            uint32 state = GetUInt32Value( log_slot + 1 );
            state |= 1 << 25;
            SetUInt32Value( log_slot + 1, state );
        }
        SendQuestFailed( quest_id );
    }
}

void Player::FailTimedQuest( uint32 quest_id )
{
    if( quest_id )
    {
        mQuestStatus[quest_id].m_timer = 0;

        IncompleteQuest( quest_id );

        uint16 log_slot = GetQuestSlot( quest_id );
        if( log_slot )
        {
            SetUInt32Value( log_slot + 2, 1 );

            uint32 state = GetUInt32Value( log_slot + 1 );
            state |= 1 << 25;
            SetUInt32Value( log_slot + 1, state );
        }
        SendQuestTimerFailed( quest_id );
    }
}

bool Player::SatisfyQuestClass( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        uint32 reqclasses = qInfo->GetRequiredClass();
        if ( reqclasses == 0 )
            return true;
        if( (reqclasses & getClassMask()) == 0 )
        {
            if( msg )
                SendCanTakeQuestResponse( INVALIDREASON_DONT_HAVE_REQ );
            return false;
        }
        return true;

    }
    return false;
}

bool Player::SatisfyQuestLevel( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        if( getLevel() < qInfo->GetMinLevel() )
        {
            if( msg )
                SendCanTakeQuestResponse( INVALIDREASON_DONT_HAVE_REQ );
            return false;
        }
        return true;
    }
    return false;
}

bool Player::SatisfyQuestLog( bool msg )
{
    uint16 log_slot = GetQuestSlot( 0 );
    if( log_slot )
        return true;
    else
    {
        if( msg )
        {
            WorldPacket data;
            data.Initialize( SMSG_QUESTLOG_FULL );
            GetSession()->SendPacket( &data );
            sLog.outDebug( "WORLD: Sent QUEST_LOG_FULL_MESSAGE" );
        }
        return false;
    }
}

bool Player::SatisfyQuestPreviousQuest( uint32 quest_id, bool msg )
{
    if( quest_id)
    {
        Quest * qInfo = objmgr.QuestTemplates[quest_id];

        // No previous quest (might be first quest in a series)
        if( qInfo->prevQuests.size() == 0)
            return true;

        // If any of the previous quests completed, return true
        for(vector<uint32>::iterator iter = qInfo->prevQuests.begin(); iter != qInfo->prevQuests.end(); iter++ )
        {
            uint32 prevId = *iter;

            StatusMap::iterator i_prevstatus = mQuestStatus.find( prevId );

            if( i_prevstatus != mQuestStatus.end() && i_prevstatus->second.m_rewarded )
                return true;
        }

        // Have only prev. quests in non-rewarded state
        if( msg )
            SendCanTakeQuestResponse( INVALIDREASON_DONT_HAVE_REQ );
    }
    return false;
}

bool Player::SatisfyQuestHaveQuest( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if(!qInfo)
        return false;

    if (!qInfo->GetHaveQuestId())
        return true;

    StatusMap::iterator iter = mQuestStatus.find(qInfo->GetHaveQuestId());
    if (iter == mQuestStatus.end())
        return false;

    if (iter->second.m_status == QUEST_STATUS_NONE)
        return false;

    return true;
}

bool Player::SatisfyQuestRace( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        uint32 reqraces = qInfo->GetRequiredRaces();
        if ( reqraces == 0 )
            return true;
        if( (reqraces & getRaceMask()) == 0 )
        {
            if( msg )
                SendCanTakeQuestResponse( INVALIDREASON_DONT_HAVE_RACE );
            return false;
        }
        return true;
    }
    return false;
}

bool Player::SatisfyQuestReputation( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        uint32 faction_id = qInfo->GetRequiredRepFaction();
        if(!faction_id)
            return true;

        return GetStanding(faction_id) >= qInfo->GetRequiredRepValue();
    }
    return false;
}

bool Player::SatisfyQuestSkill( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        uint32 reqskill = qInfo->GetRequiredSkill();
        if( reqskill == QUEST_TRSKILL_NONE )
            return true;
        if( GetSkillValue( reqskill ) < qInfo->GetRequiredSkillValue() )
        {
            if( msg )
                SendCanTakeQuestResponse( INVALIDREASON_DONT_HAVE_REQ );
            return false;
        }
        return true;
    }
    return false;
}

bool Player::SatisfyQuestStatus( uint32 quest_id, bool msg )
{
    if( quest_id )
    {
        StatusMap::iterator itr = mQuestStatus.find( quest_id );
        if  ( itr != mQuestStatus.end() && itr->second.m_status != QUEST_STATUS_NONE )
        {
            if( msg )
                SendCanTakeQuestResponse( INVALIDREASON_HAVE_QUEST );
            return false;
        }
        return true;
    }
    return false;
}

bool Player::SatisfyQuestTimed( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        if ( (find(m_timedquests.begin(), m_timedquests.end(), quest_id) != m_timedquests.end()) && qInfo->HasSpecialFlag(QUEST_SPECIAL_FLAGS_TIMED) )
        {
            if( msg )
                SendCanTakeQuestResponse( INVALIDREASON_HAVE_TIMED_QUEST );
            return false;
        }
        return true;
    }
    return false;
}

bool Player::SatisfyQuestExclusiveGroup( uint32 quest_id, bool msg )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        if(!qInfo->GetExclusiveGroup())
            return true;

        multimap<uint32, uint32>::iterator iter = objmgr.ExclusiveQuestGroups.lower_bound(qInfo->GetExclusiveGroup());
        multimap<uint32, uint32>::iterator end  = objmgr.ExclusiveQuestGroups.upper_bound(qInfo->GetExclusiveGroup());

        assert(iter!=end);                                  // always must be found if qInfo->ExclusiveGroup != 0

        for(; iter != end; ++iter)
        {
            uint32 exclude_Id = iter->second;

            // skip checked quest id, intrested only state of another quests in group
            if(exclude_Id == quest_id)
                continue;

            StatusMap::iterator i_exstatus = mQuestStatus.find( exclude_Id );

            // altearnative quest already start or complete
            if( i_exstatus != mQuestStatus.end()
                && (i_exstatus->second.m_status == QUEST_STATUS_COMPLETE || i_exstatus->second.m_status == QUEST_STATUS_INCOMPLETE) )
                return false;
        }
        return true;
    }
    return false;
}

bool Player::GiveQuestSourceItem( uint32 quest_id )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {

        uint32 srcitem = qInfo->GetSrcItemId();
        if( srcitem > 0 )
        {
            uint16 dest;
            uint32 count = qInfo->GetSrcItemCount();
            if( count <= 0 )
                count = 1;
            uint8 msg = CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, srcitem, count, false );
            if( msg == EQUIP_ERR_OK )
            {
                StoreNewItem(dest, srcitem, count, true);
                return true;
            }
            else
                SendEquipError( msg, NULL, NULL );
            return false;
        }
    }
    return true;
}

void Player::TakeQuestSourceItem( uint32 quest_id )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        uint32 srcitem = qInfo->GetSrcItemId();
        if( srcitem > 0 )
        {
            uint32 count = qInfo->GetSrcItemCount();
            if( count <= 0 )
                count = 1;
            DestroyItemCount(srcitem, count, true);
        }
    }
}

bool Player::GetQuestRewardStatus( uint32 quest_id )
{
    if( quest_id  )
    {
        StatusMap::iterator itr = mQuestStatus.find( quest_id );
        if  ( itr != mQuestStatus.end() && itr->second.m_status != QUEST_STATUS_NONE )
            return mQuestStatus[quest_id ].m_rewarded;

        return false;
    }
    return false;
}

uint32 Player::GetQuestStatus( uint32 quest_id )
{
    if( quest_id )
    {
        if  ( mQuestStatus.find( quest_id ) != mQuestStatus.end() )
            return mQuestStatus[quest_id].m_status;
    }
    return QUEST_STATUS_NONE;
}

void Player::SetQuestStatus( uint32 quest_id, QuestStatus status )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        if ((status == QUEST_STATUS_NONE) || (status == QUEST_STATUS_INCOMPLETE))
        {
            if( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_TIMED ) )
                if (find(m_timedquests.begin(), m_timedquests.end(), quest_id) != m_timedquests.end())
                    m_timedquests.remove(qInfo->GetQuestId());
        }

        mQuestStatus[quest_id].m_status = status;
    }
}

void Player::AdjustQuestReqItemCount( uint32 quest_id )
{
    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        if ( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_DELIVER ) )
        {
            for(int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
            {
                uint32 reqitemcount = qInfo->ReqItemCount[i];
                if( reqitemcount != 0 )
                {
                    uint32 curitemcount = GetItemCount(qInfo->ReqItemId[i]) + GetBankItemCount(qInfo->ReqItemId[i]);
                    mQuestStatus[quest_id].m_itemcount[i] = min(curitemcount, reqitemcount);
                }
            }
        }
    }
}

uint16 Player::GetQuestSlot( uint32 quest_id )
{
    for ( uint16 i = 0; i < MAX_QUEST_LOG_SIZE; i++ )
    {
        if ( GetUInt32Value(PLAYER_QUEST_LOG_1_1 + 3*i) == quest_id )
            return PLAYER_QUEST_LOG_1_1 + 3*i;
    }
    return 0;
}

void Player::AreaExplored( uint32 questId )
{
    if( questId )
    {
        uint16 log_slot = GetQuestSlot( questId );
        if( log_slot )
        {
            mQuestStatus[questId].m_explored = true;
        }
        if( CanCompleteQuest( questId ) )
            CompleteQuest( questId );
    }
}

void Player::ItemAddedQuestCheck( uint32 entry, uint32 count )
{
    uint32 questid;
    uint32 reqitem;
    uint32 reqitemcount;
    uint32 curitemcount;
    uint32 additemcount;
    for( int i = 0; i < MAX_QUEST_LOG_SIZE; i++ )
    {
        questid = GetUInt32Value(PLAYER_QUEST_LOG_1_1 + 3*i);
        if ( questid != 0 && mQuestStatus[questid].m_status == QUEST_STATUS_INCOMPLETE )
        {
            Quest * qInfo = objmgr.QuestTemplates[questid];
            if( qInfo && qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_DELIVER ) )
            {
                for (int j = 0; j < QUEST_OBJECTIVES_COUNT; j++)
                {
                    reqitem = qInfo->ReqItemId[j];
                    if ( reqitem == entry )
                    {
                        reqitemcount = qInfo->ReqItemCount[j];
                        curitemcount = mQuestStatus[questid].m_itemcount[j];
                        if ( curitemcount < reqitemcount )
                        {
                            additemcount = ( curitemcount + count <= reqitemcount ? count : reqitemcount - curitemcount);
                            mQuestStatus[questid].m_itemcount[j] += additemcount;
                            SendQuestUpdateAddItem( questid, j, additemcount );
                        }
                        if ( CanCompleteQuest( questid ) )
                            CompleteQuest( questid );
                        return;
                    }
                }
            }
        }
    }
}

void Player::ItemRemovedQuestCheck( uint32 entry, uint32 count )
{
    uint32 questid;
    uint32 reqitem;
    uint32 reqitemcount;
    uint32 curitemcount;
    uint32 remitemcount;
    for( int i = 0; i < MAX_QUEST_LOG_SIZE; i++ )
    {
        questid = GetUInt32Value(PLAYER_QUEST_LOG_1_1 + 3*i);
        Quest * qInfo = objmgr.QuestTemplates[questid];
        if ( qInfo )
        {
            if( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_DELIVER ) )
            {
                for (int j = 0; j < QUEST_OBJECTIVES_COUNT; j++)
                {
                    reqitem = qInfo->ReqItemId[j];
                    if ( reqitem == entry )
                    {
                        reqitemcount = qInfo->ReqItemCount[j];
                        if( mQuestStatus[questid].m_status != QUEST_STATUS_COMPLETE )
                            curitemcount = mQuestStatus[questid].m_itemcount[j];
                        else
                            curitemcount = GetItemCount(entry) + GetBankItemCount(entry);
                        if ( curitemcount - count < reqitemcount )
                        {
                            remitemcount = ( curitemcount <= reqitemcount ? count : count + reqitemcount - curitemcount);
                            mQuestStatus[questid].m_itemcount[j] = curitemcount - remitemcount;
                            IncompleteQuest( questid );
                        }
                        return;
                    }
                }
            }
        }
    }
}

void Player::KilledMonster( uint32 entry, uint64 guid )
{
    uint32 questid;
    uint32 reqkill;
    uint32 reqkillcount;
    uint32 curkillcount;
    uint32 addkillcount = 1;
    for( int i = 0; i < MAX_QUEST_LOG_SIZE; i++ )
    {
        questid = GetUInt32Value(PLAYER_QUEST_LOG_1_1 + 3*i);

        if(!questid)
            continue;

        Quest * qInfo = objmgr.QuestTemplates[questid];
        if ( qInfo && mQuestStatus[questid].m_status == QUEST_STATUS_INCOMPLETE )
        {
            if( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_KILL_OR_CAST ) )
            {
                for (int j = 0; j < QUEST_OBJECTIVES_COUNT; j++)
                {
                    // skip GO activate objective or none
                    if(qInfo->ReqCreatureOrGOId[j] <=0)
                        continue;

                    // skip Cast at creature objective
                    if(qInfo->ReqSpell[j] !=0 )
                        continue;

                    reqkill = qInfo->ReqCreatureOrGOId[j];

                    if ( reqkill == entry )
                    {
                        reqkillcount = qInfo->ReqCreatureOrGOCount[j];
                        curkillcount = mQuestStatus[questid].m_creatureOrGOcount[j];
                        if ( curkillcount < reqkillcount )
                        {
                            mQuestStatus[questid].m_creatureOrGOcount[j] = curkillcount + addkillcount;
                            SendQuestUpdateAddCreature( questid, guid, j, curkillcount, addkillcount);
                        }
                        if ( CanCompleteQuest( questid ) )
                            CompleteQuest( questid );
                        return;
                    }
                }
            }
        }
    }
}

void Player::CastedCreatureOrGO( uint32 entry, uint64 guid, uint32 spell_id )
{
    uint32 questid;
    uint32 reqCastCount;
    uint32 curCastCount;
    uint32 addCastCount = 1;
    for( int i = 0; i < MAX_QUEST_LOG_SIZE; i++ )
    {
        questid = GetUInt32Value(PLAYER_QUEST_LOG_1_1 + 3*i);

        if(!questid)
            continue;

        Quest * qInfo = objmgr.QuestTemplates[questid];
        if ( qInfo && mQuestStatus[questid].m_status == QUEST_STATUS_INCOMPLETE )
        {
            if( qInfo->HasSpecialFlag( QUEST_SPECIAL_FLAGS_KILL_OR_CAST ) )
            {
                for (int j = 0; j < QUEST_OBJECTIVES_COUNT; j++)
                {
                    // skip kill creature objective (0) or wrong spell casts
                    if(qInfo->ReqSpell[j] != spell_id )
                        continue;

                    uint32 reqTarget = 0;
                    // GO activate objective
                    if(qInfo->ReqCreatureOrGOId[j] < 0)
                    {
                        reqTarget = - qInfo->ReqCreatureOrGOId[j];
                        assert(sGOStorage.LookupEntry<GameObject>(reqTarget));
                    }
                    // creature acivate objectives
                    else if(qInfo->ReqCreatureOrGOId[j] > 0)
                    {
                        reqTarget = qInfo->ReqCreatureOrGOId[j];
                        assert(sCreatureStorage.LookupEntry<Creature>(reqTarget));
                    }
                    // other not creature/GO related obejctives
                    else
                        continue;

                    if ( reqTarget == entry )
                    {
                        reqCastCount = qInfo->ReqCreatureOrGOCount[j];
                        curCastCount = mQuestStatus[questid].m_creatureOrGOcount[j];
                        if ( curCastCount < reqCastCount )
                        {
                            mQuestStatus[questid].m_creatureOrGOcount[j] = curCastCount + addCastCount;
                            SendQuestUpdateAddCreature( questid, guid, j, curCastCount, addCastCount);
                        }
                        if ( CanCompleteQuest( questid ) )
                            CompleteQuest( questid );
                        return;
                    }
                }
            }
        }
    }
}

void Player::MoneyChanged( uint32 count )
{
    uint32 questid;
    for( int i = 0; i < MAX_QUEST_LOG_SIZE; i++ )
    {
        questid = GetUInt32Value(PLAYER_QUEST_LOG_1_1 + 3*i);
        if ( questid != 0 )
        {
            Quest * qInfo = objmgr.QuestTemplates[questid];
            if( qInfo && qInfo->GetRewOrReqMoney() < 0 )
            {
                if( mQuestStatus[questid].m_status == QUEST_STATUS_INCOMPLETE )
                {
                    if(int32(count) >= -qInfo->GetRewOrReqMoney())
                    {
                        if ( CanCompleteQuest( questid ) )
                            CompleteQuest( questid );
                    }
                }
                else if( mQuestStatus[questid].m_status == QUEST_STATUS_COMPLETE )
                {
                    if(int32(count) < -qInfo->GetRewOrReqMoney())
                        IncompleteQuest( questid );
                }
            }
        }
    }
}

bool Player::HaveQuestForItem( uint32 itemid )
{
    for( StatusMap::iterator i = mQuestStatus.begin( ); i != mQuestStatus.end( ); ++ i )
    {
        quest_status qs=i->second;

        if (qs.m_status == QUEST_STATUS_INCOMPLETE)
        {
            if (!qs.m_quest) continue;
            Quest * qinfo = qs.m_quest;

            // There should be no mixed ReqItem/ReqSource drop
            // This part for ReqItem drop
            for (int j = 0; j < QUEST_OBJECTIVES_COUNT; j++)
            {
                if(itemid == qinfo->ReqItemId[j] && qs.m_itemcount[j] < qinfo->ReqItemCount[j] )
                    return true;
            }
            // This part - for ReqSource
            for (int j = 0; j < QUEST_SOURCE_ITEM_IDS_COUNT; j++)
            {
                // examined item is a source item
                if (qinfo->ReqSourceId[j] == itemid && qinfo->ReqSourceRef[j] > 0 && qinfo->ReqSourceRef[j] <= QUEST_OBJECTIVES_COUNT)
                {
                    uint32 idx = qinfo->ReqSourceRef[j]-1;
                    // total count of created ReqItems and SourceItems is less than ReqItemCount
                    if(qinfo->ReqItemId[idx] != 0 &&
                        qs.m_itemcount[idx] + GetItemCount(itemid)+ GetBankItemCount(itemid) < qinfo->ReqItemCount[idx])
                        return true;

                    // total count of casted ReqCreatureOrGOs and SourceItems is less than ReqCreatureOrGOCount
                    if (qinfo->ReqCreatureOrGOId[idx] != 0 &&
                        qs.m_creatureOrGOcount[idx] + GetItemCount(itemid)+ GetBankItemCount(itemid) < qinfo->ReqCreatureOrGOCount[idx])
                        return true;
                }
            }
        }
    }
    return false;
}

void Player::SendQuestComplete( uint32 quest_id )
{
    if( quest_id )
    {
        WorldPacket data;
        data.Initialize( SMSG_QUESTUPDATE_COMPLETE );
        data << quest_id;
        GetSession()->SendPacket( &data );
        sLog.outDebug( "WORLD: Sent SMSG_QUESTUPDATE_COMPLETE quest = %u", quest_id );
    }
}

void Player::SendQuestReward( Quest *pQuest, uint32 XP, Object * questGiver )
{
    if( pQuest )
    {
        uint32 questid = pQuest->GetQuestId();
        sLog.outDebug( "WORLD: Sent SMSG_QUESTGIVER_QUEST_COMPLETE quest = %u", questid );
        WorldPacket data;
        data.Initialize( SMSG_QUESTGIVER_QUEST_COMPLETE );
        data << questid;
        data << uint32(0x03);

        if ( getLevel() < sWorld.getConfig(CONFIG_MAX_PLAYER_LEVEL) )
        {
            data << XP;
            data << uint32(pQuest->GetRewOrReqMoney());
        }
        else
        {
            data << uint32(0);
            data << uint32(pQuest->GetRewOrReqMoney() + XP);
        }
        data << uint32( pQuest->GetRewItemsCount() );

        for (int i = 0; i < pQuest->GetRewItemsCount(); i++)
        {
            if ( pQuest->RewItemId[i] > 0 )
                data << pQuest->RewItemId[i] << pQuest->RewItemCount[i];
        }

        GetSession()->SendPacket( &data );

        if (pQuest->GetQuestCompleteScript() != 0)
        {
            ScriptMapMap::iterator s = sScripts.find(pQuest->GetQuestCompleteScript());
            if (s == sScripts.end())
                return;

            ScriptMap *s2 = &(s->second);
            ScriptMap::iterator iter;
            bool immedScript = false;
            for (iter = s2->begin(); iter != s2->end(); iter++)
            {
                if (iter->first == 0)
                {
                    ScriptAction sa;
                    sa.source = questGiver;
                    sa.script = &iter->second;
                    sa.target = this;
                    sWorld.scriptSchedule.insert(pair<uint64, ScriptAction>(0, sa));
                    immedScript = true;
                }
                else
                {
                    ScriptAction sa;
                    sa.source = questGiver;
                    sa.script = &iter->second;
                    sa.target = this;
                    sWorld.scriptSchedule.insert(pair<uint64, ScriptAction>(sWorld.internalGameTime + iter->first, sa));
                }
            }
            if (immedScript)
                sWorld.ScriptsProcess();
        }
    }
}

void Player::SendQuestFailed( uint32 quest_id )
{
    if( quest_id )
    {
        WorldPacket data;
        data.Initialize( SMSG_QUESTGIVER_QUEST_FAILED );
        data << quest_id;
        GetSession()->SendPacket( &data );
        sLog.outDebug("WORLD: Sent SMSG_QUESTGIVER_QUEST_FAILED");
    }
}

void Player::SendQuestTimerFailed( uint32 quest_id )
{
    if( quest_id )
    {
        WorldPacket data;
        data.Initialize( SMSG_QUESTUPDATE_FAILEDTIMER );
        data << quest_id;
        GetSession()->SendPacket( &data );
        sLog.outDebug("WORLD: Sent SMSG_QUESTUPDATE_FAILEDTIMER");
    }
}

void Player::SendCanTakeQuestResponse( uint32 msg )
{
    WorldPacket data;
    data.Initialize( SMSG_QUESTGIVER_QUEST_INVALID );
    data << msg;
    GetSession()->SendPacket( &data );
    sLog.outDebug("WORLD: Sent SMSG_QUESTGIVER_QUEST_INVALID");
}

void Player::SendPushToPartyResponse( Player *pPlayer, uint32 msg )
{
    if( pPlayer )
    {
        WorldPacket data;
        data.Initialize( MSG_QUEST_PUSH_RESULT );
        data << pPlayer->GetGUID();
        data << msg;
        data << uint8(0);
        GetSession()->SendPacket( &data );
        sLog.outDebug("WORLD: Sent MSG_QUEST_PUSH_RESULT");
    }
}

void Player::SendQuestUpdateAddItem( uint32 quest_id, uint32 item_idx, uint32 count )
{
    if( quest_id )
    {
        WorldPacket data;
        data.Initialize( SMSG_QUESTUPDATE_ADD_ITEM );
        sLog.outDebug( "WORLD: Sent SMSG_QUESTUPDATE_ADD_ITEM" );
        data << objmgr.QuestTemplates[quest_id]->ReqItemId[item_idx];
        data << count;
        GetSession()->SendPacket( &data );
    }
}

void Player::SendQuestUpdateAddCreature( uint32 quest_id, uint64 guid, uint32 creature_idx, uint32 old_count, uint32 add_count )
{
    assert(old_count + add_count < 64 && "mob/GO count store in 6 bits 2^6 = 64 (0..63)");

    Quest * qInfo = objmgr.QuestTemplates[quest_id];
    if( qInfo )
    {
        WorldPacket data;
        data.Initialize( SMSG_QUESTUPDATE_ADD_KILL );
        sLog.outDebug( "WORLD: Sent SMSG_QUESTUPDATE_ADD_KILL" );
        data << qInfo->GetQuestId();
        data << uint32(qInfo->ReqCreatureOrGOId[ creature_idx ]);
        data << old_count + add_count;
        data << qInfo->ReqCreatureOrGOCount[ creature_idx ];
        data << guid;
        GetSession()->SendPacket(&data);

        uint16 log_slot = GetQuestSlot( quest_id );
        uint32 kills = GetUInt32Value( log_slot + 1 );
        kills = kills + (add_count << ( 6 * creature_idx ));
        SetUInt32Value( log_slot + 1, kills );
    }
}

/*********************************************************/
/***                   LOAD SYSTEM                     ***/
/*********************************************************/

bool Player::MinimalLoadFromDB( uint32 guid )
{
    QueryResult *result = sDatabase.PQuery("SELECT `data`,`name`,`position_x`,`position_y`,`position_z`,`map` FROM `character` WHERE `guid` = '%u'",guid);
    if(!result)
        return false;

    Field *fields = result->Fetch();

    if(!LoadValues( fields[0].GetString()))
    {
        sLog.outError("ERROR: Player #%d have broken data in `data` field. Can't be loaded.",GUID_LOPART(guid));
        delete result;
        return false;
    }

    m_name = fields[1].GetCppString();
    m_positionX = fields[2].GetFloat();
    m_positionY = fields[3].GetFloat();
    m_positionZ = fields[4].GetFloat();
    m_mapId = fields[5].GetUInt32();

    for (int i = 0; i < MAX_QUEST_LOG_SIZE; i++)
        m_items[i] = NULL;

    delete result;
    return true;
}

bool Player::LoadPositionFromDB(uint32& mapid, float& x,float& y,float& z,float& o, uint64 guid)
{
    QueryResult *result = sDatabase.PQuery("SELECT `position_x`,`position_y`,`position_z`,`orientation`,`map` FROM `character` WHERE `guid` = '%u'",guid);
    if(!result)
        return false;

    Field *fields = result->Fetch();

    x = fields[0].GetFloat();
    y = fields[1].GetFloat();
    z = fields[2].GetFloat();
    o = fields[3].GetFloat();
    mapid = fields[4].GetUInt32();

    delete result;
    return true;
}

bool Player::LoadValuesArrayFromDB(vector<string> & data, uint64 guid)
{
    std::ostringstream ss;
    ss<<"SELECT `data` FROM `character` WHERE `guid`='"<<guid<<"'";
    QueryResult *result = sDatabase.Query( ss.str().c_str() );
    if( !result )
        return false;

    Field *fields = result->Fetch();

    data = StrSplit(fields[0].GetString(), " ");

    delete result;

    return true;
}

uint32 Player::GetUInt32ValueFromArray(vector<string> const& data, uint16 index)
{
    return (uint32)atoi(data[index].c_str());
}

float Player::GetFloatValueFromArray(vector<string> const& data, uint16 index)
{
    float result;
    uint32 temp = Player::GetUInt32ValueFromArray(data,index);
    memcpy(&result, &temp, sizeof(result));

    return result;
}

uint32 Player::GetUInt32ValueFromDB(uint16 index, uint64 guid)
{
    vector<string> data;
    if(!LoadValuesArrayFromDB(data,guid))
        return 0;

    return GetUInt32ValueFromArray(data,index);
}

float Player::GetFloatValueFromDB(uint16 index, uint64 guid)
{
    float result;
    uint32 temp = Player::GetUInt32ValueFromDB(index, guid);
    memcpy(&result, &temp, sizeof(result));

    return result;
}

bool Player::LoadFromDB( uint32 guid )
{
    //                                             0      1       2         3      4      5      6       7            8            9            10    11            12         13       14             15         16       17          18          19          20           21            22                  23                  24                  25         26         27         28         29
    QueryResult *result = sDatabase.PQuery("SELECT `guid`,`realm`,`account`,`data`,`name`,`race`,`class`,`position_x`,`position_y`,`position_z`,`map`,`orientation`,`taximask`,`online`,`highest_rank`,`standing`,`rating`,`cinematic`,`totaltime`,`leveltime`,`rest_bonus`,`logout_time`,`is_logout_resting`,`resettalents_cost`,`resettalents_time`,`trans_x`, `trans_y`, `trans_z`, `trans_o`, `transguid` FROM `character` WHERE `guid` = '%u'",guid);

    if(!result)
    {
        sLog.outError("ERROR: Player (GUID: %u) not found in table `character`, can't load. ",guid);
        return false;
    }

    Field *fields = result->Fetch();

    Object::_Create( guid, HIGHGUID_PLAYER );

    if(!LoadValues( fields[3].GetString()))
    {
        sLog.outError("ERROR: Player #%d have broken data in `data` field. Can't be loaded.",GUID_LOPART(guid));
        delete result;
        return false;
    }

    // cleanup inventory related item value fields (its will be filled correctly in _LoadInventory)
    for(uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        SetUInt64Value( (uint16)(PLAYER_FIELD_INV_SLOT_HEAD + (slot * 2) ), 0 );

        int VisibleBase = PLAYER_VISIBLE_ITEM_1_0 + (slot * 12);
        for(int i = 0; i < 9; ++i )
            SetUInt32Value(VisibleBase + i, 0);

        if (m_items[slot])
        {
            delete m_items[slot];
            m_items[slot] = NULL;
        }
    }

    m_drunk = GetUInt32Value(PLAYER_BYTES_3) & 0xFFFE;

    m_name = fields[4].GetCppString();

    sLog.outDebug("Load Basic value of player %s is: ", m_name.c_str());
    outDebugValues();

    m_race = fields[5].GetUInt8();
    //Need to call it to initialize m_team (m_team can be calculated from m_race)
    //Other way is to saves m_team into characters table.
    setFactionForRace(m_race);
    SetCharm(0);

    m_class = fields[6].GetUInt8();

    PlayerInfo const *info = objmgr.GetPlayerInfo(m_race, m_class);
    if(!info)
    {
        sLog.outError("Player have incorrect race/class pair. Can't be loaded.");
        delete result;
        return false;
    }

    uint32 transGUID = fields[29].GetUInt32();
    m_positionX = fields[7].GetFloat();
    m_positionY = fields[8].GetFloat();
    m_positionZ = fields[9].GetFloat();
    m_mapId = fields[10].GetUInt32();
    m_orientation = fields[11].GetFloat();

    if (transGUID != 0)
    {
        m_transX = fields[25].GetFloat();
        m_transY = fields[26].GetFloat();
        m_transZ = fields[27].GetFloat();
        m_transO = fields[28].GetFloat();

        for (int i = 0; i < MapManager::Instance().m_Transports.size(); i++)
        {
            if ((MapManager::Instance().m_Transports[i])->GetGUIDLow() == transGUID)
            {
                m_transport = MapManager::Instance().m_Transports[i];
                m_transport->AddPassenger(this);
                m_mapId = m_transport->GetMapId();
            }
        }
    }

    // since last logout (in seconds)
    uint32 time_diff = (time(NULL) - fields[21].GetUInt32());

    rest_bonus = fields[20].GetFloat();
    //speed collect rest bonus in offline, in logout, far from tavern, city (section/in hour)
    float bubble0 = 0.0416;
    //speed collect rest bonus in offline, in logout, in tavern, city (section/in hour)
    float bubble1 = 0.083;

    if((int32)fields[21].GetUInt32() > 0)
    {
        float bubble = fields[22].GetUInt32() > 0
            ? bubble1*sWorld.getRate(RATE_REST_OFFLINE_IN_TAVERN_OR_CITY)
            : bubble0*sWorld.getRate(RATE_REST_OFFLINE_IN_WILDERNESS);

        SetRestBonus(GetRestBonus()+ (time(NULL)-(int32)fields[21].GetUInt32())*((float)GetUInt32Value(PLAYER_NEXT_LEVEL_XP)/144000)*bubble);
    }

    if(!IsPositionValid())
    {
        sLog.outError("ERROR: Player (guidlow %d) have invalid coordinates (X: %d Y: ^%d). Teleport to default race/class locations.",guid,m_positionX,m_positionY);

        m_mapId = info->mapId;
        m_positionX = info->positionX;
        m_positionY = info->positionY;
        m_positionZ = info->positionZ;
    }

    m_highest_rank = fields[14].GetUInt32();
    m_standing = fields[15].GetUInt32();
    m_rating = fields[16].GetFloat();
    m_cinematic = fields[17].GetUInt32();
    m_Played_time[0]= fields[18].GetUInt32();
    m_Played_time[1]= fields[19].GetUInt32();

    m_resetTalentsCost = fields[23].GetUInt32();
    m_resetTalentsTime = fields[24].GetUInt64();

    if( HasFlag(PLAYER_FLAGS, 8) )
        SetUInt32Value(PLAYER_FLAGS, 0);

    if( HasFlag(PLAYER_FLAGS, 0x11) )
        m_deathState = DEAD;

    _LoadTaxiMask( fields[12].GetString() );

    delete result;

    // remember loaded power values to restore after stats initialization and modifier appling
    float savedPower[MAX_POWERS];
    for(uint32 i = 0; i < MAX_POWERS; ++i)
        savedPower[i] = GetPower(Powers(i));

    // reset stats before loading any modifiers
    InitStatsForLevel(getLevel(),false,false);

    // make sure the unit is considered out of combat for proper loading
    ClearInCombat();

    //mails are loaded only when needed ;-) - when player in game click on mailbox.
    //_LoadMail();

    _LoadAuras(time_diff);

    _LoadSpells(time_diff);

    _LoadQuestStatus();

    _LoadTutorials();

    _LoadInventory(time_diff);

    _LoadActions();

    _LoadReputation();

    // Skip _ApplyAllAuraMods(); -- applied in _LoadAuras by AddAura calls at aura load
    // Skip _ApplyAllItemMods(); -- already applied in _LoadInventory()

    // restore remembered power values
    for(uint32 i = 0; i < MAX_POWERS; ++i)
        SetPower(Powers(i),savedPower[i]);
    

    sLog.outDebug("The value of player %s after load item and aura is: ", m_name.c_str());
    outDebugValues();

    return true;
}

void Player::_LoadActions()
{

    m_actions.clear();

    QueryResult *result = sDatabase.PQuery("SELECT `button`,`action`,`type`,`misc` FROM `character_action` WHERE `guid` = '%u' ORDER BY `button`",GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();

            addAction(fields[0].GetUInt8(), fields[1].GetUInt16(), fields[2].GetUInt8(), fields[3].GetUInt8());
        }
        while( result->NextRow() );

        delete result;
    }
}

void Player::_LoadAuras(uint32 timediff)
{
    m_Auras.clear();
    for (int i = 0; i < TOTAL_AURAS; i++)
        m_modAuras[i].clear();

    for(uint8 i = 0; i < 48; i++)
        SetUInt32Value((uint16)(UNIT_FIELD_AURA + i), 0);
    for(uint8 j = 0; j < 6; j++)
        SetUInt32Value((uint16)(UNIT_FIELD_AURAFLAGS + j), 0);

    QueryResult *result = sDatabase.PQuery("SELECT `spell`,`effect_index`,`remaintime` FROM `character_aura` WHERE `guid` = '%u'",GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();
            uint32 spellid = fields[0].GetUInt32();
            uint32 effindex = fields[1].GetUInt32();
            int32 remaintime = (int32)fields[2].GetUInt32();

            SpellEntry* spellproto = sSpellStore.LookupEntry(spellid);
            if(!spellproto)
            {
                sLog.outError("Unknown aura (spellid %u, effindex %u), ignore.",spellid,effindex);
                continue;
            }

            // negative effects should continue counting down after logout
            if (remaintime != -1 && !IsPositiveEffect(spellid, effindex))
            {
                remaintime -= timediff;
                if(remaintime <= 0) continue;
            }

            // FIXME: real caster not stored in DB currently

            Aura* aura = new Aura(spellproto, effindex, this, this/*caster*/);
            aura->SetAuraDuration(remaintime);
            AddAura(aura);
        }
        while( result->NextRow() );

        delete result;
    }

    if(m_class == CLASS_WARRIOR)
        CastSpell(this,SPELL_PASSIVE_BATTLE_STANCE,true);
}

void Player::LoadCorpse()
{
    if(Corpse* corpse = GetCorpse())
    {
        if( isAlive() )
        {
            if( corpse->GetType() == CORPSE_RESURRECTABLE )
                corpse->ConvertCorpseToBones();
        }
        else
            corpse->UpdateForPlayer(this,true);
    }
    else
    {
        //Prevent Dead Player login without corpse
        if(!isAlive())
            ResurrectPlayer();
    }
}

void Player::_LoadInventory(uint32 timediff)
{
    QueryResult *result = sDatabase.PQuery("SELECT `slot`,`item`,`item_template` FROM `character_inventory` WHERE `guid` = '%u' AND `bag` = '0' ORDER BY `slot`",GetGUIDLow());

    uint16 dest;
    if (result)
    {
        // prevent items from being added to the queue when stored
        m_itemUpdateQueueBlocked = true;
        do
        {
            Field *fields = result->Fetch();
            uint8  slot      = fields[0].GetUInt8();
            uint32 item_guid = fields[1].GetUInt32();
            uint32 item_id   = fields[2].GetUInt32();

            ItemPrototype const * proto = objmgr.GetItemPrototype(item_id);

            if(!proto)
            {
                sLog.outError( "Player::_LoadInventory: Player %s have unknown item (id: #%u) in inventory, skipped.", GetName(),item_id );
                continue;
            }

            Item *item = NewItemOrBag(proto);
            item->SetSlot( slot );
            item->SetContainer( NULL );

            if(!item->LoadFromDB(item_guid, GetGUID()))
            {
                delete item;
                continue;
            }

            bool success = true;
            dest = ((INVENTORY_SLOT_BAG_0 << 8) | slot);
            if( IsInventoryPos( dest ) )
            {
                if( !CanStoreItem( INVENTORY_SLOT_BAG_0, slot, dest, item, false ) == EQUIP_ERR_OK )
                {
                    success = false;
                    continue;
                }

                StoreItem(dest, item, true);
            }
            else if( IsEquipmentPos( dest ) )
            {
                if( !CanEquipItem( slot, dest, item, false, false ) == EQUIP_ERR_OK )
                {
                    success = false;
                    continue;
                }

                QuickEquipItem(dest, item);
            }
            else if( IsBankPos( dest ) )
            {
                if( !CanBankItem( INVENTORY_SLOT_BAG_0, slot, dest, item, false, false ) == EQUIP_ERR_OK )
                {
                    success = false;
                    continue;
                }

                BankItem(dest, item, true);
            }

            // item's state may have changed after stored
            if (success) item->SetState(ITEM_UNCHANGED, this);
            else delete item;
        } while (result->NextRow());

        delete result;
        m_itemUpdateQueueBlocked = false;
    }
    if(isAlive())
        _ApplyAllItemMods();
}

// load mailed items which should receive current player
void Player::_LoadMailedItems()
{
    QueryResult *result = sDatabase.PQuery( "SELECT `item`,`item_template` FROM `mail` WHERE `receiver` = '%u' AND `item` > 0", GetGUIDLow());

    if( !result )
        return;

    Field *fields;
    do
    {
        fields = result->Fetch();
        uint32 item_guid = fields[0].GetUInt32();
        uint32 item_id   = fields[1].GetUInt32();

        ItemPrototype const *proto = objmgr.GetItemPrototype(item_id);

        if(!proto)
        {
            sLog.outError( "Bag::LoadFromDB: Player %d have unknown item (GUID: %u id: #%u) in mail, skipped.", GetGUIDLow(), item_guid, item_id);
            continue;
        }

        Item *item = NewItemOrBag(proto);
        if(!item->LoadFromDB(item_guid, 0))
        {
            delete item;
            continue;
        }
        AddMItem(item);
    }
    while( result->NextRow() );

    delete result;
}

void Player::_LoadMail()
{
    //delete old mails, and if old mail has item so delete it too
    time_t base = time(NULL);

    //FIXME: mails with COD will not be returned, but deleted.

    //delete old mails:
    sDatabase.PExecute("DELETE a FROM item_instance AS a INNER JOIN mail AS ab ON a.guid=ab.item WHERE ab.time < '" I64FMTD "' AND `receiver` = '%u'", (uint64)base, GetGUIDLow());
    sDatabase.PExecute("DELETE FROM `mail` WHERE `time` < '" I64FMTD "' AND `receiver` = '%u'", (uint64)base, GetGUIDLow());

    _LoadMailedItems();

    m_mail.clear();

    QueryResult *result = sDatabase.PQuery("SELECT `id`,`sender`,`receiver`,`subject`,`body`,`item`,`item_template`,`time`,`money`,`cod`,`checked` FROM `mail` WHERE `receiver` = '%u'",GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();
            Mail *be = new Mail;
            be->messageID = fields[0].GetUInt32();
            be->sender = fields[1].GetUInt32();
            be->receiver = fields[2].GetUInt32();
            be->subject = fields[3].GetCppString();
            be->body = fields[4].GetCppString();
            be->item_guidlow = fields[5].GetUInt32();
            be->item_id = fields[6].GetUInt32();
            be->time = fields[7].GetUInt32();
            be->money = fields[8].GetUInt32();
            be->COD = fields[9].GetUInt32();
            be->checked = fields[10].GetUInt32();
            m_mail.push_back(be);
        }
        while( result->NextRow() );

        delete result;
    }

    m_mailsLoaded = true;
}

void Player::LoadPet()
{
    uint64 pet_guid = GetPetGUID();
    if(pet_guid)
    {
        Creature* in_pet = ObjectAccessor::Instance().GetCreature(*this, pet_guid);
        if(in_pet)
            return;
        Pet *pet = new Pet();
        pet->LoadPetFromDB(this);
    }
}

void Player::_LoadQuestStatus()
{
    mQuestStatus.clear();

    QueryResult *result = sDatabase.PQuery("SELECT `quest`,`status`,`rewarded`,`explored`,`completed_once`,`timer`,`mobcount1`,`mobcount2`,`mobcount3`,`mobcount4`,`itemcount1`,`itemcount2`,`itemcount3`,`itemcount4` FROM `character_queststatus` WHERE `guid` = '%u'", GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();

            uint32 quest_id = fields[0].GetUInt32();
            Quest* pQuest = objmgr.QuestTemplates[quest_id];   // used to be new, no delete?
            if( pQuest )
            {
                mQuestStatus[quest_id].m_quest = pQuest;

                uint32 qstatus = fields[1].GetUInt32();
                if(qstatus < MAX_QUEST_STATUS)
                    mQuestStatus[quest_id].m_status = QuestStatus(qstatus);
                else
                {
                    mQuestStatus[quest_id].m_status = QUEST_STATUS_NONE;
                    sLog.outError("Player %s have invalid quest %d status (%d), replaced by QUEST_STATUS_NONE(0).",GetName(),quest_id,qstatus);
                }

                mQuestStatus[quest_id].m_rewarded = ( fields[2].GetUInt8() > 0 );
                mQuestStatus[quest_id].m_explored = ( fields[3].GetUInt8() > 0 );
                mQuestStatus[quest_id].m_completed_once = ( fields[4].GetUInt8() > 0 );

                if( objmgr.QuestTemplates[quest_id]->HasSpecialFlag( QUEST_SPECIAL_FLAGS_TIMED ) && !mQuestStatus[quest_id].m_rewarded )
                    AddTimedQuest( quest_id );

                if (fields[5].GetUInt32() <= sWorld.GetGameTime())
                {
                    mQuestStatus[quest_id].m_timer = 1;
                } else
                mQuestStatus[quest_id].m_timer = (fields[5].GetUInt32() - sWorld.GetGameTime()) * 1000;

                mQuestStatus[quest_id].m_creatureOrGOcount[0] = fields[6].GetUInt32();
                mQuestStatus[quest_id].m_creatureOrGOcount[1] = fields[7].GetUInt32();
                mQuestStatus[quest_id].m_creatureOrGOcount[2] = fields[8].GetUInt32();
                mQuestStatus[quest_id].m_creatureOrGOcount[3] = fields[9].GetUInt32();
                mQuestStatus[quest_id].m_itemcount[0] = fields[10].GetUInt32();
                mQuestStatus[quest_id].m_itemcount[1] = fields[11].GetUInt32();
                mQuestStatus[quest_id].m_itemcount[2] = fields[12].GetUInt32();
                mQuestStatus[quest_id].m_itemcount[3] = fields[13].GetUInt32();

                sLog.outDebug("Quest status is {%u} for quest {%u}", mQuestStatus[quest_id].m_status, quest_id);
            }
        }
        while( result->NextRow() );

        delete result;
    }
}

void Player::_LoadReputation()
{
    Factions newFaction;

    factions.clear();

    QueryResult *result = sDatabase.PQuery("SELECT `faction`,`reputation`,`standing`,`flags` FROM `character_reputation` WHERE `guid` = '%u'",GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();

            newFaction.ID               = fields[0].GetUInt32();
            newFaction.ReputationListID = fields[1].GetUInt32();
            newFaction.Standing         = fields[2].GetUInt32();
            newFaction.Flags            = fields[3].GetUInt32();

            factions.push_back(newFaction);
        }
        while( result->NextRow() );

        delete result;
    }
    else
    {
        //LoadReputationFromDBC();
        //Set initial reputations
        SetInitialFactions();
    }
}

void Player::_LoadSpells(uint32 timediff)
{
    for (PlayerSpellMap::iterator itr = m_spells.begin(); itr != m_spells.end(); ++itr)
        delete itr->second;
    m_spells.clear();

    QueryResult *result = sDatabase.PQuery("SELECT `spell`,`slot`,`active` FROM `character_spell` WHERE `guid` = '%u'",GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();

            addSpell(fields[0].GetUInt16(), fields[2].GetUInt8(), PLAYERSPELL_UNCHANGED, fields[1].GetUInt16());
        }
        while( result->NextRow() );

        delete result;
    }
}

void Player::_LoadTaxiMask(const char* data)
{
    vector<string> tokens = StrSplit(data, " ");

    int index;
    vector<string>::iterator iter;

    for (iter = tokens.begin(), index = 0;
        (index < 8) && (iter != tokens.end()); ++iter, ++index)
    {
        m_taximask[index] = atol((*iter).c_str());
    }
}

void Player::_LoadTutorials()
{
    QueryResult *result = sDatabase.PQuery("SELECT `tut0`,`tut1`,`tut2`,`tut3`,`tut4`,`tut5`,`tut6`,`tut7` FROM `character_tutorial` WHERE `guid` = '%u'",GetGUIDLow());

    if(result)
    {
        do
        {
            Field *fields = result->Fetch();

            for (int iI=0; iI<8; iI++)
                m_Tutorials[iI] = fields[iI].GetUInt32();

        }
        while( result->NextRow() );

        delete result;
    }
}

/*********************************************************/
/***                   SAVE SYSTEM                     ***/
/*********************************************************/

void Player::SaveToDB()
{
    // delay auto save at any saves (manual, in code, or autosave)
    m_nextSave = sWorld.getConfig(CONFIG_INTERVAL_SAVE);

    // saved before flight
    if (isInFlight())
        return;

    // save state
    uint32 tmp_bytes = GetUInt32Value(UNIT_FIELD_BYTES_1);
    uint32 tmp_flags = GetUInt32Value(UNIT_FIELD_FLAGS);
    uint32 tmp_pflags = GetUInt32Value(PLAYER_FLAGS);

    int is_logout_resting=0;                                //logout, far from tavern/city
                                                            //logout, but in tavern/city
    if(!IsInWorld()&&HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING))is_logout_resting=1;

    // Set player sit state to standing on save
    RemoveFlag(UNIT_FIELD_BYTES_1,PLAYER_STATE_SIT);
    RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISABLE_ROTATE);

    //remove restflag when save
    //this is becouse of the rename char stuff
    RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);

    ClearInCombat();                                        // Remove combat flag

    sLog.outDebug("The value of player %s before unload item and aura is: ", m_name.c_str());
    outDebugValues();

    if(isAlive())
    {
        _RemoveAllItemMods();
        _RemoveAllAuraMods();
    }

    bool inworld = IsInWorld();
    if (inworld)
        RemoveFromWorld();

    sDatabase.PExecute("DELETE FROM `character` WHERE `guid` = '%u'",GetGUIDLow());

    std::ostringstream ss;
    ss << "INSERT INTO `character` (`guid`,`realm`,`account`,`name`,`race`,`class`,"
        "`map`,`position_x`,`position_y`,`position_z`,`orientation`,`data`,"
        "`taximask`,`online`,`highest_rank`,`standing`,`rating`,`cinematic`,"
        "`totaltime`,`leveltime`,`rest_bonus`,`logout_time`,`is_logout_resting`,`resettalents_cost`,`resettalents_time`,"
        "`trans_x`, `trans_y`, `trans_z`, `trans_o`, `transguid`) VALUES ("
        << GetGUIDLow() << ", "
        << realmID << ", "
        << GetSession()->GetAccountId() << ", '"
        << m_name << "', "
        << m_race << ", "
        << m_class << ", "
        << m_mapId << ", "
        << m_positionX << ", "
        << m_positionY << ", "
        << m_positionZ << ", "
        << m_orientation << ", '";

    uint16 i;
    for( i = 0; i < m_valuesCount; i++ )
    {
        ss << GetUInt32Value(i) << " ";
    }

    ss << "', '";

    for( i = 0; i < 8; i++ )
        ss << m_taximask[i] << " ";

    ss << "', ";
    inworld ? ss << 1: ss << 0;

    ss << ", ";
    ss << m_highest_rank;

    ss << ", ";
    ss << m_standing;

    ss << ", ";
    ss << m_rating;

    ss << ", ";
    ss << m_cinematic;

    ss << ", ";
    ss << m_Played_time[0];
    ss << ", ";
    ss << m_Played_time[1];

    ss << ", ";
    ss << rest_bonus;
    ss << ", ";
    ss << time(NULL);
    ss << ", ";
    ss << is_logout_resting;
    ss << ", ";
    ss << m_resetTalentsCost;
    ss << ", ";
    ss << (uint64)m_resetTalentsTime;

    ss << ", ";
    ss << m_transX;
    ss << ", ";
    ss << m_transY;
    ss << ", ";
    ss << m_transZ;
    ss << ", ";
    ss << m_transO;
    ss << ", ";
    if (m_transport)
        ss << m_transport->GetGUIDLow();
    else
        ss << "0";

    ss << " )";

    sDatabase.Execute( ss.str().c_str() );

    SaveEnchant();

    if(m_mailsUpdated)                                      //save mails only when needed
        _SaveMail();

    _SaveInventory();
    _SaveQuestStatus();
    _SaveTutorials();
    _SaveSpells();
    _SaveSpellCooldowns();
    _SaveActions();
    _SaveAuras();
    _SaveReputation();
    SavePet();

    sLog.outDebug("Save Basic value of player %s is: ", m_name.c_str());
    outDebugValues();

    if(isAlive())
    {
        _ApplyAllAuraMods();
        _ApplyAllItemMods();
    }

    // restore state
    SetUInt32Value(UNIT_FIELD_BYTES_1, tmp_bytes);
    SetUInt32Value(UNIT_FIELD_FLAGS, tmp_flags);
    SetUInt32Value(PLAYER_FLAGS, tmp_pflags);

    if (inworld)
        AddToWorld();
}

void Player::_SaveActions()
{
    sDatabase.PExecute("DELETE FROM `character_action` WHERE `guid` = '%u'",GetGUIDLow());

    std::list<struct actions>::iterator itr;
    for (itr = m_actions.begin(); itr != m_actions.end(); ++itr)
    {
        sDatabase.PExecute("INSERT INTO `character_action` (`guid`,`button`,`action`,`type`,`misc`) VALUES ('%u', '%u', '%u', '%u', '%u')", GetGUIDLow(), (uint32)itr->button, (uint32)itr->action, (uint32)itr->type, (uint32)itr->misc);
    }
}

void Player::_SaveAuras()
{
    sDatabase.PExecute("DELETE FROM `character_aura` WHERE `guid` = '%u'",GetGUIDLow());

    AuraMap const& auras = GetAuras();
    for(AuraMap::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
    {
        SpellEntry *spellInfo = itr->second->GetSpellProto();
        uint8 i;
        for (i = 0; i < 3; i++)
            if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_SHAPESHIFT ||
            spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MOD_STEALTH)
                break;
        if (i == 3 && !itr->second->IsPassive())
            sDatabase.PExecute("INSERT INTO `character_aura` (`guid`,`spell`,`effect_index`,`remaintime`) VALUES ('%u', '%u', '%u', '%d')", GetGUIDLow(), (uint32)(*itr).second->GetId(), (uint32)(*itr).second->GetEffIndex(), int((*itr).second->GetAuraDuration()));
    }
}

void Player::_SaveInventory()
{
    // force items in buyback slots to new state
    // and remove those that aren't already
    for (uint8 i = 0; i < 12; i++)
    {
        Item *item = m_buybackitems[i];
        if (!item || item->GetState() == ITEM_NEW) continue;
        sDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` = '%u'", item->GetGUIDLow());
        sDatabase.PExecute("DELETE FROM `item_instance` WHERE `guid` = '%u'", item->GetGUIDLow());
        m_buybackitems[i]->FSetState(ITEM_NEW);
    }

    if (m_itemUpdateQueue.empty()) return;

    // do not save if the update queue is corrupt
    bool error = false;
    for(int i = 0; i < m_itemUpdateQueue.size(); i++)
    {
        Item *item = m_itemUpdateQueue[i];
        if(!item || item->GetState() == ITEM_REMOVED) continue;
        Item *test = GetItemByPos( item->GetBagSlot(), item->GetSlot());

        if (test == NULL)
        {
            sLog.outError("Player::_SaveInventory - the bag(%d) and slot(%d) values for the item with guid %d are incorrect, the player doesn't have an item at that position!", item->GetBagSlot(), item->GetSlot(), item->GetGUIDLow());
            error = true;
        }
        else if (test != item)
        {
            sLog.outError("Player::_SaveInventory - the bag(%d) and slot(%d) values for the item with guid %d are incorrect, the item with guid %d is there instead!", item->GetBagSlot(), item->GetSlot(), item->GetGUIDLow(), test->GetGUIDLow());
            error = true;
        }
    }

    if (error)
    {
        sLog.outError("Player::_SaveInventory - one or more errors occured save aborted!");
        sChatHandler.SendSysMessage(GetSession(), "Item save failed!");
        return;
    }

    for(int i = 0; i < m_itemUpdateQueue.size(); i++)
    {
        Item *item = m_itemUpdateQueue[i];
        if(!item) continue;

        Bag *container = item->GetContainer();
        uint32 bag_guid = container ? container->GetGUIDLow() : 0;

        switch(item->GetState())
        {
            case ITEM_NEW:
                sDatabase.PExecute("INSERT INTO `character_inventory` (`guid`,`bag`,`slot`,`item`,`item_template`) VALUES ('%u', '%u', '%u', '%u', '%u')", GetGUIDLow(), bag_guid, item->GetSlot(), item->GetGUIDLow(), item->GetEntry());
                break;
            case ITEM_CHANGED:
                sDatabase.PExecute("UPDATE `character_inventory` SET `guid`='%u', `bag`='%u', `slot`='%u', `item_template`='%u' WHERE `item`='%u'", GetGUIDLow(), bag_guid, item->GetSlot(), item->GetEntry(), item->GetGUIDLow());
                break;
            case ITEM_REMOVED:
                sDatabase.PExecute("DELETE FROM `character_inventory` WHERE `item` = '%u'", item->GetGUIDLow());
        }

        item->SaveToDB();
    }
    m_itemUpdateQueue.clear();
}

void Player::_SaveMail()
{
    if (!m_mailsLoaded)
        return;

    sDatabase.PExecute("DELETE FROM `mail` WHERE `receiver` = '%u'",GetGUIDLow());

    std::list<Mail*>::iterator itr;
    for (itr = m_mail.begin(); itr != m_mail.end(); itr++)
    {
        Mail *m = (*itr);

        //escape apostrophes
        std::string subject = m->subject;
        std::string body = m->body;
        sDatabase.escape_string(body);
        sDatabase.escape_string(subject);

        sDatabase.PExecute("INSERT INTO `mail` (`id`,`sender`,`receiver`,`subject`,`body`,`item`,`item_template`,`time`,`money`,`cod`,`checked`) "
            "VALUES ('%u', '%u', '%u', '%s', '%s', '%u', '%u', '" I64FMTD "', '%u', '%u', '%u')",
            m->messageID, m->sender, m->receiver, subject.c_str(), body.c_str(), m->item_guidlow, m->item_id, (uint64)m->time, m->money, m->COD, m->checked);
    }
    m_mailsUpdated = false;
}

void Player::_SaveQuestStatus()
{
    sDatabase.PExecute("DELETE FROM `character_queststatus` WHERE `guid` = '%u'",GetGUIDLow());

    for( StatusMap::iterator i = mQuestStatus.begin( ); i != mQuestStatus.end( ); ++ i )
    {
        sDatabase.PExecute("INSERT INTO `character_queststatus` (`guid`,`quest`,`status`,`rewarded`,`explored`,`completed_once`,`timer`,`mobcount1`,`mobcount2`,`mobcount3`,`mobcount4`,`itemcount1`,`itemcount2`,`itemcount3`,`itemcount4`) VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '" I64FMTD "', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u')",
            GetGUIDLow(), i->first, i->second.m_status, i->second.m_rewarded, i->second.m_explored, i->second.m_completed_once, uint64(i->second.m_timer / 1000 + sWorld.GetGameTime()), i->second.m_creatureOrGOcount[0], i->second.m_creatureOrGOcount[1], i->second.m_creatureOrGOcount[2], i->second.m_creatureOrGOcount[3], i->second.m_itemcount[0], i->second.m_itemcount[1], i->second.m_itemcount[2], i->second.m_itemcount[3]);
    }
}

void Player::_SaveReputation()
{
    std::list<Factions>::iterator itr;

    sDatabase.PExecute("DELETE FROM `character_reputation` WHERE `guid` = '%u'",GetGUIDLow());

    for(itr = factions.begin(); itr != factions.end(); ++itr)
    {
        sDatabase.PExecute("INSERT INTO `character_reputation` (`guid`,`faction`,`reputation`,`standing`,`flags`) VALUES ('%u', '%u', '%u', '%u', '%u')", GetGUIDLow(), itr->ID, itr->ReputationListID, itr->Standing, itr->Flags);
    }
}

void Player::_SaveSpells()
{
    for (PlayerSpellMap::const_iterator itr = m_spells.begin(), next = m_spells.begin(); itr != m_spells.end(); itr = next)
    {
        next++;
        if (itr->second->state == PLAYERSPELL_REMOVED || itr->second->state == PLAYERSPELL_CHANGED)
            sDatabase.PExecute("DELETE FROM `character_spell` WHERE `guid` = '%u' and `spell` = '%u'", GetGUIDLow(), itr->first);
        if (itr->second->state == PLAYERSPELL_NEW || itr->second->state == PLAYERSPELL_CHANGED)
            sDatabase.PExecute("INSERT INTO `character_spell` (`guid`,`spell`,`slot`,`active`) VALUES ('%u', '%u', '%u','%u')", GetGUIDLow(), itr->first, itr->second->slotId,itr->second->active);
        if (itr->second->state == PLAYERSPELL_REMOVED)
            _removeSpell(itr->first);
        else
            itr->second->state = PLAYERSPELL_UNCHANGED;
    }
}

void Player::_SaveTutorials()
{
    sDatabase.PExecute("DELETE FROM `character_tutorial` WHERE `guid` = '%u'",GetGUIDLow());
    sDatabase.PExecute("INSERT INTO `character_tutorial` (`guid`,`tut0`,`tut1`,`tut2`,`tut3`,`tut4`,`tut5`,`tut6`,`tut7`) VALUES ('%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u', '%u')", GetGUIDLow(), m_Tutorials[0], m_Tutorials[1], m_Tutorials[2], m_Tutorials[3], m_Tutorials[4], m_Tutorials[5], m_Tutorials[6], m_Tutorials[7]);
}

void Player::SavePet()
{
    Creature* pet = GetPet();
    if(pet && (pet->isPet() || pet->isTamed()))
        pet->SaveAsPet();
}

void Player::outDebugValues() const
{
    sLog.outDebug("HP is: \t\t\t%u\t\tMP is: \t\t\t%u",GetMaxHealth(), GetMaxPower(POWER_MANA));
    sLog.outDebug("AGILITY is: \t\t%f\t\tSTRENGTH is: \t\t%f",GetStat(STAT_AGILITY), GetStat(STAT_STRENGTH));
    sLog.outDebug("INTELLECT is: \t\t%f\t\tSPIRIT is: \t\t%f",GetStat(STAT_INTELLECT), GetStat(STAT_SPIRIT));
    sLog.outDebug("STAMINA is: \t\t%f\t\tSPIRIT is: \t\t%f",GetStat(STAT_STAMINA), GetStat(STAT_SPIRIT));
    sLog.outDebug("Armor is: \t\t%f\t\tBlock is: \t\t%f",GetArmor(), GetFloatValue(PLAYER_BLOCK_PERCENTAGE));
    sLog.outDebug("HolyRes is: \t\t%f\t\tFireRes is: \t\t%f",GetResistance(SPELL_SCHOOL_HOLY), GetResistance(SPELL_SCHOOL_FIRE));
    sLog.outDebug("NatureRes is: \t\t%f\t\tFrostRes is: \t\t%f",GetResistance(SPELL_SCHOOL_NATURE), GetResistance(SPELL_SCHOOL_FROST));
    sLog.outDebug("ShadowRes is: \t\t%f\t\tArcaneRes is: \t\t%f",GetResistance(SPELL_SCHOOL_SHADOW), GetResistance(SPELL_SCHOOL_ARCANE));
    sLog.outDebug("MIN_DAMAGE is: \t\t%f\tMAX_DAMAGE is: \t\t%f",GetFloatValue(UNIT_FIELD_MINDAMAGE), GetFloatValue(UNIT_FIELD_MAXDAMAGE));
    sLog.outDebug("MIN_OFFHAND_DAMAGE is: \t%f\tMAX_OFFHAND_DAMAGE is: \t%f",GetFloatValue(UNIT_FIELD_MINOFFHANDDAMAGE), GetFloatValue(UNIT_FIELD_MAXOFFHANDDAMAGE));
    sLog.outDebug("MIN_RANGED_DAMAGE is: \t%f\tMAX_RANGED_DAMAGE is: \t%f",GetFloatValue(UNIT_FIELD_MINRANGEDDAMAGE), GetFloatValue(UNIT_FIELD_MAXRANGEDDAMAGE));
    sLog.outDebug("ATTACK_TIME is: \t%u\t\tRANGE_ATTACK_TIME is: \t%u",GetAttackTime(BASE_ATTACK), GetAttackTime(RANGED_ATTACK));
}

/*********************************************************/
/***              LOW LEVEL FUNCTIONS:Notifiers        ***/
/*********************************************************/

void Player::SendOutOfRange(Object* obj)
{
    UpdateData his_data;
    WorldPacket his_pk;
    obj->BuildOutOfRangeUpdateBlock(&his_data);
    his_data.BuildPacket(&his_pk);
    GetSession()->SendPacket(&his_pk);
}

inline void Player::SendAttackSwingNotInRange()
{
    WorldPacket data;
    data.Initialize(SMSG_ATTACKSWING_NOTINRANGE);
    GetSession()->SendPacket( &data );
}

void Player::SavePositionInDB(uint32 mapid, float x,float y,float z,float o,uint64 guid)
{
    std::ostringstream ss2;
    ss2 << "UPDATE `character` SET `position_x`='"<<x<<"',`position_y`='"<<y
        << "',`position_z`='"<<z<<"',`orientation`='"<<o<<"',`map`='"<<mapid
        << "' WHERE `guid`='"<<guid<<"'";
    sDatabase.Execute(ss2.str().c_str());
}

bool Player::SaveValuesArrayInDB(vector<string> const& tokens, uint64 guid)
{
    std::ostringstream ss2;
    ss2<<"UPDATE `character` SET `data`='";
    vector<string>::const_iterator iter;
    int i=0;
    for (iter = tokens.begin(); iter != tokens.end(); ++iter, ++i)
    {
        ss2<<tokens[i]<<" ";
    }
    ss2<<"' WHERE `guid`='"<<guid<<"'";

    return sDatabase.Execute(ss2.str().c_str());
}

void Player::SetUInt32ValueInArray(vector<string>& tokens,uint16 index, uint32 value)
{
    char buf[11];
    snprintf(buf,11,"%u",value);
    tokens[index] = buf;
}

void Player::SetUInt32ValueInDB(uint16 index, uint32 value, uint64 guid)
{
    vector<string> tokens;
    if(!LoadValuesArrayFromDB(tokens,guid))
        return;

    char buf[11];
    snprintf(buf,11,"%u",value);
    tokens[index] = buf;

    SaveValuesArrayInDB(tokens,guid);
}

void Player::SetFloatValueInDB(uint16 index, float value, uint64 guid)
{
    uint32 temp;
    memcpy(&temp, &value, sizeof(value));
    Player::SetUInt32ValueInDB(index, temp, guid);
}

inline void Player::SendAttackSwingNotStanding()
{
    WorldPacket data;
    data.Initialize(SMSG_ATTACKSWING_NOTSTANDING);
    GetSession()->SendPacket( &data );
}

inline void Player::SendAttackSwingDeadTarget()
{
    WorldPacket data;
    data.Initialize(SMSG_ATTACKSWING_DEADTARGET);
    GetSession()->SendPacket( &data );
}

inline void Player::SendAttackSwingCantAttack()
{
    WorldPacket data;
    data.Initialize(SMSG_ATTACKSWING_CANT_ATTACK);
    GetSession()->SendPacket( &data );
}

inline void Player::SendAttackSwingCancelAttack()
{
    WorldPacket data;
    data.Initialize(SMSG_CANCEL_COMBAT);
    GetSession()->SendPacket( &data );
}

inline void Player::SendAttackSwingBadFacingAttack()
{
    WorldPacket data;
    data.Initialize(SMSG_ATTACKSWING_BADFACING);
    GetSession()->SendPacket( &data );
}

void Player::PlaySound(uint32 Sound, bool OnlySelf)
{
    WorldPacket data;
    data.Initialize(SMSG_PLAY_SOUND);
    data << Sound;
    if (OnlySelf)
        GetSession()->SendPacket( &data );
    else
        SendMessageToSet( &data, true );
}

void Player::SendExplorationExperience(uint32 Area, uint32 Experience)
{
    WorldPacket data;
    data.Initialize( SMSG_EXPLORATION_EXPERIENCE );
    data << Area;
    data << Experience;
    GetSession()->SendPacket(&data);
}

/*********************************************************/
/***              Update timers                        ***/
/*********************************************************/

void Player::UpdatePVPFlag(time_t currTime)
{
    if( !GetPvP() ) return;

    //Player is counting to set/unset pvp flag
    if( !m_pvp_counting ) return;

    //Is player is in a PvP action stop counting
    if( isInCombatWithPlayer() )
    {
        m_pvp_counting = false;
        m_pvp_count = time(NULL);
        return;
    }

    //Wait 5 min until remove pvp mode
    if( currTime < m_pvp_count + 300 ) return;

    SetPvP(false);
    //sChatHandler.SendSysMessage(GetSession(), "PvP toggled off.");

}

void Player::UpdateDuelFlag(time_t currTime)
{
    if(!duel || duel->startTimer == 0) return;
    if(currTime < duel->startTimer + 3) return;

    SetUInt32Value(PLAYER_DUEL_TEAM, 1);
    duel->opponent->SetUInt32Value(PLAYER_DUEL_TEAM, 2);

    duel->startTimer = 0;
    duel->startTime  = currTime;
    duel->opponent->duel->startTimer = 0;
    duel->opponent->duel->startTime  = currTime;
}

void Player::UnsummonPet(Creature* pet)
{
    if(!pet)
        pet = GetPet();

    if(!pet||pet->GetGUID()!=GetPetGUID()) return;

    SetPet(0);

    pet->CombatStop();

    if(pet->isPet())
        pet->SaveAsPet();

    WorldPacket data;
    data.Initialize(SMSG_DESTROY_OBJECT);
    data << pet->GetGUID();
    SendMessageToSet (&data, true);

    data.Initialize(SMSG_PET_SPELLS);
    data << uint64(0);
    GetSession()->SendPacket(&data);

    ObjectAccessor::Instance().AddObjectToRemoveList(pet);
}

void Player::UnTamePet(Creature* pet)
{
    if(!pet)
        pet = GetPet();

    if(!pet||!pet->isTamed()||pet->GetGUID()!=GetPetGUID()) return;

    pet->SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE,pet->GetCreatureInfo()->faction);
    pet->SetMaxPower(POWER_HAPPINESS,0);
    pet->SetPower(POWER_HAPPINESS,0);
    pet->SetMaxPower(POWER_FOCUS,0);
    pet->SetPower(POWER_FOCUS,0);
    pet->SetUInt64Value(UNIT_FIELD_CREATEDBY, 0);
    pet->SetUInt32Value(UNIT_FIELD_PETNUMBER,0);
    pet->SetTamed(false);
    SetPet(0);

    pet->AIM_Initialize();

    WorldPacket data;

    data.Initialize(SMSG_PET_SPELLS);
    data << uint64(0);
    GetSession()->SendPacket(&data);
}

void Player::Uncharm()
{
    Creature* charm = GetCharm();
    if(!charm) return;

    SetCharm(0);

    CreatureInfo const *cinfo = charm->GetCreatureInfo();
    charm->SetUInt64Value(UNIT_FIELD_CHARMEDBY,0);
    charm->SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE,cinfo->faction);

    charm->AIM_Initialize();
    WorldPacket data;
    data.Initialize(SMSG_PET_SPELLS);
    data << uint64(0);
    GetSession()->SendPacket(&data);
}

void Player::PetSpellInitialize()
{
    Creature* pet = GetPet();
    if(!pet)
        pet = GetCharm();
    if(pet)
    {

        WorldPacket data;
        uint16 Command = 7;
        uint16 State = 6;
        uint8 addlist = 0;

        sLog.outDebug("Pet Spells Groups");

        data.clear();
        data.Initialize(SMSG_PET_SPELLS);

        data << (uint64)pet->GetGUID() << uint32(0x00000000) << uint32(0x1010000);

        data << uint16 (2) << uint16(Command << 8) << uint16 (1) << uint16(Command << 8) << uint16 (0) << uint16(Command << 8);

        for(uint32 i=0; i < CREATURE_MAX_SPELLS; i++)
                                                            //C100 = maybe group
            data << uint16 (pet->m_spells[i]) << uint16 (0xC100);

        data << uint16 (2) << uint16(State << 8) << uint16 (1) << uint16(State << 8) << uint16 (0) << uint16(State << 8);

        if(pet->GetUInt32Value(UNIT_FIELD_PETNUMBER))
        {
            for(PlayerSpellMap::iterator itr = m_spells.begin();itr != m_spells.end();itr++)
            {
                if(itr->second->active == 4)
                    addlist++;
            }
        }

        data << uint8(addlist);

        if(pet->GetUInt32Value(UNIT_FIELD_PETNUMBER))
        {
            for(PlayerSpellMap::iterator itr = m_spells.begin();itr != m_spells.end();itr++)
            {
                if(itr->second->active == 4)
                {
                    bool hasthisspell = false;

                    SpellEntry *spellInfo = sSpellStore.LookupEntry(itr->first);
                    data << uint16(spellInfo->EffectTriggerSpell[0]);
                    for(uint32 i=0; i < CREATURE_MAX_SPELLS; i++)
                    {
                        if(pet->m_spells[i] == spellInfo->EffectTriggerSpell[0])
                        {
                            data << uint16(0xC1);
                            hasthisspell = true;
                            break;
                        }
                    }
                    if(!hasthisspell)
                        data << uint16(0x01);
                }
            }
        }

        data << uint8(0x01) << uint32(0x6010) << uint32(0x00) << uint32(0x00) << uint16(0x00);

        GetSession()->SendPacket(&data);
    }
}

int32 Player::GetTotalFlatMods(uint32 spellId, uint8 op)
{
    SpellEntry *spellInfo = sSpellStore.LookupEntry(spellId);
    if (!spellInfo) return 0;
    int32 total = 0;
    for (SpellModList::iterator itr = m_spellMods[op].begin(); itr != m_spellMods[op].end(); ++itr)
    {
        SpellModifier *mod = *itr;
        if (!mod) continue;
        if ((mod->mask & spellInfo->SpellFamilyFlags) == 0) continue;
        if (mod->type == SPELLMOD_FLAT)
            total += mod->value;
    }
    return total;
}

int32 Player::GetTotalPctMods(uint32 spellId, uint8 op)
{
    SpellEntry *spellInfo = sSpellStore.LookupEntry(spellId);
    if (!spellInfo) return 0;
    int32 total = 0;
    for (SpellModList::iterator itr = m_spellMods[op].begin(); itr != m_spellMods[op].end(); ++itr)
    {
        SpellModifier *mod = *itr;
        if (!mod) continue;
        if ((mod->mask & spellInfo->SpellFamilyFlags) == 0) continue;
        if (mod->type == SPELLMOD_PCT)
            total += mod->value;
    }
    return total;
}

void Player::ApplyBlockValueMod(int32 val,bool apply)
{
    ApplyModUInt32Var(m_BlockValue,val,apply);
}

void Player::RemoveAreaAurasFromGroup()
{
    Group* pGroup = objmgr.GetGroupByLeader(this->GetGroupLeader());
    if(!pGroup)
        return;

    for(uint32 p=0;p<pGroup->GetMembersCount();p++)
    {
        Unit* Member = ObjectAccessor::Instance().FindPlayer(pGroup->GetMemberGUID(p));
        if(!Member)
            continue;
        Member->RemoveAreaAurasByOthers(GetGUID());
        for (uint8 i = 0; i < 4; i++)
            if (m_TotemSlot[i])
                Member->RemoveAreaAurasByOthers(m_TotemSlot[i]);
    }
}

// send Proficiency
void Player::SendProficiency(uint8 pr1, uint32 pr2)
{
    WorldPacket data;
    data.Initialize (SMSG_SET_PROFICIENCY);
    data << pr1 << pr2;
    GetSession()->SendPacket (&data);
}

void Player::RemovePetitionsAndSigns(uint64 guid)
{
    QueryResult *result = sDatabase.PQuery("SELECT `ownerguid`,`charterguid` FROM `guild_charter_sign` WHERE `playerguid` = '%u'", guid);
    if(result)
    {
        do
        {
            Field *fields = result->Fetch();
            uint64 ownerguid   = MAKE_GUID(fields[0].GetUInt32(),HIGHGUID_PLAYER);
            uint64 charterguid = MAKE_GUID(fields[1].GetUInt32(),HIGHGUID_ITEM);

            // send update  if charter owner in game
            Player* owner = objmgr.GetPlayer(ownerguid);
            if(owner)
                owner->GetSession()->SendPetitionQueryOpcode(charterguid);

        } while ( result->NextRow() );

        delete result;

        sDatabase.PExecute("DELETE FROM `guild_charter_sign` WHERE `playerguid` = '%u'",guid);
    }

    sDatabase.PExecute("DELETE FROM `guild_charter` WHERE `ownerguid` = '%u'",guid);
    sDatabase.PExecute("DELETE FROM `guild_charter_sign` WHERE `ownerguid` = '%u'",guid);
}

void Player::SetRestBonus (float rest_bonus_new)
{
    if(rest_bonus_new < 0)
        rest_bonus_new = 0;

    float rest_bonus_max = (float)GetUInt32Value(PLAYER_NEXT_LEVEL_XP)/2;

    if(rest_bonus_new > rest_bonus_max)
        rest_bonus = rest_bonus_max;
    else
        rest_bonus = rest_bonus_new;

    // update data for client
    if(rest_bonus>10)
    {
        SetFlag(PLAYER_BYTES_2, 0x1000000);                 // Set Reststate = Rested
        RemoveFlag(PLAYER_BYTES_2, 0x2000000);              // Remove Reststate = Normal
    }
    else if(rest_bonus<=0)
    {
        SetFlag(PLAYER_BYTES_2, 0x2000000);                 // Set Reststate = Normal
        RemoveFlag(PLAYER_BYTES_2, 0x1000000);              // Remove Reststate = Rested
    }

    //RestTickUpdate
    SetUInt32Value(PLAYER_REST_STATE_EXPERIENCE, rest_bonus);
}
