/* 
 * Copyright (C) 2005-2008 MaNGOS <http://www.mangosproject.org/>
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
#include "WorldPacket.h"
#include "WorldSession.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Opcodes.h"
#include "Log.h"
#include "UpdateMask.h"
#include "World.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Player.h"
#include "Pet.h"
#include "Unit.h"
#include "Spell.h"
#include "DynamicObject.h"
#include "SpellAuras.h"
#include "Group.h"
#include "UpdateData.h"
#include "MapManager.h"
#include "ObjectAccessor.h"
#include "CellImpl.h"
#include "Policies/SingletonImp.h"
#include "SharedDefines.h"
#include "Tools.h"
#include "LootMgr.h"
#include "VMapFactory.h"
#include "BattleGround.h"

#define SPELL_CHANNEL_UPDATE_INTERVAL 1000

extern pEffect SpellEffects[TOTAL_SPELL_EFFECTS];

bool IsQuestTameSpell(uint32 spellId)
{
    SpellEntry const *spellproto = sSpellStore.LookupEntry(spellId);
    if (!spellproto) return false;

    return spellproto->Effect[0] == SPELL_EFFECT_THREAT
        && spellproto->Effect[1] == SPELL_EFFECT_APPLY_AURA && spellproto->EffectApplyAuraName[1] == SPELL_AURA_DUMMY;
}

SpellCastTargets::SpellCastTargets()
{
    m_unitTarget = NULL;
    m_itemTarget = NULL;
    m_GOTarget   = NULL;

    m_unitTargetGUID   = 0;
    m_GOTargetGUID     = 0;
    m_CorpseTargetGUID = 0;
    m_itemTargetGUID   = 0;
    m_itemTargetEntry  = 0;

    m_srcX = m_srcY = m_srcZ = m_destX = m_destY = m_destZ = 0;
    m_strTarget = "";
    m_targetMask = 0;
}

SpellCastTargets::~SpellCastTargets()
{
}

void SpellCastTargets::setUnitTarget(Unit *target)
{
    if (!target)
        return;

    m_destX = target->GetPositionX();
    m_destY = target->GetPositionY();
    m_destZ = target->GetPositionZ();
    m_unitTarget = target;
    m_unitTargetGUID = target->GetGUID();
    m_targetMask |= TARGET_FLAG_UNIT;
}

void SpellCastTargets::setDestination(float x, float y, float z)
{
    m_destX = x;
    m_destY = y;
    m_destZ = z;
    m_targetMask |= TARGET_FLAG_DEST_LOCATION;
}

void SpellCastTargets::setGOTarget(GameObject *target)
{
    m_GOTarget = target;
    m_GOTargetGUID = target->GetGUID();
    //    m_targetMask |= TARGET_FLAG_OBJECT;
}

void SpellCastTargets::setItemTarget(Item* item)
{
    if(!item)
        return;

    m_itemTarget = item;
    m_itemTargetGUID = item->GetGUID();
    m_itemTargetEntry = item->GetEntry();
    m_targetMask |= TARGET_FLAG_ITEM;
}

void SpellCastTargets::setCorpseTarget(Corpse* corpse)
{
    m_CorpseTargetGUID = corpse->GetGUID();
}

void SpellCastTargets::Update(Unit* caster)
{
    m_GOTarget   = m_GOTargetGUID ? ObjectAccessor::GetGameObject(*caster,m_GOTargetGUID) : NULL;
    m_unitTarget = m_unitTargetGUID ?
        ( m_unitTargetGUID==caster->GetGUID() ? caster : ObjectAccessor::GetUnit(*caster, m_unitTargetGUID) ) :
    NULL;

    m_itemTarget = NULL;
    if(caster->GetTypeId()==TYPEID_PLAYER)
    {
        if(m_targetMask & TARGET_FLAG_ITEM)
            m_itemTarget = ((Player*)caster)->GetItemByGuid(m_itemTargetGUID);
        else
        {
            Player* pTrader = ((Player*)caster)->GetTrader();
            if(pTrader && m_itemTargetGUID < TRADE_SLOT_COUNT)
                m_itemTarget = pTrader->GetItemByPos(pTrader->GetItemPosByTradeSlot(m_itemTargetGUID));
        }
        if(m_itemTarget)
            m_itemTargetEntry = m_itemTarget->GetEntry();
    }
}

bool SpellCastTargets::read ( WorldPacket * data, Unit *caster )
{
    if(data->rpos()+2 > data->size())
        return false;

    *data >> m_targetMask;

    if(m_targetMask == TARGET_FLAG_SELF)
    {
        m_destX = caster->GetPositionX();
        m_destY = caster->GetPositionY();
        m_destZ = caster->GetPositionZ();
        m_unitTarget = caster;
        m_unitTargetGUID = caster->GetGUID();
        return true;
    }

    if( m_targetMask & TARGET_FLAG_UNIT )
        if(!readGUID(*data, m_unitTargetGUID))
            return false;

    if( m_targetMask & ( TARGET_FLAG_OBJECT | TARGET_FLAG_OBJECT_UNK ))
        if(!readGUID(*data, m_GOTargetGUID))
            return false;

    if(( m_targetMask & ( TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM )) && caster->GetTypeId() == TYPEID_PLAYER)
        if(!readGUID(*data, m_itemTargetGUID))
            return false;

    if( m_targetMask & TARGET_FLAG_SOURCE_LOCATION )
    {
        if(data->rpos()+4+4+4 > data->size())
            return false;

        *data >> m_srcX >> m_srcY >> m_srcZ;
        if(!MaNGOS::IsValidMapCoord(m_srcX, m_srcY))
            return false;
    }

    if( m_targetMask & TARGET_FLAG_DEST_LOCATION )
    {
        if(data->rpos()+4+4+4 > data->size())
            return false;

        *data >> m_destX >> m_destY >> m_destZ;
        if(!MaNGOS::IsValidMapCoord(m_destX, m_destY))
            return false;
    }

    if( m_targetMask & TARGET_FLAG_STRING )
    {
        if(data->rpos()+1 > data->size())
            return false;

        *data >> m_strTarget;
    }

    if( m_targetMask & (TARGET_FLAG_CORPSE | TARGET_FLAG_PVP_CORPSE ) )
        if(!readGUID(*data, m_CorpseTargetGUID))
            return false;

    // find real units/GOs
    Update(caster);
    return true;
}

void SpellCastTargets::write ( WorldPacket * data )
{
    *data << uint32(m_targetMask);

    if(m_targetMask & TARGET_FLAG_UNIT)
    {
        if(m_unitTarget)
            data->append(m_unitTarget->GetPackGUID());
        else
            *data << uint8(0);
    }

    if( m_targetMask & ( TARGET_FLAG_OBJECT | TARGET_FLAG_OBJECT_UNK ) )
    {
        if(m_GOTarget)
            data->append(m_GOTarget->GetPackGUID());
        else
            *data << uint8(0);
    }

    if( m_targetMask & ( TARGET_FLAG_ITEM | TARGET_FLAG_TRADE_ITEM ) )
    {
        if(m_itemTarget)
            data->append(m_itemTarget->GetPackGUID());
        else
            *data << uint8(0);
    }

    if( m_targetMask & TARGET_FLAG_SOURCE_LOCATION )
        *data << m_srcX << m_srcY << m_srcZ;

    if( m_targetMask & TARGET_FLAG_DEST_LOCATION )
        *data << m_destX << m_destY << m_destZ;

    if( m_targetMask & TARGET_FLAG_STRING )
        *data << m_strTarget;

    if( m_targetMask & ( TARGET_FLAG_CORPSE | TARGET_FLAG_PVP_CORPSE ) )
        data->appendPackGUID(m_CorpseTargetGUID);
}

Spell::Spell( Unit* Caster, SpellEntry const *info, bool triggered, uint64 originalCasterGUID, Spell** triggeringContainer )
{
    ASSERT( Caster != NULL && info != NULL );
    ASSERT( info == sSpellStore.LookupEntry( info->Id ) && "`info` must be pointer to sSpellStore element");

    m_spellInfo = info;
    m_caster = Caster;
    m_selfContainer = NULL;
    m_triggeringContainer = triggeringContainer;
    m_deletable = true;

    // Get data for type of attack
    switch (m_spellInfo->DmgClass)
    {
     case SPELL_DAMAGE_CLASS_MELEE:
         if (m_spellInfo->AttributesEx3 & 0x1000000) m_attackType = OFF_ATTACK;
         else                                        m_attackType = BASE_ATTACK;
         break;
     case SPELL_DAMAGE_CLASS_RANGED: 
         m_attackType = RANGED_ATTACK; 
         break;
     default:
         if (m_spellInfo->Id == 5019) m_attackType = RANGED_ATTACK; // Wands
         else                         m_attackType = BASE_ATTACK;
         break;
    }

    m_spellSchoolMask = GetSpellSchoolMask(info);           // Can be override for some spell (wand shoot for example)

    if(m_attackType == RANGED_ATTACK)
    {
        // wand case
        if((m_caster->getClassMask() & CLASSMASK_WAND_USERS) != 0 && m_caster->GetTypeId()==TYPEID_PLAYER)
        {
            if(Item* pItem = ((Player*)m_caster)->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED))
                m_spellSchoolMask = SpellSchoolMask(1 << pItem->GetProto()->Damage->DamageType);
        }
    }

    if(originalCasterGUID)
        m_originalCasterGUID = originalCasterGUID;
    else
        m_originalCasterGUID = m_caster->GetGUID();

    if(m_originalCasterGUID==m_caster->GetGUID())
        m_originalCaster = m_caster;
    else
        m_originalCaster = ObjectAccessor::GetUnit(*m_caster,m_originalCasterGUID);

    for(int i=0; i <3; ++i)
        m_currentBasePoints[i] = m_spellInfo->EffectBasePoints[i];

    m_spellState = SPELL_STATE_NULL;

    m_castPositionX = m_castPositionY = m_castPositionZ = 0;
    m_TriggerSpells.clear();
    m_IsTriggeredSpell = triggered;
    //m_AreaAura = false;
    m_CastItem = NULL;

    unitTarget = NULL;
    itemTarget = NULL;
    gameObjTarget = NULL;
    focusObject = NULL;
    m_cast_count = 0;
    m_triggeredByAuraSpell  = NULL;

    m_autoRepeat = false;
    if( m_spellInfo->AttributesEx2 == 0x000020 )            //Auto Shot & Shoot
        m_autoRepeat = true;

    m_casttime = 0;                                         // setup to correct value in Spell::prepare, don't must be used before.
    m_timer = 0;                                            // will set to castime in preper

    m_needAliveTargetMask = 0;

    m_meleeSpell = false;

    m_rangedShoot = ((m_spellInfo->Attributes & 18) == 18);
    if( m_spellInfo->StartRecoveryTime == 0 && !m_autoRepeat && !m_rangedShoot )
    {
        for (int i = 0; i < 3; i++)
        {
            if (m_spellInfo->Effect[i]==SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL ||
                m_spellInfo->Effect[i]==SPELL_EFFECT_WEAPON_DAMAGE)
            {
                m_meleeSpell = true;
                break;
            }
        }
    }

    // determine reflection
    m_canReflect = false;

    if(m_spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MAGIC && (m_spellInfo->AttributesEx2 & 0x4)==0)
    {
        for(int j=0;j<3;j++)
        {
            if (m_spellInfo->Effect[j]==0)
                continue;

            if(!IsPositiveTarget(m_spellInfo->EffectImplicitTargetA[j],m_spellInfo->EffectImplicitTargetB[j]))
                m_canReflect = true;
            else
                m_canReflect = (m_spellInfo->AttributesEx & (1<<7)) ? true : false;

            if(m_canReflect)
                continue;
            else
                break;
        }
    }

    CleanupTargetList();
}

Spell::~Spell()
{
}

void Spell::FillTargetMap()
{
    // TODO: ADD the correct target FILLS!!!!!!

    for(uint32 i=0;i<3;i++)
    {
        // not call for empty effect.
        // Also some spells use not used effect targets for store targets for dummy effect in triggered spells
        if(m_spellInfo->Effect[i]==0)
            continue;

        // targets for TARGET_SCRIPT filled in Spell::canCast call
        if( m_spellInfo->EffectImplicitTargetA[i] == TARGET_SCRIPT || m_spellInfo->EffectImplicitTargetB[i] == TARGET_SCRIPT )
            continue;

        std::list<Unit*> tmpUnitMap;

        // TargetA/TargetB dependent from each other, we not switch to full support this dependences
        // but need it support in some know cases
        switch(m_spellInfo->EffectImplicitTargetA[i])
        {
            case TARGET_ALL_AROUND_CASTER:
                if( m_spellInfo->EffectImplicitTargetB[i]==TARGET_ALL_PARTY ||
                    m_spellInfo->EffectImplicitTargetB[i]==TARGET_ALL_FRIENDLY_UNITS_AROUND_CASTER ||
                    m_spellInfo->EffectImplicitTargetB[i]==TARGET_RANDOM_RAID_MEMBER )
                {
                    SetTargetMap(i,m_spellInfo->EffectImplicitTargetB[i],tmpUnitMap);
                }
                // Note: this hack with search required until GO casting not implemented
                // enviromenment damage spells already have around enemies targeting but this not help in case not existed GO casting support
                // currently each eanemy selected explicitly and self cast damage
                else if(m_spellInfo->EffectImplicitTargetB[i]==TARGET_ALL_ENEMY_IN_AREA && m_spellInfo->Effect[i]==SPELL_EFFECT_ENVIRONMENTAL_DAMAGE)
                {
                    if(m_targets.getUnitTarget())
                        tmpUnitMap.push_back(m_targets.getUnitTarget());
                }
                else
                {
                    SetTargetMap(i,m_spellInfo->EffectImplicitTargetA[i],tmpUnitMap);
                    SetTargetMap(i,m_spellInfo->EffectImplicitTargetB[i],tmpUnitMap);
                }
                break;
            case TARGET_CURRENT_SELECTED_ENEMY:
                SetTargetMap(i,m_spellInfo->EffectImplicitTargetA[i],tmpUnitMap);
                break;
            default:
                SetTargetMap(i,m_spellInfo->EffectImplicitTargetA[i],tmpUnitMap);
                SetTargetMap(i,m_spellInfo->EffectImplicitTargetB[i],tmpUnitMap);
                break;
        }

        if( (m_spellInfo->EffectImplicitTargetA[i]==0 || m_spellInfo->EffectImplicitTargetA[i]==TARGET_EFFECT_SELECT) &&
            (m_spellInfo->EffectImplicitTargetB[i]==0 || m_spellInfo->EffectImplicitTargetB[i]==TARGET_EFFECT_SELECT) )
        {
            // add here custom effects that need default target.
            // FOR EVERY TARGET TYPE THERE IS A DIFFERENT FILL!!
            switch(m_spellInfo->Effect[i])
            {
                //case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                case SPELL_EFFECT_RESURRECT:
                case SPELL_EFFECT_LEARN_SPELL:
                case SPELL_EFFECT_SKILL_STEP:
                case SPELL_EFFECT_SELF_RESURRECT:
                case SPELL_EFFECT_PROFICIENCY:
                case SPELL_EFFECT_PARRY:
                case SPELL_EFFECT_CREATE_ITEM:
                    if(m_targets.getUnitTarget())
                        tmpUnitMap.push_back(m_targets.getUnitTarget());
                    break;
                case SPELL_EFFECT_DUMMY:
                {
                    switch(m_spellInfo->Id)
                    {
                        case 20577:                         // Cannibalize
                        {
                            // non-standard target selection
                            SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex);
                            float max_range = GetSpellMaxRange(srange);

                            CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                            Cell cell(p);
                            cell.data.Part.reserved = ALL_DISTRICT;
                            cell.SetNoCreate();

                            WorldObject* result = NULL;

                            MaNGOS::CannibalizeObjectCheck u_check(m_caster, max_range);
                            MaNGOS::WorldObjectSearcher<MaNGOS::CannibalizeObjectCheck > searcher(result, u_check);

                            TypeContainerVisitor<MaNGOS::WorldObjectSearcher<MaNGOS::CannibalizeObjectCheck >, GridTypeMapContainer > grid_searcher(searcher);
                            CellLock<GridReadGuard> cell_lock(cell, p);
                            cell_lock->Visit(cell_lock, grid_searcher, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));

                            if(!result)
                            {
                                TypeContainerVisitor<MaNGOS::WorldObjectSearcher<MaNGOS::CannibalizeObjectCheck >, WorldTypeMapContainer > world_searcher(searcher);
                                cell_lock->Visit(cell_lock, world_searcher, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                            }

                            if(result)
                            {
                                switch(result->GetTypeId())
                                {
                                    case TYPEID_UNIT:
                                    case TYPEID_PLAYER:
                                        tmpUnitMap.push_back((Unit*)result);
                                        break;
                                    case TYPEID_CORPSE:
                                        m_targets.setCorpseTarget((Corpse*)result);
                                        if(Player* owner = ObjectAccessor::FindPlayer(((Corpse*)result)->GetOwnerGUID()))
                                            tmpUnitMap.push_back(owner);
                                        break;
                                }
                            }
                            else
                            {
                                // clear cooldown at fail
                                if(m_caster->GetTypeId()==TYPEID_PLAYER)
                                {
                                    ((Player*)m_caster)->RemoveSpellCooldown(m_spellInfo->Id);

                                    WorldPacket data(SMSG_CLEAR_COOLDOWN, (4+8));
                                    data << uint32(m_spellInfo->Id);
                                    data << uint64(m_caster->GetGUID());
                                    ((Player*)m_caster)->GetSession()->SendPacket(&data);
                                }

                                SendCastResult(SPELL_FAILED_NO_EDIBLE_CORPSES);
                                finish(false);
                            }
                            break;
                        }
                        default:
                            if(m_targets.getUnitTarget())
                                tmpUnitMap.push_back(m_targets.getUnitTarget());
                            break;
                    }
                    break;
                }
                case SPELL_EFFECT_SUMMON_PLAYER:
                    if(m_caster->GetTypeId()==TYPEID_PLAYER && ((Player*)m_caster)->GetSelection())
                    {
                        Player* target = objmgr.GetPlayer(((Player*)m_caster)->GetSelection());
                        if(target)
                            tmpUnitMap.push_back(target);
                    }
                    break;
                case SPELL_EFFECT_RESURRECT_NEW:
                    if(m_targets.getUnitTarget())
                        tmpUnitMap.push_back(m_targets.getUnitTarget());
                    if(m_targets.getCorpseTargetGUID())
                    {
                        Corpse *corpse = ObjectAccessor::GetCorpse(*m_caster,m_targets.getCorpseTargetGUID());
                        if(corpse)
                        {
                            Player* owner = ObjectAccessor::FindPlayer(corpse->GetOwnerGUID());
                            if(owner)
                                tmpUnitMap.push_back(owner);
                        }
                    }
                    break;
                case SPELL_EFFECT_SKILL:
                case SPELL_EFFECT_SUMMON_CHANGE_ITEM:
                case SPELL_EFFECT_SUMMON_GUARDIAN:
                case SPELL_EFFECT_SUMMON:
                case SPELL_EFFECT_SUMMON_WILD:
                case SPELL_EFFECT_STUCK:
                case SPELL_EFFECT_ADD_FARSIGHT:
                case SPELL_EFFECT_DESTROY_ALL_TOTEMS:
                case SPELL_EFFECT_SUMMON_DEMON:
                case SPELL_EFFECT_TRANS_DOOR:
                    tmpUnitMap.push_back(m_caster);
                    break;
                case SPELL_EFFECT_LEARN_PET_SPELL:
                    if(Pet* pet = m_caster->GetPet())
                        tmpUnitMap.push_back(pet);
                    break;
                case SPELL_EFFECT_FEED_PET:
                case SPELL_EFFECT_PROSPECTING:
                case SPELL_EFFECT_DISENCHANT:
                case SPELL_EFFECT_ENCHANT_ITEM:
                case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
                    if(m_targets.getItemTarget())
                        AddItemTarget(m_targets.getItemTarget(), i);
                    break;
                case SPELL_EFFECT_APPLY_AURA:
                    switch(m_spellInfo->EffectApplyAuraName[i])
                    {
                        case SPELL_AURA_ADD_FLAT_MODIFIER:  // some spell mods auras have 0 target modes instead expected TRAGET_SELF(1) (and present for other ranks for same spell for example)
                        case SPELL_AURA_ADD_PCT_MODIFIER:
                            tmpUnitMap.push_back(m_caster);
                            break;
                        default:                            // apply to target in other case
                            if(m_targets.getUnitTarget())
                                tmpUnitMap.push_back(m_targets.getUnitTarget());
                            break;
                    }
                    break;
                case SPELL_EFFECT_APPLY_AREA_AURA:
                                                            // AreaAura
                    if(m_spellInfo->Attributes == 0x9050000 || m_spellInfo->Attributes == 0x10000)
                        SetTargetMap(i,TARGET_AREAEFFECT_PARTY,tmpUnitMap);
                    break;
                case SPELL_EFFECT_SKIN_PLAYER_CORPSE:
                    if(m_targets.getUnitTarget())
                    {
                        tmpUnitMap.push_back(m_targets.getUnitTarget());
                    }
                    else if (m_targets.getCorpseTargetGUID())
                    {
                        Corpse *corpse = ObjectAccessor::GetCorpse(*m_caster,m_targets.getCorpseTargetGUID());
                        if(corpse)
                        {
                            Player* owner = ObjectAccessor::FindPlayer(corpse->GetOwnerGUID());
                            if(owner)
                                tmpUnitMap.push_back(owner);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        if(IsChanneledSpell(m_spellInfo) && !tmpUnitMap.empty())
            m_needAliveTargetMask  |= (1<<i);

        if(m_caster->GetTypeId() == TYPEID_PLAYER && (!m_caster->IsPvP() || ((Player*)m_caster)->pvpInfo.endTimer != 0))
        {
            Player *me = (Player*)m_caster;
            for (std::list<Unit*>::const_iterator itr = tmpUnitMap.begin(); itr != tmpUnitMap.end(); itr++)
            {
                Unit *owner = (*itr)->GetOwner();
                Unit *u = owner ? owner : (*itr);
                if(u->IsPvP() && (!me->duel || me->duel->opponent != u))
                    me->UpdatePvP(true);
            }
        }

        for (std::list<Unit*>::iterator itr = tmpUnitMap.begin() ; itr != tmpUnitMap.end();)
        {
            if(!CheckTarget(*itr, i, false ))
            {
                itr = tmpUnitMap.erase(itr);
                continue;
            }
            else
                ++itr;
        }

        for(std::list<Unit*>::iterator iunit= tmpUnitMap.begin();iunit != tmpUnitMap.end();++iunit)
            AddUnitTarget((*iunit), i);
    }
}

void Spell::CleanupTargetList()
{
    m_UniqueTargetInfo.clear();
    m_UniqueGOTargetInfo.clear();
    m_UniqueItemInfo.clear();
    m_countOfHit = 0;
    m_countOfMiss = 0;
    m_delayMoment = 0;
}

void Spell::AddUnitTarget(Unit* pVictim, uint32 effIndex)
{
    if( m_spellInfo->Effect[effIndex]==0 )
        return;

    uint64 targetGUID = pVictim->GetGUID();

    // Lookup target in already in list
    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
    {
        if (targetGUID == ihit->targetGUID)                 // Found in list
        {
            ihit->effectMask |= 1<<effIndex;                // Add only effect mask
            return;
        }
    }

    // This is new target calculate data for him

    // Get spell hit result on target
    TargetInfo target;
    target.targetGUID = targetGUID;                         // Store target GUID
    target.effectMask = 1<<effIndex;                        // Store index of effect

    // Calculate hit result
    target.missCondition = m_caster->SpellHitResult(pVictim, m_spellInfo, m_canReflect);
    if (target.missCondition == SPELL_MISS_NONE)
        ++m_countOfHit;
    else
        ++m_countOfMiss;

    // Spell have speed - need calculate incoming time
    if (m_spellInfo->speed > 0.0f)
    {
        // calculate spell incoming interval
        float dist = m_caster->GetDistance(pVictim->GetPositionX(), pVictim->GetPositionY(), pVictim->GetPositionZ());
        if (dist < 5.0f) dist = 5.0f;
        target.timeDelay = (uint64) floor(dist / m_spellInfo->speed * 1000.0f);

        // Calculate minimum incoming time
        if (m_delayMoment==0 || m_delayMoment>target.timeDelay)
            m_delayMoment = target.timeDelay;
    }
    else
        target.timeDelay = 0LL;

    // If target reflect spell back to caster
    if (target.missCondition==SPELL_MISS_REFLECT)
    {
        // Calculate reflected spell result on caster
        target.reflectResult =  m_caster->SpellHitResult(m_caster, m_spellInfo, m_canReflect);

        if (target.reflectResult == SPELL_MISS_REFLECT)     // Impossible reflect again, so simply deflect spell
            target.reflectResult = SPELL_MISS_PARRY;

        // Increase time interval for reflected spells by 1.5
        target.timeDelay+=target.timeDelay>>1;
    }
    else
        target.reflectResult = SPELL_MISS_NONE;

    // Add target to list
    m_UniqueTargetInfo.push_back(target);
}

void Spell::AddUnitTarget(uint64 unitGUID, uint32 effIndex)
{
    Unit* unit = m_caster->GetGUID()==unitGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, unitGUID);
    if (unit)
        AddUnitTarget(unit, effIndex);
}

void Spell::AddGOTarget(GameObject* pVictim, uint32 effIndex)
{
    if( m_spellInfo->Effect[effIndex]==0 )
        return;

    uint64 targetGUID = pVictim->GetGUID();

    // Lookup target in already in list
    for(std::list<GOTargetInfo>::iterator ihit= m_UniqueGOTargetInfo.begin();ihit != m_UniqueGOTargetInfo.end();++ihit)
    {
        if (targetGUID == ihit->targetGUID)                 // Found in list
        {
            ihit->effectMask |= 1<<effIndex;                // Add only effect mask
            return;
        }
    }

    // This is new target calculate data for him

    GOTargetInfo target;
    target.targetGUID = targetGUID;
    target.effectMask = 1<<effIndex;

    // Spell have speed - need calculate incoming time
    if (m_spellInfo->speed > 0.0f)
    {
        // calculate spell incoming interval
        float dist = m_caster->GetDistance(pVictim->GetPositionX(), pVictim->GetPositionY(), pVictim->GetPositionZ());
        if (dist < 5.0f) dist = 5.0f;
        target.timeDelay = (uint64) floor(dist / m_spellInfo->speed * 1000.0f);
        if (m_delayMoment==0 || m_delayMoment>target.timeDelay)
            m_delayMoment = target.timeDelay;
    }
    else
        target.timeDelay = 0LL;

    ++m_countOfHit;

    // Add target to list
    m_UniqueGOTargetInfo.push_back(target);
}

void Spell::AddGOTarget(uint64 goGUID, uint32 effIndex)
{
    GameObject* go = ObjectAccessor::GetGameObject(*m_caster, goGUID);
    if (go)
        AddGOTarget(go, effIndex);
}

void Spell::AddItemTarget(Item* pitem, uint32 effIndex)
{
    if( m_spellInfo->Effect[effIndex]==0 )
        return;

    // Lookup target in already in list
    for(std::list<ItemTargetInfo>::iterator ihit= m_UniqueItemInfo.begin();ihit != m_UniqueItemInfo.end();++ihit)
    {
        if (pitem == ihit->item)                            // Found in list
        {
            ihit->effectMask |= 1<<effIndex;                // Add only effect mask
            return;
        }
    }

    // This is new target add data

    ItemTargetInfo target;
    target.item       = pitem;
    target.effectMask = 1<<effIndex;
    m_UniqueItemInfo.push_back(target);
}

void Spell::doTriggers(SpellMissInfo missInfo, uint32 damage, uint32 block, uint32 absorb, bool crit)
{
    // Do triggers depenends from hit result (triggers on hit do in effects)
    // Set aura states depends from hit result
    if (missInfo!=SPELL_MISS_NONE)
    {
        // Miss/dodge/parry/block only for melee based spells
        // Resist only for magic based spells
        switch (missInfo)
        {
            case SPELL_MISS_MISS:
                if(m_caster->GetTypeId()== TYPEID_PLAYER)
                    ((Player*)m_caster)->UpdateWeaponSkill(BASE_ATTACK);

                m_caster->CastMeleeProcDamageAndSpell(unitTarget, 0, m_attackType, MELEE_HIT_MISS, m_spellInfo, m_IsTriggeredSpell);
                break;
            case SPELL_MISS_RESIST:
                m_caster->ProcDamageAndSpell(unitTarget, PROC_FLAG_TARGET_RESISTS, PROC_FLAG_RESIST_SPELL, 0, m_spellInfo, m_IsTriggeredSpell);
                break;
            case SPELL_MISS_DODGE:
                if(unitTarget->GetTypeId() == TYPEID_PLAYER)
                    ((Player*)unitTarget)->UpdateDefense();

                // Overpower
                if (m_caster->GetTypeId() == TYPEID_PLAYER && m_caster->getClass() == CLASS_WARRIOR)
                {
                   ((Player*) m_caster)->AddComboPoints(unitTarget, 1);
                    m_caster->StartReactiveTimer( REACTIVE_OVERPOWER );
                }

                // Riposte
                if (unitTarget->getClass() != CLASS_ROGUE)
                {
                    unitTarget->ModifyAuraState(AURA_STATE_DEFENSE, true);
                    unitTarget->StartReactiveTimer( REACTIVE_DEFENSE );
                }

                m_caster->CastMeleeProcDamageAndSpell(unitTarget, 0, m_attackType, MELEE_HIT_DODGE, m_spellInfo, m_IsTriggeredSpell);
                break;
            case SPELL_MISS_PARRY:
                // Update victim defense ?
                if(unitTarget->GetTypeId() == TYPEID_PLAYER)
                    ((Player*)unitTarget)->UpdateDefense();
                // Mongoose bite - set only Counterattack here
                if (unitTarget->getClass() == CLASS_HUNTER)
                {
                    unitTarget->ModifyAuraState(AURA_STATE_HUNTER_PARRY,true);
                    unitTarget->StartReactiveTimer( REACTIVE_HUNTER_PARRY );
                }
                else
                {
                    unitTarget->ModifyAuraState(AURA_STATE_DEFENSE, true);
                    unitTarget->StartReactiveTimer( REACTIVE_DEFENSE );
                }
                m_caster->CastMeleeProcDamageAndSpell(unitTarget, 0, m_attackType, MELEE_HIT_PARRY, m_spellInfo, m_IsTriggeredSpell);              
                break;
            case SPELL_MISS_BLOCK:
                unitTarget->ModifyAuraState(AURA_STATE_DEFENSE, true);
                unitTarget->StartReactiveTimer( REACTIVE_DEFENSE );

                m_caster->CastMeleeProcDamageAndSpell(unitTarget, 0, m_attackType, MELEE_HIT_BLOCK, m_spellInfo, m_IsTriggeredSpell);              
                break;
            // Trigger from this events not supported
            case SPELL_MISS_EVADE:
            case SPELL_MISS_IMMUNE:
            case SPELL_MISS_IMMUNE2:
            case SPELL_MISS_DEFLECT:
            case SPELL_MISS_ABSORB:
            // Trigger from reflects need do after get reflect result
            case SPELL_MISS_REFLECT:
                break;
            default:
                break;
        }
    }
}

void Spell::DoAllEffectOnTarget(TargetInfo *target)
{
    // Get mask of effects for target
    uint32 mask = target->effectMask;
    if (mask == 0)                                          // No effects
        return;

    Unit* unit = m_caster->GetGUID()==target->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster,target->targetGUID);
    if (unit==NULL)
        return;

    SpellMissInfo missInfo = target->missCondition;
    // Need init unitTarget by default unit (can changed in code on reflect)
    // Or on missInfo!=SPELL_MISS_NONE unitTarget undefined (but need in trigger subsystem)
    unitTarget = unit;

    if (missInfo==SPELL_MISS_NONE)                          // In case spell hit target, do all effect on that target
        DoSpellHitOnUnit(unit, mask);
    else if (missInfo == SPELL_MISS_REFLECT)                // In case spell reflect from target, do all effect on caster (if hit)
    {
        if (target->reflectResult == SPELL_MISS_NONE)       // If reflected spell hit caster -> do all effect on him
            DoSpellHitOnUnit(m_caster, mask);
    }

    // Do triggers only on miss/resist/parry/dodge
    if (missInfo!=SPELL_MISS_NONE)
        doTriggers(missInfo);

    // Call scripted function for AI if this spell is casted upon a creature (except pets)
    if(IS_CREATURE_GUID(target->targetGUID))
    {
        // cast at creature (or GO) quest objectives update at successful cast finished (+channel finished)
        // ignore autorepeat/melee casts for speed (not exist quest for spells (hm... )
        if( m_caster->GetTypeId() == TYPEID_PLAYER && !IsAutoRepeat() && !IsMeleeSpell() && !IsChannelActive() )
            ((Player*)m_caster)->CastedCreatureOrGO(unit->GetEntry(),unit->GetGUID(),m_spellInfo->Id);

        if(((Creature*)unit)->AI())
            ((Creature*)unit)->AI()->SpellHit(m_caster ,m_spellInfo);
    }
}

void Spell::DoSpellHitOnUnit(Unit *unit, const uint32 effectMask)
{
    if(!unit || !effectMask)
        return;

    // Recheck immune (only for delayed spells)
    if (m_spellInfo->speed && (unit->IsImmunedToSpellDamage(m_spellInfo,true) || unit->IsImmunedToSpell(m_spellInfo,true)))
    {
        m_caster->SendSpellMiss(unit, m_spellInfo->Id, SPELL_MISS_IMMUNE);
        return;
    }

    for(uint32 effectNumber=0;effectNumber<3;effectNumber++)
    {
        if (effectMask & (1<<effectNumber))
        {
            HandleEffects(unit,NULL,NULL,effectNumber,m_damageMultipliers[effectNumber]);
            if ( m_applyMultiplier[effectNumber] )
            {
                // Get multiplier
                float multiplier = m_spellInfo->DmgMultiplier[effectNumber];
                // Apply multiplier mods
                if(Player* modOwner = m_originalCaster->GetSpellModOwner())
                    modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_EFFECT_PAST_FIRST, multiplier,this);
                m_damageMultipliers[effectNumber] *= multiplier;
            }
        }
    }
}

void Spell::DoAllEffectOnTarget(GOTargetInfo *target)
{
    uint32 effectMask = target->effectMask;
    if(!effectMask)
        return;

    GameObject* go = ObjectAccessor::GetGameObject(*m_caster, target->targetGUID);
    if(!go)
        return;

    for(uint32 effectNumber=0;effectNumber<3;effectNumber++)
        if (effectMask & (1<<effectNumber))
            HandleEffects(NULL,NULL,go,effectNumber);

    // cast at creature (or GO) quest objectives update at succesful cast finished (+channel finished)
    // ignore autorepeat/melee casts for speed (not exist quest for spells (hm... )
    if( m_caster->GetTypeId() == TYPEID_PLAYER && !IsAutoRepeat() && !IsMeleeSpell() && !IsChannelActive() )
        ((Player*)m_caster)->CastedCreatureOrGO(go->GetEntry(),go->GetGUID(),m_spellInfo->Id);
}

void Spell::DoAllEffectOnTarget(ItemTargetInfo *target)
{
    uint32 effectMask = target->effectMask;
    if(!target->item || !effectMask)
        return;

    for(uint32 effectNumber=0;effectNumber<3;effectNumber++)
        if (effectMask & (1<<effectNumber))
            HandleEffects(NULL, target->item, NULL, effectNumber);
}

bool Spell::IsAliveUnitPresentInTargetList()
{
    // Not need check return true
    if (m_needAliveTargetMask == 0)
        return true;

    uint8 needAliveTargetMask = m_needAliveTargetMask;

    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
    {
        if( ihit->missCondition == SPELL_MISS_NONE && (needAliveTargetMask & ihit->effectMask) )
        {
            Unit *unit = m_caster->GetGUID()==ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);

            if (unit && unit->isAlive())
                needAliveTargetMask &= ~ihit->effectMask;   // remove from need alive mask effect that have alive target
        }
    }

    // is all effects from m_needAliveTargetMask have alive targets
    return needAliveTargetMask==0;
}

// Helper for Chain Healing
// Spell target first
// Raidmates then descending by injury suffered (MaxHealth - Health)
// Other players/mobs then descending by injury suffered (MaxHealth - Health)
struct ChainHealingOrder : public std::binary_function<const Unit*, const Unit*, bool>
{
    const Unit* MainTarget;
    ChainHealingOrder(Unit const* Target) : MainTarget(Target) {};
    // functor for operator ">"
    bool operator()(Unit const* _Left, Unit const* _Right) const
    {
        return (ChainHealingHash(_Left) < ChainHealingHash(_Right));
    }
    int32 ChainHealingHash(Unit const* Target) const
    {
        if (Target == MainTarget)
            return 0;
        else if (Target->GetTypeId() == TYPEID_PLAYER && MainTarget->GetTypeId() == TYPEID_PLAYER &&
            ((Player const*)Target)->IsInSameRaidWith((Player const*)MainTarget))
        {
            if (Target->GetHealth() == Target->GetMaxHealth())
                return 40000;
            else
                return 20000 - Target->GetMaxHealth() + Target->GetHealth();
        }
        else
            return 40000 - Target->GetMaxHealth() + Target->GetHealth();
    }
};

class ChainHealingFullHealth: std::unary_function<const Unit*, bool>
{
    public:
        const Unit* MainTarget;
        ChainHealingFullHealth(const Unit* Target) : MainTarget(Target) {};

        bool operator()(const Unit* Target)
        {
            return (Target != MainTarget && Target->GetHealth() == Target->GetMaxHealth());
        }
};

// Helper for targets nearest to the spell target
// The spell target is always first unless there is a target at _completely_ the same position (unbelievable case)
struct TargetDistanceOrder : public std::binary_function<const Unit, const Unit, bool>
{
    const Unit* MainTarget;
    TargetDistanceOrder(const Unit* Target) : MainTarget(Target) {};
    // functor for operator ">"
    bool operator()(const Unit* _Left, const Unit* _Right) const
    {
        return (MainTarget->GetDistance(_Left) < MainTarget->GetDistance(_Right));
    }
};

void Spell::SetTargetMap(uint32 i,uint32 cur,std::list<Unit*> &TagUnitMap)
{
    float radius;
    if (m_spellInfo->EffectRadiusIndex[i])
        radius = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[i]));
    else
        radius = GetSpellMaxRange(sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex));

    if(m_originalCaster)
        if(Player* modOwner = m_originalCaster->GetSpellModOwner())
            modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_RADIUS, radius,this);

    uint32 unMaxTargets = m_spellInfo->MaxAffectedTargets;
    switch(cur)
    {
        case TARGET_TOTEM_EARTH:
        case TARGET_TOTEM_WATER:
        case TARGET_TOTEM_AIR:
        case TARGET_TOTEM_FIRE:
        case TARGET_SELF:
        case TARGET_DYNAMIC_OBJECT:
        case TARGET_AREAEFFECT_CUSTOM:
        case TARGET_AREAEFFECT_CUSTOM_2:
        {
            TagUnitMap.push_back(m_caster);
            break;
        }
        case TARGET_RANDOM_ENEMY_CHAIN_IN_AREA:
        {
            m_targets.m_targetMask = 0;
            unMaxTargets = m_spellInfo->EffectChainTarget[i];
            float max_range = radius + unMaxTargets * CHAIN_SPELL_JUMP_RADIUS;

            CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
            Cell cell(p);
            cell.data.Part.reserved = ALL_DISTRICT;
            cell.SetNoCreate();

            std::list<Unit *> tempUnitMap;

            {
                MaNGOS::AnyAoETargetUnitInObjectRangeCheck u_check(m_caster, m_caster, max_range);
                MaNGOS::UnitListSearcher<MaNGOS::AnyAoETargetUnitInObjectRangeCheck> searcher(tempUnitMap, u_check);

                TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyAoETargetUnitInObjectRangeCheck>, WorldTypeMapContainer > world_unit_searcher(searcher);
                TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyAoETargetUnitInObjectRangeCheck>, GridTypeMapContainer >  grid_unit_searcher(searcher);

                CellLock<GridReadGuard> cell_lock(cell, p);
                cell_lock->Visit(cell_lock, world_unit_searcher, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                cell_lock->Visit(cell_lock, grid_unit_searcher, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
            }

            if(tempUnitMap.empty())
                break;

            tempUnitMap.sort(TargetDistanceOrder(m_caster));

            //Now to get us a random target that's in the initial range of the spell
            uint32 t = 0;
            std::list<Unit *>::iterator itr = tempUnitMap.begin();
            while(itr!= tempUnitMap.end() && (*itr)->GetDistanceSq(m_caster) < radius * radius)
                ++t, ++itr;

            if(!t)
                break;

            itr = tempUnitMap.begin();
            std::advance(itr, rand()%t);
            Unit *pUnitTarget = *itr;
            TagUnitMap.push_back(pUnitTarget);

            tempUnitMap.erase(itr);

            tempUnitMap.sort(TargetDistanceOrder(pUnitTarget));

            t = unMaxTargets - 1;
            Unit *prev = pUnitTarget;
            std::list<Unit*>::iterator next = tempUnitMap.begin();

            while(t && next != tempUnitMap.end() )
            {
                if(prev->GetDistanceSq(*next) > CHAIN_SPELL_JUMP_RADIUS * CHAIN_SPELL_JUMP_RADIUS)
                    break;

                if(!prev->IsWithinLOSInMap(*next))
                {
                    ++next;
                    continue;
                }

                prev = *next;
                TagUnitMap.push_back(prev);
                tempUnitMap.erase(next);
                tempUnitMap.sort(TargetDistanceOrder(prev));
                next = tempUnitMap.begin();

                --t;
            }
        }break;
        case TARGET_PET:
        {
            Pet* tmpUnit = m_caster->GetPet();
            if (!tmpUnit) break;
            TagUnitMap.push_back(tmpUnit);
            break;
        }
        case TARGET_CHAIN_DAMAGE:
        {
            if (m_spellInfo->EffectChainTarget[i] <= 1)
            {
                Unit* pUnitTarget = SelectMagnetTarget();
                if(pUnitTarget)
                    TagUnitMap.push_back(pUnitTarget);
            }
            else
            {
                Unit* pUnitTarget = m_targets.getUnitTarget();
                if(!pUnitTarget)
                    break;

                unMaxTargets = m_spellInfo->EffectChainTarget[i];

                float max_range;
                if(m_spellInfo->DmgClass==SPELL_DAMAGE_CLASS_MELEE)
                    max_range = radius;                     //
                else
                    //FIXME: This very like horrible hack and wrong for most spells
                    max_range = radius + unMaxTargets * CHAIN_SPELL_JUMP_RADIUS;

                CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                Cell cell(p);
                cell.data.Part.reserved = ALL_DISTRICT;
                cell.SetNoCreate();

                Unit* originalCaster = GetOriginalCaster();
                if(originalCaster)
                {
                    std::list<Unit *> tempUnitMap;

                    {
                        MaNGOS::AnyAoETargetUnitInObjectRangeCheck u_check(pUnitTarget, originalCaster, max_range);
                        MaNGOS::UnitListSearcher<MaNGOS::AnyAoETargetUnitInObjectRangeCheck> searcher(tempUnitMap, u_check);

                        TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyAoETargetUnitInObjectRangeCheck>, WorldTypeMapContainer > world_unit_searcher(searcher);
                        TypeContainerVisitor<MaNGOS::UnitListSearcher<MaNGOS::AnyAoETargetUnitInObjectRangeCheck>, GridTypeMapContainer >  grid_unit_searcher(searcher);

                        CellLock<GridReadGuard> cell_lock(cell, p);
                        cell_lock->Visit(cell_lock, world_unit_searcher, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                        cell_lock->Visit(cell_lock, grid_unit_searcher, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                    }

                    tempUnitMap.sort(TargetDistanceOrder(pUnitTarget));

                    if(tempUnitMap.empty())
                        break;

                    if(*tempUnitMap.begin() == pUnitTarget)
                        tempUnitMap.erase(tempUnitMap.begin());

                    TagUnitMap.push_back(pUnitTarget);
                    uint32 t = unMaxTargets - 1;
                    Unit *prev = pUnitTarget;
                    std::list<Unit*>::iterator next = tempUnitMap.begin();

                    while(t && next != tempUnitMap.end() )
                    {
                        if(prev->GetDistanceSq(*next) > CHAIN_SPELL_JUMP_RADIUS * CHAIN_SPELL_JUMP_RADIUS)
                            break;

                        if(!prev->IsWithinLOSInMap(*next))
                        {
                            ++next;
                            continue;
                        }

                        prev = *next;
                        TagUnitMap.push_back(prev);
                        tempUnitMap.erase(next);
                        tempUnitMap.sort(TargetDistanceOrder(prev));
                        next = tempUnitMap.begin();

                        --t;
                    }
                }
            }
        }break;
        case TARGET_ALL_ENEMY_IN_AREA:
        {
        }break;
        case TARGET_ALL_ENEMY_IN_AREA_INSTANT:
        {
            // targets the ground, not the units in the area
            if (m_spellInfo->Effect[i]!=SPELL_EFFECT_PERSISTENT_AREA_AURA)
            {
                CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                Cell cell(p);
                cell.data.Part.reserved = ALL_DISTRICT;
                cell.SetNoCreate();

                MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, TagUnitMap, radius, PUSH_DEST_CENTER,SPELL_TARGETS_AOE_DAMAGE);

                TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, WorldTypeMapContainer > world_object_notifier(notifier);
                TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, GridTypeMapContainer >  grid_object_notifier(notifier);

                CellLock<GridReadGuard> cell_lock(cell, p);
                cell_lock->Visit(cell_lock, world_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                cell_lock->Visit(cell_lock, grid_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));

                // exclude caster (this can be important if this not original caster)
                TagUnitMap.remove(m_caster);
            }
        }break;
        case TARGET_ALL_PARTY_AROUND_CASTER:
        case TARGET_ALL_PARTY_AROUND_CASTER_2:
        case TARGET_ALL_PARTY:
        {
            Unit* owner = m_caster->GetCharmerOrOwner();
            Player *pTarget = NULL;

            if(owner)
            {
                if(owner->GetTypeId() == TYPEID_PLAYER)
                    pTarget = (Player*)owner;
            }
            else if (m_caster->GetTypeId() == TYPEID_PLAYER)
                pTarget = (Player*)m_caster;

            Group *pGroup = pTarget ? pTarget->GetGroup() : NULL;

            if(pGroup)
            {
                uint8 subgroup = pTarget->GetSubGroup();

                for(GroupReference *itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
                {
                    Player* Target = itr->getSource();

                    // IsHostileTo check duel and controlled by enemy
                    if( Target && Target->GetSubGroup()==subgroup && !m_caster->IsHostileTo(Target) )
                    {
                        if( m_caster->IsWithinDistInMap(Target, radius) )
                            TagUnitMap.push_back(Target);

                        if(Pet* pet = Target->GetPet())
                            if( m_caster->IsWithinDistInMap(pet, radius) )
                                TagUnitMap.push_back(pet);
                    }
                }
            }
            else if (owner)
            {
                if(m_caster->IsWithinDistInMap(owner, radius))
                    TagUnitMap.push_back(owner);
            }
            else
                TagUnitMap.push_back(m_caster);
        }break;
        case TARGET_RANDOM_RAID_MEMBER:
        {
            Player *pTarget = NULL;

            if (m_caster->GetTypeId() == TYPEID_PLAYER)
                pTarget = (Player*)m_caster;

            Group *pGroup = pTarget ? pTarget->GetGroup() : NULL;

            if(pGroup)
            {
                std::vector<Player*> nearMembers;
                nearMembers.reserve(pGroup->GetMembersCount());

                for(GroupReference *itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
                {
                    Player* Target = itr->getSource();

                    // IsHostileTo check duel and controlled by enemy
                    if( Target && Target != pTarget && m_caster->IsWithinDistInMap(Target, radius) && 
                        !Target->HasStealthAura() && !Target->HasInvisibilityAura() 
                        && !m_caster->IsHostileTo(Target) )
                        nearMembers.push_back(Target);
                }

                if (!nearMembers.empty())
                {
                    uint32 randTarget = urand(0,nearMembers.size()-1);
                    TagUnitMap.push_back(nearMembers[randTarget]);
                }
            }
        }break;
        case TARGET_SINGLE_FRIEND:
        case TARGET_SINGLE_FRIEND_2:
        {
            if(m_targets.getUnitTarget())
                TagUnitMap.push_back(m_targets.getUnitTarget());
        }break;
        case TARGET_ALL_AROUND_CASTER:
        {
            CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
            Cell cell(p);
            cell.data.Part.reserved = ALL_DISTRICT;
            cell.SetNoCreate();

            MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, TagUnitMap, radius, PUSH_SELF_CENTER,SPELL_TARGETS_AOE_DAMAGE);

            TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, WorldTypeMapContainer > world_object_notifier(notifier);
            TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, GridTypeMapContainer >  grid_object_notifier(notifier);

            CellLock<GridReadGuard> cell_lock(cell, p);
            cell_lock->Visit(cell_lock, world_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
            cell_lock->Visit(cell_lock, grid_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
        }break;
        case TARGET_ALL_FRIENDLY_UNITS_AROUND_CASTER:
        {
            CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
            Cell cell(p);
            cell.data.Part.reserved = ALL_DISTRICT;
            cell.SetNoCreate();

            MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, TagUnitMap, radius, PUSH_SELF_CENTER,SPELL_TARGETS_FRIENDLY);

            TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, WorldTypeMapContainer > world_object_notifier(notifier);
            TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, GridTypeMapContainer >  grid_object_notifier(notifier);

            CellLock<GridReadGuard> cell_lock(cell, p);
            cell_lock->Visit(cell_lock, world_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
            cell_lock->Visit(cell_lock, grid_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
        }break;
        // TARGET_SINGLE_PARTY means that the spells can only be casted on a party member and not on the caster (some sceals, fire shield from imp, etc..) 
        case TARGET_SINGLE_PARTY:
        {
            Unit *target = m_targets.getUnitTarget();
            // Thoses spells apparently can't be casted on the caster.
            if( target && target != m_caster)
            {
                // Can only be casted on group's members or its pets
                Group  *pGroup = NULL;

                Unit* owner = m_caster->GetCharmerOrOwner();
                Unit *targetOwner = target->GetCharmerOrOwner();
                if(owner)
                {
                    if(owner->GetTypeId() == TYPEID_PLAYER)
                    {
                        if( target == owner )
                        {
                            TagUnitMap.push_back(target);
                            break;
                        }
                        pGroup = ((Player*)owner)->GetGroup();
                    }
                }
                else if (m_caster->GetTypeId() == TYPEID_PLAYER)
                {
                    if( targetOwner == m_caster && target->GetTypeId()==TYPEID_UNIT && ((Creature*)target)->isPet())
                    {
                        TagUnitMap.push_back(target);
                        break;
                    }
                    pGroup = ((Player*)m_caster)->GetGroup();
                }
 
                if(pGroup)
                {
                    // Our target can also be a player's pet who's grouped with us or our pet. But can't be controlled player
                    if(targetOwner)
                    {
                        if( targetOwner->GetTypeId() == TYPEID_PLAYER && 
                            target->GetTypeId()==TYPEID_UNIT && (((Creature*)target)->isPet()) &&
                            target->GetOwnerGUID()==targetOwner->GetGUID() &&
                            pGroup->IsMember(((Player*)targetOwner)->GetGUID()))
                        {
                            TagUnitMap.push_back(target);
                        }
                    }
                    // 1Our target can be a player who is on our group
                    else if (target->GetTypeId() == TYPEID_PLAYER && pGroup->IsMember(((Player*)target)->GetGUID()))
                    {
                        TagUnitMap.push_back(target);
                    }
                }
            }
        }break;
        case TARGET_GAMEOBJECT:
        {
            if(m_targets.getGOTarget())
                AddGOTarget(m_targets.getGOTarget(), i);
        }break;
        case TARGET_IN_FRONT_OF_CASTER:
        {
            CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
            Cell cell(p);
            cell.data.Part.reserved = ALL_DISTRICT;
            cell.SetNoCreate();

            MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, TagUnitMap, radius, PUSH_IN_FRONT,SPELL_TARGETS_AOE_DAMAGE);

            TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, WorldTypeMapContainer > world_object_notifier(notifier);
            TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, GridTypeMapContainer >  grid_object_notifier(notifier);

            CellLock<GridReadGuard> cell_lock(cell, p);
            cell_lock->Visit(cell_lock, world_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
            cell_lock->Visit(cell_lock, grid_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
        }break;
        case TARGET_DUELVSPLAYER:
        {
            Unit *target = m_targets.getUnitTarget();
            if(target)
            {
                if(m_caster->IsFriendlyTo(target))
                {
                    TagUnitMap.push_back(target);
                }
                else
                {
                    Unit* pUnitTarget = SelectMagnetTarget();
                    if(pUnitTarget)
                        TagUnitMap.push_back(pUnitTarget);
                }
            }
        }break;
        case TARGET_GAMEOBJECT_ITEM:
        {
            if(m_targets.getGOTargetGUID())
                AddGOTarget(m_targets.getGOTarget(), i);
            else if(m_targets.getItemTarget())
                AddItemTarget(m_targets.getItemTarget(), i);
            break;
        }
        case TARGET_MASTER:
        {
            Unit* owner = m_caster->GetCharmerOrOwner();
            if(owner)
                TagUnitMap.push_back(owner);
            break;
        }
        case TARGET_ALL_ENEMY_IN_AREA_CHANNELED:
        {
            // targets the ground, not the units in the area
            if (m_spellInfo->Effect[i]!=SPELL_EFFECT_PERSISTENT_AREA_AURA)
            {
                CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                Cell cell(p);
                cell.data.Part.reserved = ALL_DISTRICT;
                cell.SetNoCreate();

                MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, TagUnitMap, radius, PUSH_DEST_CENTER,SPELL_TARGETS_AOE_DAMAGE);

                TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, WorldTypeMapContainer > world_object_notifier(notifier);
                TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, GridTypeMapContainer >  grid_object_notifier(notifier);

                CellLock<GridReadGuard> cell_lock(cell, p);
                cell_lock->Visit(cell_lock, world_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                cell_lock->Visit(cell_lock, grid_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
            }
        }break;
        case TARGET_MINION:
        {
            if(m_spellInfo->Effect[i] != SPELL_EFFECT_DUEL)
                TagUnitMap.push_back(m_caster);
        }break;
        case TARGET_SINGLE_ENEMY:
        {
            Unit* pUnitTarget = SelectMagnetTarget();
            if(pUnitTarget)
                TagUnitMap.push_back(pUnitTarget);
        }break;
        case TARGET_AREAEFFECT_PARTY:
        {
            Unit* owner = m_caster->GetCharmerOrOwner();
            Player *pTarget = NULL;

            if(owner)
            {
                TagUnitMap.push_back(m_caster);
                if(owner->GetTypeId() == TYPEID_PLAYER)
                    pTarget = (Player*)owner;
            }
            else if (m_caster->GetTypeId() == TYPEID_PLAYER)
            {
                if(Unit* target = m_targets.getUnitTarget())
                {
                    if( target->GetTypeId() != TYPEID_PLAYER)
                    {
                        if(((Creature*)target)->isPet())
                        {
                            Unit *targetOwner = target->GetOwner();
                            if(targetOwner->GetTypeId() == TYPEID_PLAYER)
                                pTarget = (Player*)targetOwner;
                        }
                    }
                    else
                        pTarget = (Player*)target;
                }
            }

            Group* pGroup = pTarget ? pTarget->GetGroup() : NULL;

            if(pGroup)
            {
                uint8 subgroup = pTarget->GetSubGroup();

                for(GroupReference *itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
                {
                    Player* Target = itr->getSource();

                    // IsHostileTo check duel and controlled by enemy
                    if(Target && Target->GetSubGroup()==subgroup && !m_caster->IsHostileTo(Target))
                    {
                        if( pTarget->IsWithinDistInMap(Target, radius) )
                            TagUnitMap.push_back(Target);

                        if(Pet* pet = Target->GetPet())
                            if( pTarget->IsWithinDistInMap(pet, radius) )
                                TagUnitMap.push_back(pet);
                    }
                }
            }
            else if (owner)
            {
                if(m_caster->IsWithinDistInMap(owner, radius))
                    TagUnitMap.push_back(owner);
            }
            else if(pTarget)
            {
                TagUnitMap.push_back(pTarget);

                if(Pet* pet = pTarget->GetPet())
                    if( m_caster->IsWithinDistInMap(pet, radius) )
                        TagUnitMap.push_back(pet);
            }

        }break;
        case TARGET_SCRIPT:
        {
            if(m_targets.getUnitTarget())
                TagUnitMap.push_back(m_targets.getUnitTarget());
            if(m_targets.getItemTarget())
                AddItemTarget(m_targets.getItemTarget(), i);
        }break;
        case TARGET_SELF_FISHING:
        {
            TagUnitMap.push_back(m_caster);
        }break;
        case TARGET_CHAIN_HEAL:
        {
            Unit* pUnitTarget = m_targets.getUnitTarget();
            if(!pUnitTarget)
                break;

            if (m_spellInfo->EffectChainTarget[i] <= 1)
                TagUnitMap.push_back(pUnitTarget);
            else
            {
                unMaxTargets = m_spellInfo->EffectChainTarget[i];
                float max_range = radius + unMaxTargets * CHAIN_SPELL_JUMP_RADIUS;

                std::list<Unit *> tempUnitMap;

                {
                    CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                    Cell cell(p);
                    cell.data.Part.reserved = ALL_DISTRICT;
                    cell.SetNoCreate();

                    MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, tempUnitMap, max_range, PUSH_SELF_CENTER, SPELL_TARGETS_FRIENDLY);

                    TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, WorldTypeMapContainer > world_object_notifier(notifier);
                    TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, GridTypeMapContainer >  grid_object_notifier(notifier);

                    CellLock<GridReadGuard> cell_lock(cell, p);
                    cell_lock->Visit(cell_lock, world_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                    cell_lock->Visit(cell_lock, grid_object_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));

                }

                if(m_caster != pUnitTarget && std::find(tempUnitMap.begin(),tempUnitMap.end(),m_caster) == tempUnitMap.end() )
                    tempUnitMap.push_front(m_caster);

                tempUnitMap.sort(TargetDistanceOrder(pUnitTarget));

                if(tempUnitMap.empty())
                    break;

                if(*tempUnitMap.begin() == pUnitTarget)
                    tempUnitMap.erase(tempUnitMap.begin());

                TagUnitMap.push_back(pUnitTarget);
                uint32 t = unMaxTargets - 1;
                Unit *prev = pUnitTarget;
                std::list<Unit*>::iterator next = tempUnitMap.begin();

                while(t && next != tempUnitMap.end() )
                {
                    if(prev->GetDistanceSq(*next) > CHAIN_SPELL_JUMP_RADIUS * CHAIN_SPELL_JUMP_RADIUS)
                        break;

                    if(!prev->IsWithinLOSInMap(*next))
                    {
                        ++next;
                        continue;
                    }

                    if((*next)->GetHealth() == (*next)->GetMaxHealth())
                    {
                        next = tempUnitMap.erase(next);
                        continue;
                    }

                    prev = *next;
                    TagUnitMap.push_back(prev);
                    tempUnitMap.erase(next);
                    tempUnitMap.sort(TargetDistanceOrder(prev));
                    next = tempUnitMap.begin();

                    --t;
                }
            }
        }break;
        case TARGET_CURRENT_SELECTED_ENEMY:
        {
            Unit* currentTarget = m_targets.getUnitTarget();

            if(currentTarget)
            {
                TagUnitMap.push_back(currentTarget);

                if(m_spellInfo->EffectImplicitTargetB[i]==TARGET_ALL_ENEMY_IN_AREA_INSTANT)
                {
                    CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                    Cell cell(p);
                    cell.data.Part.reserved = ALL_DISTRICT;
                    cell.SetNoCreate();
                    MaNGOS::SpellNotifierCreatureAndPlayer notifier(*this, TagUnitMap, radius,PUSH_TARGET_CENTER, SPELL_TARGETS_AOE_DAMAGE);
                    TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, WorldTypeMapContainer > world_notifier(notifier);
                    TypeContainerVisitor<MaNGOS::SpellNotifierCreatureAndPlayer, GridTypeMapContainer > grid_notifier(notifier);
                    CellLock<GridReadGuard> cell_lock(cell, p);
                    cell_lock->Visit(cell_lock, world_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                    cell_lock->Visit(cell_lock, grid_notifier, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                }
            }
        }break;
        case TARGET_AREAEFFECT_PARTY_AND_CLASS:
        {
            Player* targetPlayer = m_targets.getUnitTarget() && m_targets.getUnitTarget()->GetTypeId() == TYPEID_PLAYER
                ? (Player*)m_targets.getUnitTarget() : NULL;

            Group* pGroup = targetPlayer ? targetPlayer->GetGroup() : NULL;
            if(pGroup)
            {
                for(GroupReference *itr = pGroup->GetFirstMember(); itr != NULL; itr = itr->next())
                {
                    Player* Target = itr->getSource();

                    // IsHostileTo check duel and controlled by enemy
                    if( Target && targetPlayer->IsWithinDistInMap(Target, radius) &&
                        targetPlayer->getClass() == Target->getClass() &&
                        !m_caster->IsHostileTo(Target) )
                    {
                        TagUnitMap.push_back(Target);
                    }
                }
            }
            else if(m_targets.getUnitTarget())
                TagUnitMap.push_back(m_targets.getUnitTarget());
            break;
        }
        default:
            break;
    }

    if (unMaxTargets && TagUnitMap.size() > unMaxTargets)
    {
        // make sure one unit is always removed per iteration
        uint32 removed_utarget = 0;
        for (std::list<Unit*>::iterator itr = TagUnitMap.begin(), next; itr != TagUnitMap.end(); itr = next)
        {
            next = itr;
            ++next;
            if (!*itr) continue;
            if ((*itr) == m_targets.getUnitTarget())
            {
                TagUnitMap.erase(itr);
                removed_utarget = 1;
                //        break;
            }
        }
        // remove random units from the map
        while (TagUnitMap.size() > unMaxTargets - removed_utarget)
        {
            uint32 poz = urand(0, TagUnitMap.size()-1);
            for (std::list<Unit*>::iterator itr = TagUnitMap.begin(); itr != TagUnitMap.end(); ++itr, --poz)
            {
                if (!*itr) continue;
                if (!poz)
                {
                    TagUnitMap.erase(itr);
                    break;
                }
            }
        }
        // the player's target will always be added to the map
        if (removed_utarget && m_targets.getUnitTarget())
            TagUnitMap.push_back(m_targets.getUnitTarget());
    }
}

void Spell::prepare(SpellCastTargets * targets, Aura* triggeredByAura)
{
    m_targets = *targets;

    m_spellState = SPELL_STATE_PREPARING;

    m_castPositionX = m_caster->GetPositionX();
    m_castPositionY = m_caster->GetPositionY();
    m_castPositionZ = m_caster->GetPositionZ();
    m_castOrientation = m_caster->GetOrientation();

    if(triggeredByAura)
        m_triggeredByAuraSpell  = triggeredByAura->GetSpellProto();

    // create and add update event for this spell
    SpellEvent* Event = new SpellEvent(this);
    m_caster->m_Events.AddEvent(Event, m_caster->m_Events.CalculateTime(1));

    //Prevent casting at cast another spell (ServerSide check)
    if(m_caster->IsNonMeleeSpellCasted(false, true) && m_cast_count)
    {
        SendCastResult(SPELL_FAILED_SPELL_IN_PROGRESS);
        finish(false);
        return;
    }

    uint8 result = CanCast(true);
    if(result != 0)
    {
        if(triggeredByAura)
        {
            SendChannelUpdate(0);
            triggeredByAura->SetAuraDuration(0);
        }
        SendCastResult(result);
        finish(false);
        return;
    }

    // calculate cast time (calculated after first CanCast check to prevent charge counting for first CanCast fail)
    m_casttime = GetSpellCastTime(sCastTimesStore.LookupEntry(m_spellInfo->CastingTimeIndex));

    if(Player* modOwner = m_caster->GetSpellModOwner())
        modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_CASTING_TIME, m_casttime,this);

    m_casttime = int32(m_casttime*m_caster->GetFloatValue(UNIT_MOD_CAST_SPEED));

    // set timer base at cast time
    ReSetTimer();

    // stealth must be removed at cast starting (at show channel bar)
    // skip triggered spell (item equip spell casting and other not explicit character casts/item uses)
    if ( !m_IsTriggeredSpell && !CanBeUsedWhileStealthed(m_spellInfo) )
    {
        m_caster->RemoveSpellsCausingAura(SPELL_AURA_MOD_STEALTH);
        m_caster->RemoveSpellsCausingAura(SPELL_AURA_FEIGN_DEATH);
    }

    // do first cast of autorepeat spell with recovery time delay (like after any autocast)
    if(IsAutoRepeat())
        m_spellState = SPELL_STATE_FINISHED;

    if(m_IsTriggeredSpell)
        cast(true);
    else
    {
        m_caster->SetCurrentCastedSpell( this );
        m_selfContainer = &(m_caster->m_currentSpells[GetCurrentContainer()]);
        SendSpellStart();
    }
}

void Spell::cancel()
{
    if(m_spellState == SPELL_STATE_FINISHED)
        return;

    m_autoRepeat = false;
    switch (m_spellState)
    {
        case SPELL_STATE_PREPARING:
        case SPELL_STATE_DELAYED:
        {
            SendInterrupted(0);
            SendCastResult(SPELL_FAILED_INTERRUPTED);
        } break;

        case SPELL_STATE_CASTING:
        {
            for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
            {
                if( ihit->missCondition == SPELL_MISS_NONE )
                {
                    Unit* unit = m_caster->GetGUID()==(*ihit).targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
                    if( unit && unit->isAlive() )
                        unit->RemoveAurasDueToSpell(m_spellInfo->Id);
                }
            }

            m_caster->RemoveAurasDueToSpell(m_spellInfo->Id);
            SendChannelUpdate(0);
            SendInterrupted(0);
            SendCastResult(SPELL_FAILED_INTERRUPTED);
        } break;

        default:
        {
        } break;
    }

    finish(false);
    m_caster->RemoveDynObject(m_spellInfo->Id);
    m_caster->RemoveGameObject(m_spellInfo->Id,true);
}

void Spell::cast(bool skipCheck)
{
    uint32 mana = 0;
    uint8 castResult = 0;

    // update pointers base at GUIDs to prevent access to non-existed already object
    UpdatePointers();

    // cancel at lost main target unit
    if(!m_targets.getUnitTarget() && m_targets.getUnitTargetGUID() && m_targets.getUnitTargetGUID() != m_caster->GetGUID())
    {
        cancel();
        return;
    }

    if(m_caster->GetTypeId() != TYPEID_PLAYER && m_targets.getUnitTarget() && m_targets.getUnitTarget() != m_caster)
        m_caster->SetInFront(m_targets.getUnitTarget());

    castResult = CheckMana( &mana);
    if(castResult != 0)
    {
        SendCastResult(castResult);
        finish(false);
        return;
    }

    // triggered cast called from Spell::prepare where it was already checked
    if(!skipCheck)
    {
        castResult = CanCast(false);
        if(castResult != 0)
        {
            SendCastResult(castResult);
            finish(false);
            return;
        }
    }

    // Conflagrate - consumes immolate
    if ((m_spellInfo->TargetAuraState == AURA_STATE_IMMOLATE) && m_targets.getUnitTarget())
    {
        // for caster applied auras only
        Unit::AuraList const &mPeriodic = m_targets.getUnitTarget()->GetAurasByType(SPELL_AURA_PERIODIC_DAMAGE);
        for(Unit::AuraList::const_iterator i = mPeriodic.begin(); i != mPeriodic.end(); ++i)
        {
            if( (*i)->GetSpellProto()->SpellFamilyName == SPELLFAMILY_WARLOCK && ((*i)->GetSpellProto()->SpellFamilyFlags & 4) &&
                (*i)->GetCasterGUID()==m_caster->GetGUID() )
            {
                m_targets.getUnitTarget()->RemoveAura((*i)->GetId(), (*i)->GetEffIndex());
                break;
            }
        }
    }

    // traded items have trade slot instead of guid in m_itemTargetGUID
    // set to real guid to be sent later to the client
    m_targets.updateTradeSlotItem();

    // CAST SPELL
    SendSpellCooldown();

    TakePower(mana);
    TakeReagents();                                         // we must remove reagents before HandleEffects to allow place crafted item in same slot
    FillTargetMap();

    if(m_spellState == SPELL_STATE_FINISHED)                // stop cast if spell marked as finish somewhere in Take*/FillTargetMap
        return;

    SendCastResult(castResult);
    SendSpellGo();                                          // we must send smsg_spell_go packet before m_castItem delete in TakeCastItem()...

    // Pass cast spell event to handler (not send triggered by aura spells)
    if (m_spellInfo->DmgClass != SPELL_DAMAGE_CLASS_MELEE && m_spellInfo->DmgClass != SPELL_DAMAGE_CLASS_RANGED && !m_triggeredByAuraSpell)
    {
        m_caster->ProcDamageAndSpell(m_targets.getUnitTarget(), PROC_FLAG_CAST_SPELL, PROC_FLAG_NONE, 0, m_spellInfo, m_IsTriggeredSpell);

        // update pointers base at GUIDs to prevent access to non-existed already object
        UpdatePointers();                                   // pointers can be invalidate at triggered spell casting
    }

    // Okay, everything is prepared. Now we need to distinguish between immediate and evented delayed spells
    if (m_spellInfo->speed > 0.0f)
    {

        // Remove used for cast item if need (it can be already NULL after TakeReagents call
        // in case delayed spell remove item at cast delay start
        TakeCastItem();

        // Okay, maps created, now prepare flags
        m_immediateHandled = false;
        m_spellState = SPELL_STATE_DELAYED;
        SetDelayStart(0);
    }
    else
    {
        // Immediate spell, no big deal
        handle_immediate();
    }
}

void Spell::handle_immediate()
{
    // start channeling if applicable
    if(IsChanneledSpell(m_spellInfo))
    {
        m_spellState = SPELL_STATE_CASTING;
        SendChannelStart(GetSpellDuration(m_spellInfo));
    }

    // process immediate effects (items, ground, etc.) also initialize some variables
    _handle_immediate_phase();

    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
        DoAllEffectOnTarget(&(*ihit));

    for(std::list<GOTargetInfo>::iterator ihit= m_UniqueGOTargetInfo.begin();ihit != m_UniqueGOTargetInfo.end();++ihit)
        DoAllEffectOnTarget(&(*ihit));

    // spell is finished, perform some last features of the spell here
    _handle_finish_phase();

    // Remove used for cast item if need (it can be already NULL after TakeReagents call
    TakeCastItem();

    if(m_spellState != SPELL_STATE_CASTING)
        finish(true);                                       // successfully finish spell cast (not last in case autorepeat or channel spell)
}

uint64 Spell::handle_delayed(uint64 t_offset)
{
    uint64 next_time = 0;

    if (!m_immediateHandled)
    {
        _handle_immediate_phase();
        m_immediateHandled = true;
    }

    // now recheck units targeting correctness (need before any effects apply to prevent adding immunity at first effect not allow apply second spell effect and similar cases)
    std::list<TargetInfo>::iterator inext;
    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin(); ihit != m_UniqueTargetInfo.end();ihit = inext)
    {
        inext = ihit;
        ++inext;
        if( ihit->timeDelay <= t_offset )
        {
            DoAllEffectOnTarget(&(*ihit));
            m_UniqueTargetInfo.erase(ihit);
        }
        else
        {
            if( next_time == 0 || ihit->timeDelay < next_time )
                next_time = ihit->timeDelay;
        }
    }

    // now recheck gameobject targeting correctness
    std::list<GOTargetInfo>::iterator ignext;
    for(std::list<GOTargetInfo>::iterator ighit= m_UniqueGOTargetInfo.begin(); ighit != m_UniqueGOTargetInfo.end();ighit = ignext)
    {
        ignext = ighit;
        ++ignext;
        if( ighit->timeDelay <= t_offset )
        {
            DoAllEffectOnTarget(&(*ighit));
            m_UniqueGOTargetInfo.erase(ighit);
        }
        else
        {
            if( next_time == 0 || ighit->timeDelay < next_time )
                next_time = ighit->timeDelay;
        }
    }

    if (m_UniqueTargetInfo.empty() && m_UniqueGOTargetInfo.empty())
    {
        // spell is finished, perform some last features of the spell here
        _handle_finish_phase();

        finish(true);                                       // successfully finish spell cast

        // return zero, spell is finished now
        return 0;
    }
    else
    {
        // spell is unfinished, return next execution time
        return next_time;
    }
}

void Spell::_handle_immediate_phase()
{
    // handle some immediate features of the spell here
    HandleThreatSpells(m_spellInfo->Id);

    m_needSpellLog = IsNeedSendToClient();
    for(uint32 j = 0;j<3;j++)
    {
        if(m_spellInfo->Effect[j]==0)
            continue;

        // apply Send Event effect to ground in case empty target lists
        if( m_spellInfo->Effect[j] == SPELL_EFFECT_SEND_EVENT && !HaveTargetsForEffect(j) )
        {
            HandleEffects(NULL,NULL,NULL, j);
            continue;
        }

        // Dont do spell log, if is school damage spell
        if(m_spellInfo->Effect[j] == SPELL_EFFECT_SCHOOL_DAMAGE || m_spellInfo->Effect[j] == 0)
            m_needSpellLog = false;

        // initialize multipliers
        m_damageMultipliers[j] = 1.0f;
        m_applyMultiplier[j] =
            (m_spellInfo->EffectImplicitTargetA[j] == TARGET_CHAIN_DAMAGE || m_spellInfo->EffectImplicitTargetA[j] == TARGET_CHAIN_HEAL) &&
            (m_spellInfo->EffectChainTarget[j] > 1);
    }

    // process items
    for(std::list<ItemTargetInfo>::iterator ihit= m_UniqueItemInfo.begin();ihit != m_UniqueItemInfo.end();++ihit)
        DoAllEffectOnTarget(&(*ihit));

    // process ground
    for(uint32 j = 0;j<3;j++)
    {
        // persistent area auras target only the ground
        if(m_spellInfo->Effect[j] == SPELL_EFFECT_PERSISTENT_AREA_AURA)
            HandleEffects(NULL,NULL,NULL, j);
    }
}

void Spell::_handle_finish_phase()
{
    // spell log
    if(m_needSpellLog)
        SendLogExecute();
}

void Spell::SendSpellCooldown()
{
    if(m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    Player* _player = (Player*)m_caster;

    // init cooldown values
    uint32 cat   = 0;
    int32 rec    = -1;
    int32 catrec = -1;

    // some special item spells without correct cooldown in SpellInfo
    // cooldown information stored in item prototype
    // This used in same way in WorldSession::HandleItemQuerySingleOpcode data sending to client.

    if(m_CastItem)
    {
        ItemPrototype const* proto = m_CastItem->GetProto();
        if(proto)
        {
            for(int idx = 0; idx < 5; ++idx)
            {
                if(proto->Spells[idx].SpellId == m_spellInfo->Id)
                {
                    cat    = proto->Spells[idx].SpellCategory;
                    rec    = proto->Spells[idx].SpellCooldown;
                    catrec = proto->Spells[idx].SpellCategoryCooldown;
                    break;
                }
            }
        }
    }

    // if no cooldown found above then base at DBC data
    if(rec < 0 && catrec < 0)
    {
        cat = m_spellInfo->Category;
        rec = m_spellInfo->RecoveryTime;
        catrec = m_spellInfo->CategoryRecoveryTime;
    }

    // shoot spells used equipped item cooldown values already assigned in GetAttackTime(RANGED_ATTACK)
    // prevent 0 cooldowns set by another way
    if (rec <= 0 && catrec <= 0 && (cat == 76 || cat == 351))
        rec = _player->GetAttackTime(RANGED_ATTACK);

    // Now we have cooldown data (if found any), time to apply mods
    if(rec > 0)
        _player->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COOLDOWN, rec, this);

    if(catrec > 0)
        _player->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COOLDOWN, catrec, this);

    // replace negative cooldowns by 0
    if (rec < 0) rec = 0;
    if (catrec < 0) catrec = 0;

    // no cooldown after applying spell mods
    if( rec == 0 && catrec == 0)
        return;

    time_t curTime = time(NULL);

    time_t recTime    = curTime+rec/1000;                   // in secs
    time_t catrecTime = curTime+catrec/1000;                // in secs

    // self spell cooldown
    if (rec > 0)
    {
        if(m_CastItem)
            _player->AddSpellCooldown(m_spellInfo->Id, m_CastItem->GetEntry(), recTime);
        else
            _player->AddSpellCooldown(m_spellInfo->Id, 0, recTime);
    }
    else
    {
        if(m_CastItem)
            _player->AddSpellCooldown(m_spellInfo->Id, m_CastItem->GetEntry(), catrecTime);
        else
            _player->AddSpellCooldown(m_spellInfo->Id, 0, catrecTime);
    }

    if (catrec)
    {
        SpellCategoryStore::const_iterator i_scstore = sSpellCategoryStore.find(cat);
        if(i_scstore != sSpellCategoryStore.end())
        {
            for(SpellCategorySet::const_iterator i_scset = i_scstore->second.begin(); i_scset != i_scstore->second.end(); ++i_scset)
            {
                if(*i_scset == m_spellInfo->Id)             // skip casted spell
                    continue;

                if(m_CastItem)
                    _player->AddSpellCooldown(*i_scset, m_CastItem->GetEntry(), catrecTime);
                else
                    _player->AddSpellCooldown(*i_scset, 0, catrecTime);
            }
        }
    }
}

void Spell::update(uint32 difftime)
{
    // update pointers based at it's GUIDs
    UpdatePointers();

    if(m_targets.getUnitTargetGUID())
    {
        if(!m_targets.getUnitTarget() || !m_targets.getUnitTarget()->isAlive())
        {
            if(m_autoRepeat)
            {
                m_autoRepeat = false;
                m_spellState = SPELL_STATE_FINISHED;
                return;
            }

            if(!m_targets.getUnitTarget())
            {
                cancel();
                return;
            }
        }
    }

    // check if the player caster has moved before the spell finished
    if ((m_caster->GetTypeId() == TYPEID_PLAYER && m_timer != 0) &&
        (m_castPositionX != m_caster->GetPositionX() || m_castPositionY != m_caster->GetPositionY() || m_castPositionZ != m_caster->GetPositionZ()) &&
        (m_spellInfo->Effect[0] != SPELL_EFFECT_STUCK || !m_caster->HasUnitMovementFlag(MOVEMENTFLAG_FALLING)))
    {
        // always cancel for channeled spells
        if( m_spellState == SPELL_STATE_CASTING )
            cancel();
        // don't cancel for melee, autorepeat and instant spells
        else if(!m_meleeSpell && !m_autoRepeat && (m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_MOVEMENT))
            cancel();
    }

    switch(m_spellState)
    {
        case SPELL_STATE_PREPARING:
        {
            if(m_timer)
            {
                if(difftime >= m_timer)
                    m_timer = 0;
                else
                    m_timer -= difftime;
            }

            if(m_timer == 0 && !m_meleeSpell)
                cast();
        } break;
        case SPELL_STATE_CASTING:
        {
            if(m_timer > 0)
            {
                if( m_caster->GetTypeId() == TYPEID_PLAYER )
                {
                    // check if player has jumped before the channeling finished
                    if(m_caster->HasUnitMovementFlag(MOVEMENTFLAG_JUMPING))
                        cancel();

                    // check for incapacitating player states
                    if( m_caster->hasUnitState(UNIT_STAT_STUNDED | UNIT_STAT_CONFUSED))
                        cancel();

                    // check if player has turned if flag is set
                    if( m_spellInfo->ChannelInterruptFlags & CHANNEL_FLAG_TURNING && m_castOrientation != m_caster->GetOrientation() )
                        cancel();
                }

                // check if there are alive targets left
                if (!IsAliveUnitPresentInTargetList())
                {
                    SendChannelUpdate(0);
                    finish();
                }

                if(difftime >= m_timer)
                    m_timer = 0;
                else
                    m_timer -= difftime;
            }

            if(m_timer == 0)
            {
                SendChannelUpdate(0);

                // channeled spell processed independently for quest targeting
                // cast at creature (or GO) quest objectives update at succesful cast channel finished
                // ignore autorepeat/melee casts for speed (not exist quest for spells (hm... )
                if( m_caster->GetTypeId() == TYPEID_PLAYER && !IsAutoRepeat() && !IsMeleeSpell() )
                {
                    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
                    {
                        TargetInfo* target = &*ihit;
                        if(!IS_CREATURE_GUID(target->targetGUID))
                            continue;

                        Unit* unit = m_caster->GetGUID()==target->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster,target->targetGUID);
                        if (unit==NULL)
                            continue;

                        ((Player*)m_caster)->CastedCreatureOrGO(unit->GetEntry(),unit->GetGUID(),m_spellInfo->Id);
                    }

                    for(std::list<GOTargetInfo>::iterator ihit= m_UniqueGOTargetInfo.begin();ihit != m_UniqueGOTargetInfo.end();++ihit)
                    {
                        GOTargetInfo* target = &*ihit;

                        GameObject* go = ObjectAccessor::GetGameObject(*m_caster, target->targetGUID);
                        if(!go)
                            continue;

                        ((Player*)m_caster)->CastedCreatureOrGO(go->GetEntry(),go->GetGUID(),m_spellInfo->Id);
                    }
                }

                finish();
            }
        } break;
        default:
        {
        }break;
    }
}

void Spell::finish(bool ok)
{
    if(!m_caster) return;

    if(m_spellState == SPELL_STATE_FINISHED)
        return;

    m_spellState = SPELL_STATE_FINISHED;

    //remove spell mods
    if (m_caster->GetTypeId() == TYPEID_PLAYER)
        ((Player*)m_caster)->RemoveSpellMods(this);

    // other code related only to successfully finished spells
    if(!ok)
        return;

    // call triggered spell only at successful cast
    if(!m_TriggerSpells.empty())
        TriggerSpell();

    //handle SPELL_AURA_ADD_TARGET_TRIGGER auras
    Unit::AuraList const& targetTriggers = m_caster->GetAurasByType(SPELL_AURA_ADD_TARGET_TRIGGER);
    for(Unit::AuraList::const_iterator i = targetTriggers.begin(); i != targetTriggers.end(); ++i)
    {
        SpellEntry const *auraSpellInfo = (*i)->GetSpellProto();
        uint32 auraSpellIdx = (*i)->GetEffIndex();
        if (IsAffectedBy(auraSpellInfo, auraSpellIdx))
        {
            for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
                if( ihit->effectMask & (1<<auraSpellIdx) )
            {
                // check m_caster->GetGUID() let load auras at login and speedup most often case
                Unit *unit = m_caster->GetGUID()== ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
                if (unit && unit->isAlive())
                {
                    // Calculate chance at that moment (can be depend for example from combo points)
                    int32 chance = m_caster->CalculateSpellDamage(auraSpellInfo, auraSpellIdx, (*i)->GetBasePoints(),unit);

                    if(roll_chance_i(chance))
                        m_caster->CastSpell(unit, auraSpellInfo->EffectTriggerSpell[auraSpellIdx], true, NULL, (*i));
                }
            }
        }
    }

    if (IsMeleeAttackResetSpell())
    {
        m_caster->resetAttackTimer(BASE_ATTACK);
        if(m_caster->haveOffhandWeapon())
            m_caster->resetAttackTimer(OFF_ATTACK);
    }

    // Clear combo at finish state
    if(m_caster->GetTypeId() == TYPEID_PLAYER && NeedsComboPoints(m_spellInfo))
        ((Player*)m_caster)->ClearComboPoints();
}

void Spell::SendCastResult(uint8 result)
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    if(((Player*)m_caster)->GetSession()->PlayerLoading())  // don't send cast results at loading time
        return;

    if(result != 0)
    {
        WorldPacket data(SMSG_CAST_RESULT, (4+1+1));
        data << uint32(m_spellInfo->Id);
        data << uint8(result);                              // problem
        data << uint8(m_cast_count);                        // single cast or multi 2.3 (0/1)
        switch (result)
        {
            case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
                data << uint32(m_spellInfo->RequiresSpellFocus);
                break;
            case SPELL_FAILED_REQUIRES_AREA:
                // hardcode areas limitation case
                if( m_spellInfo->Id==41618 || m_spellInfo->Id==41620 )
                    data << uint32(3842);
                else if( m_spellInfo->Id==41617 || m_spellInfo->Id==41619 )
                    data << uint32(3905);
                // normal case
                else
                    data << uint32(m_spellInfo->AreaId);
                break;
            case SPELL_FAILED_TOTEMS:
                if(m_spellInfo->Totem[0])
                    data << uint32(m_spellInfo->Totem[0]);
                if(m_spellInfo->Totem[1])
                    data << uint32(m_spellInfo->Totem[1]);
                break;
            case SPELL_FAILED_TOTEM_CATEGORY:
                if(m_spellInfo->TotemCategory[0])
                    data << uint32(m_spellInfo->TotemCategory[0]);
                if(m_spellInfo->TotemCategory[1])
                    data << uint32(m_spellInfo->TotemCategory[1]);
                break;
            case SPELL_FAILED_EQUIPPED_ITEM_CLASS:
                data << uint32(m_spellInfo->EquippedItemClass);
                data << uint32(m_spellInfo->EquippedItemSubClassMask);
                data << uint32(m_spellInfo->EquippedItemInventoryTypeMask);
                break;
        }
        ((Player*)m_caster)->GetSession()->SendPacket(&data);
    }
    else
    {
        WorldPacket data(SMSG_CAST_SUCCESS, (8+4));
        data.append(m_caster->GetPackGUID());
        data << uint32(m_spellInfo->Id);
        ((Player*)m_caster)->GetSession()->SendPacket(&data);
    }
}

void Spell::SendSpellStart()
{
    if(!IsNeedSendToClient())
        return;

    sLog.outDebug("Sending SMSG_SPELL_START id=%u",m_spellInfo->Id);

    uint16 castFlags = CAST_FLAG_UNKNOWN1;
    if(m_rangedShoot)
        castFlags |= CAST_FLAG_AMMO;

    Unit * target;
    if(!m_targets.getUnitTarget())
        target = m_caster;
    else
        target = m_targets.getUnitTarget();

    WorldPacket data(SMSG_SPELL_START, (8+8+4+4+2));
    if(m_CastItem)
        data.append(m_CastItem->GetPackGUID());
    else
        data.append(m_caster->GetPackGUID());

    data.append(m_caster->GetPackGUID());
    data << uint32(m_spellInfo->Id);
    data << uint8(m_cast_count);                            // single cast or multi 2.3 (0/1)
    data << uint16(castFlags);
    data << uint32(m_timer);

    m_targets.write(&data);

    if( castFlags & CAST_FLAG_AMMO )
        WriteAmmoToPacket(&data);

    m_caster->SendMessageToSet(&data, true);
}

void Spell::SendSpellGo()
{
    // not send invisible spell casting
    if(!IsNeedSendToClient())
        return;

    sLog.outDebug("Sending SMSG_SPELL_GO id=%u",m_spellInfo->Id);

    Unit * target;
    if(!m_targets.getUnitTarget())
        target = m_caster;
    else
        target = m_targets.getUnitTarget();

    uint16 castFlags = CAST_FLAG_UNKNOWN3;
    if(m_rangedShoot)
        castFlags |= CAST_FLAG_AMMO;

    WorldPacket data(SMSG_SPELL_GO, 50);                    // guess size
    if(m_CastItem)
        data.append(m_CastItem->GetPackGUID());
    else
        data.append(m_caster->GetPackGUID());

    data.append(m_caster->GetPackGUID());
    data << uint32(m_spellInfo->Id);

    data << uint16(castFlags);
    data << uint32(getMSTime());                            // timestamp

    WriteSpellGoTargets(&data);

    m_targets.write(&data);

    if( castFlags & CAST_FLAG_AMMO )
        WriteAmmoToPacket(&data);

    m_caster->SendMessageToSet(&data, true);
}

void Spell::WriteAmmoToPacket( WorldPacket * data )
{
    uint32 ammoInventoryType = 0;
    uint32 ammoDisplayID = 0;

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        Item *pItem = ((Player*)m_caster)->GetItemByPos( INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED );
        if(pItem)
        {
            ammoInventoryType = pItem->GetProto()->InventoryType;
            if( ammoInventoryType == INVTYPE_THROWN )
                ammoDisplayID = pItem->GetProto()->DisplayInfoID;
            else
            {
                uint32 ammoID = ((Player*)m_caster)->GetUInt32Value(PLAYER_AMMO_ID);
                if(ammoID)
                {
                    ItemPrototype const *pProto = objmgr.GetItemPrototype( ammoID );
                    if(pProto)
                    {
                        ammoDisplayID = pProto->DisplayInfoID;
                        ammoInventoryType = pProto->InventoryType;
                    }
                }
            }
        }
    }
    // TODO: implement selection ammo data based at ranged weapon stored in equipmodel/equipinfo/equipslot fields

    *data << uint32(ammoDisplayID);
    *data << uint32(ammoInventoryType);
}

void Spell::WriteSpellGoTargets( WorldPacket * data )
{
    *data << (uint8)m_countOfHit;
    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
        if ((*ihit).missCondition == SPELL_MISS_NONE)       // Add only hits
            *data << uint64(ihit->targetGUID);

    for(std::list<GOTargetInfo>::iterator ighit= m_UniqueGOTargetInfo.begin();ighit != m_UniqueGOTargetInfo.end();++ighit)
        *data << uint64(ighit->targetGUID);                 // Always hits

    *data << (uint8)m_countOfMiss;
    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
    {
        if( ihit->missCondition != SPELL_MISS_NONE )        // Add only miss
        {
            *data << uint64(ihit->targetGUID);
            *data << uint8(ihit->missCondition);
            if( ihit->missCondition == SPELL_MISS_REFLECT )
                *data << uint8(ihit->reflectResult);
        }
    }
}

void Spell::SendLogExecute()
{
    Unit *target = m_targets.getUnitTarget() ? m_targets.getUnitTarget() : m_caster;

    WorldPacket data(SMSG_SPELLLOGEXECUTE, (8+4+4+4+4+8));

    if(m_caster->GetTypeId() == TYPEID_PLAYER)
        data.append(m_caster->GetPackGUID());
    else
        data.append(target->GetPackGUID());

    data << uint32(m_spellInfo->Id);
    uint32 count1 = 1;
    data << uint32(count1);                                 // count1 (effect count?)
    for(uint32 i = 0; i < count1; ++i)
    {
        data << uint32(m_spellInfo->Effect[0]);             // spell effect?
        uint32 count2 = 1;
        data << uint32(count2);                             // count2 (target count?)
        for(uint32 j = 0; j < count2; ++j)
        {
            switch(m_spellInfo->Effect[0])
            {
                case SPELL_EFFECT_MANA_DRAIN:
                    if(Unit *unit = m_targets.getUnitTarget())
                        data.append(unit->GetPackGUID());
                    else
                        data << uint8(0);
                    data << uint32(0);
                    data << uint32(0);
                    data << float(0);
                    break;
                case SPELL_EFFECT_ADD_EXTRA_ATTACKS:
                    if(Unit *unit = m_targets.getUnitTarget())
                        data.append(unit->GetPackGUID());
                    else
                        data << uint8(0);
                    data << uint32(0);                      // count?
                    break;
                case SPELL_EFFECT_INTERRUPT_CAST:
                    if(Unit *unit = m_targets.getUnitTarget())
                        data.append(unit->GetPackGUID());
                    else
                        data << uint8(0);
                    data << uint32(0);                      // spellid
                    break;
                case SPELL_EFFECT_DURABILITY_DAMAGE:
                    if(Unit *unit = m_targets.getUnitTarget())
                        data.append(unit->GetPackGUID());
                    else
                        data << uint8(0);
                    data << uint32(0);
                    data << uint32(0);
                    break;
                case SPELL_EFFECT_OPEN_LOCK:
                case SPELL_EFFECT_OPEN_LOCK_ITEM:
                    if(Item *item = m_targets.getItemTarget())
                        data.append(item->GetPackGUID());
                    else
                        data << uint8(0);
                    break;
                case SPELL_EFFECT_CREATE_ITEM:
                    data << uint32(m_spellInfo->EffectItemType[0]);
                    break;
                case SPELL_EFFECT_SUMMON:
                case SPELL_EFFECT_SUMMON_WILD:
                case SPELL_EFFECT_SUMMON_GUARDIAN:
                case SPELL_EFFECT_TRANS_DOOR:
                case SPELL_EFFECT_SUMMON_PET:
                case SPELL_EFFECT_SUMMON_POSSESSED:
                case SPELL_EFFECT_SUMMON_TOTEM:
                case SPELL_EFFECT_SUMMON_OBJECT_WILD:
                case SPELL_EFFECT_CREATE_HOUSE:
                case SPELL_EFFECT_DUEL:
                case SPELL_EFFECT_SUMMON_TOTEM_SLOT1:
                case SPELL_EFFECT_SUMMON_TOTEM_SLOT2:
                case SPELL_EFFECT_SUMMON_TOTEM_SLOT3:
                case SPELL_EFFECT_SUMMON_TOTEM_SLOT4:
                case SPELL_EFFECT_SUMMON_PHANTASM:
                case SPELL_EFFECT_SUMMON_CRITTER:
                case SPELL_EFFECT_SUMMON_OBJECT_SLOT1:
                case SPELL_EFFECT_SUMMON_OBJECT_SLOT2:
                case SPELL_EFFECT_SUMMON_OBJECT_SLOT3:
                case SPELL_EFFECT_SUMMON_OBJECT_SLOT4:
                case SPELL_EFFECT_SUMMON_DEMON:
                case SPELL_EFFECT_150:
                    if(Unit *unit = m_targets.getUnitTarget())
                        data.append(unit->GetPackGUID());
                    else if(m_targets.getItemTargetGUID())
                        data.appendPackGUID(m_targets.getItemTargetGUID());
                    else if(GameObject *go = m_targets.getGOTarget())
                        data.append(go->GetPackGUID());
                    else
                        data << uint8(0);                   // guid
                    break;
                case SPELL_EFFECT_FEED_PET:
                    data << uint32(m_targets.getItemTargetEntry());
                    break;
                case SPELL_EFFECT_DISMISS_PET:
                    if(Unit *unit = m_targets.getUnitTarget())
                        data.append(unit->GetPackGUID());
                    else
                        data << uint8(0);
                    break;
                default:
                    return;
            }
        }
    }

    m_caster->SendMessageToSet(&data, true);
}

void Spell::SendInterrupted(uint8 result)
{
    WorldPacket data(SMSG_SPELL_FAILURE, (8+4+1));
    data.append(m_caster->GetPackGUID());
    data << m_spellInfo->Id;
    data << result;
    m_caster->SendMessageToSet(&data, true);

    data.Initialize(SMSG_SPELL_FAILED_OTHER, (8+4));
    data.append(m_caster->GetPackGUID());
    data << m_spellInfo->Id;
    m_caster->SendMessageToSet(&data, true);
}

void Spell::SendChannelUpdate(uint32 time)
{
    if(time == 0)
    {
        m_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT,0);
        m_caster->SetUInt32Value(UNIT_CHANNEL_SPELL,0);
    }

    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data( MSG_CHANNEL_UPDATE, 8+4 );
    data.append(m_caster->GetPackGUID());
    data << time;

    ((Player*)m_caster)->GetSession()->SendPacket( &data );
}

void Spell::SendChannelStart(uint32 duration)
{
    WorldObject* target = NULL;

    // select first not rsusted target from target list for _0_ effect
    if(!m_UniqueTargetInfo.empty())
    {
        for(std::list<TargetInfo>::iterator itr= m_UniqueTargetInfo.begin();itr != m_UniqueTargetInfo.end();++itr)
        {
            if( (itr->effectMask & (1<<0)) && itr->reflectResult==SPELL_MISS_NONE && itr->targetGUID != m_caster->GetGUID())
            {
                target = ObjectAccessor::GetUnit(*m_caster, itr->targetGUID);
                break;
            }
        }
    }
    else if(!m_UniqueGOTargetInfo.empty())
    {
        for(std::list<GOTargetInfo>::iterator itr= m_UniqueGOTargetInfo.begin();itr != m_UniqueGOTargetInfo.end();++itr)
        {
            if(itr->effectMask & (1<<0) )
            {
                target = ObjectAccessor::GetGameObject(*m_caster, itr->targetGUID);
                break;
            }
        }
    }

    if (m_caster->GetTypeId() == TYPEID_PLAYER)
    {
        WorldPacket data( MSG_CHANNEL_START, (8+4+4) );
        data.append(m_caster->GetPackGUID());
        data << m_spellInfo->Id;
        data << duration;

        ((Player*)m_caster)->GetSession()->SendPacket( &data );
    }

    m_timer = duration;
    if(target)
        m_caster->SetUInt64Value(UNIT_FIELD_CHANNEL_OBJECT, target->GetGUID());
    m_caster->SetUInt32Value(UNIT_CHANNEL_SPELL, m_spellInfo->Id);
}

void Spell::SendResurrectRequest(Player* target)
{
    WorldPacket data(SMSG_RESURRECT_REQUEST, (8+4+2+4));
    data << m_caster->GetGUID();
    data << uint32(1) << uint16(0) << uint32(1);

    target->GetSession()->SendPacket(&data);
}

void Spell::SendPlaySpellVisual(uint32 SpellID)
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    WorldPacket data(SMSG_PLAY_SPELL_VISUAL, 12);
    data << m_caster->GetGUID();
    data << SpellID;
    ((Player*)m_caster)->GetSession()->SendPacket(&data);
}

void Spell::TakeCastItem()
{
    if(!m_CastItem || m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    // not remove cast item at triggered spell (equipping, weapon damage, etc)
    if(m_IsTriggeredSpell)
        return;

    ItemPrototype const *proto = m_CastItem->GetProto();

    if(!proto)
    {
        // This code is to avoid a crash
        // I'm not sure, if this is really an error, but I guess every item needs a prototype
        sLog.outError("Cast item has no item prototype highId=%d, lowId=%d",m_CastItem->GetGUIDHigh(), m_CastItem->GetGUIDLow());
        return;
    }

    bool expendable = false;
    bool withoutCharges = false;

    for (int i = 0; i<5; i++)
    {
        if (proto->Spells[i].SpellId)
        {
            // item has limited charges
            if (proto->Spells[i].SpellCharges)
            {
                if (proto->Spells[i].SpellCharges < 0)
                    expendable = true;

                int32 charges = m_CastItem->GetSpellCharges(i);

                // item has charges left
                if (charges)
                {
                    (charges > 0) ? --charges : ++charges;  // abs(charges) less at 1 after use
                    if (proto->Stackable < 2)
                        m_CastItem->SetSpellCharges(i, charges);
                    m_CastItem->SetState(ITEM_CHANGED, (Player*)m_caster);
                }

                // all charges used
                withoutCharges = (charges == 0);
            }
        }
    }

    if (expendable && withoutCharges)
    {
        uint32 count = 1;
        ((Player*)m_caster)->DestroyItemCount(m_CastItem, count, true);

        // prevent crash at access to deleted m_targets.getItemTarget
        if(m_CastItem==m_targets.getItemTarget())
            m_targets.setItemTarget(NULL);

        m_CastItem = NULL;
    }
}

void Spell::TakePower(uint32 mana)
{
    if(m_CastItem || m_triggeredByAuraSpell)
        return;

    // health as power used
    if(m_spellInfo->powerType == -2)
    {
        m_caster->ModifyHealth( -(int32)mana );
        return;
    }

    if(m_spellInfo->powerType <0 || m_spellInfo->powerType > POWER_HAPPINESS)
    {
        sLog.outError("Spell::TakePower: Unknown power type '%d'", m_spellInfo->powerType);
        return;
    }

    Powers powerType = Powers(m_spellInfo->powerType);

    m_caster->ModifyPower(powerType, -(int32)mana);
    if (powerType == POWER_MANA)
    {
        // Set the five second timer
        if (m_caster->GetTypeId() == TYPEID_PLAYER && mana > 0)
        {
            ((Player *)m_caster)->SetLastManaUse((uint32)getMSTime());
        }
    }
}

void Spell::TakeReagents()
{
    if(m_IsTriggeredSpell)                                  // reagents used in triggered spell removed by original spell or don't must be removed.
        return;

    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    Player* p_caster = (Player*)m_caster;

    for(uint32 x=0;x<8;x++)
    {
        if(m_spellInfo->Reagent[x] <= 0)
            continue;

        uint32 itemid = m_spellInfo->Reagent[x];
        uint32 itemcount = m_spellInfo->ReagentCount[x];

        // if CastItem is also spell reagent
        if (m_CastItem)
        {
            ItemPrototype const *proto = m_CastItem->GetProto();
            if( proto && proto->ItemId == itemid )
            {
                for(int s=0;s<5;s++)
                {
                    // CastItem will be used up and does not count as reagent
                    int32 charges = m_CastItem->GetSpellCharges(s);
                    if (proto->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                    {
                        ++itemcount;
                        break;
                    }
                }

                m_CastItem = NULL;
            }
        }

        // if getItemTarget is also spell reagent
        if (m_targets.getItemTargetEntry()==itemid)
            m_targets.setItemTarget(NULL);

        p_caster->DestroyItemCount(itemid, itemcount, true);
    }
}

void Spell::HandleThreatSpells(uint32 spellId)
{
    if(!m_targets.getUnitTarget() || !spellId)
        return;

    if(!m_targets.getUnitTarget()->CanHaveThreatList())
        return;

    SpellThreatEntry const *threatSpell = sSpellThreatStore.LookupEntry<SpellThreatEntry>(spellId);
    if(!threatSpell)
        return;

    m_targets.getUnitTarget()->AddThreat(m_caster, float(threatSpell->threat));

    DEBUG_LOG("Spell %u, rank %u, added an additional %i threat", spellId, spellmgr.GetSpellRank(spellId), threatSpell->threat);
}

void Spell::HandleEffects(Unit *pUnitTarget,Item *pItemTarget,GameObject *pGOTarget,uint32 i, float DamageMultiplier)
{
    unitTarget = pUnitTarget;
    itemTarget = pItemTarget;
    gameObjTarget = pGOTarget;

    uint8 eff = m_spellInfo->Effect[i];
    uint32 mechanic = m_spellInfo->EffectMechanic[i];

    damage = int32(CalculateDamage((uint8)i,unitTarget)*DamageMultiplier);

    sLog.outDebug( "Spell: Effect : %u", eff);

    //Simply return. Do not display "immune" in red text on client
    if(unitTarget && unitTarget->IsImmunedToSpellEffect(eff, mechanic))
        return;

    if(eff<TOTAL_SPELL_EFFECTS)
    {
        //sLog.outDebug( "WORLD: Spell FX %d < TOTAL_SPELL_EFFECTS ", eff);
        (*this.*SpellEffects[eff])(i);
    }
    /*
    else
    {
        sLog.outDebug( "WORLD: Spell FX %d > TOTAL_SPELL_EFFECTS ", eff);
        if (m_CastItem)
            EffectEnchantItemTmp(i);
        else
        {
            sLog.outError("SPELL: unknown effect %u spell id %u\n",
                eff, m_spellInfo->Id);
        }
    }
    */
}

void Spell::TriggerSpell()
{
    for(TriggerSpells::iterator si=m_TriggerSpells.begin(); si!=m_TriggerSpells.end(); ++si)
    {
        Spell* spell = new Spell(m_caster, (*si), true, m_originalCasterGUID, this->m_selfContainer);
        spell->prepare(&m_targets);                         // use original spell original targets
    }
}

uint8 Spell::CanCast(bool strict)
{
    // check cooldowns to prevent cheating
    if(m_caster->GetTypeId()==TYPEID_PLAYER && ((Player*)m_caster)->HasSpellCooldown(m_spellInfo->Id))
    {
        if(m_triggeredByAuraSpell)
            return SPELL_FAILED_DONT_REPORT;
        else
            return SPELL_FAILED_NOT_READY;
    }

    // only check at first call, Stealth auras are already removed at second call
    // for now, ignore triggered spells
    if( strict && !m_IsTriggeredSpell)
    {
        // Cannot be used in this stance/form
        if(uint8 shapeError = GetErrorAtShapeshiftedCast(m_spellInfo, m_caster->m_form))
            return shapeError;

        if ((m_spellInfo->Attributes & 0x20000) && !(m_caster->HasStealthAura()))
            return SPELL_FAILED_ONLY_STEALTHED;
    }

    // cancel autorepeat spells if cast start when moving
    // (not wand currently autorepeat cast delayed to moving stop anyway in spell update code)
    if( m_caster->GetTypeId()==TYPEID_PLAYER && ((Player*)m_caster)->isMoving() )
    {
        // skip stuck spell to allow use it in falling case and apply spell limitations at movement
        if( (!m_caster->HasUnitMovementFlag(MOVEMENTFLAG_FALLING) || m_spellInfo->Effect[0] != SPELL_EFFECT_STUCK) &&
            (IsAutoRepeat() || (m_spellInfo->AuraInterruptFlags & AURA_INTERRUPT_FLAG_NOT_SEATED) != 0) )
            return SPELL_FAILED_MOVING;
    }

    Unit *target = m_targets.getUnitTarget();

    if(target)
    {
        if(target != m_caster)
        {
            // Not allow casting on flying player
            if (target->isInFlight())
                return SPELL_FAILED_BAD_TARGETS;

            if(VMAP::VMapFactory::checkSpellForLoS(m_spellInfo->Id) && !m_caster->IsWithinLOSInMap(target))
                return SPELL_FAILED_LINE_OF_SIGHT;

            // auto selection spell rank implemented in WorldSession::HandleCastSpellOpcode
            // this case can be triggered if rank not found (too low-level target for first rank)
            if(m_caster->GetTypeId() == TYPEID_PLAYER && !IsPassiveSpell(m_spellInfo->Id) && !m_CastItem)
            {
                for(int i=0;i<3;i++)
                {
                    if(IsPositiveEffect(m_spellInfo->Id, i) && m_spellInfo->Effect[i] == SPELL_EFFECT_APPLY_AURA)
                        if(target->getLevel() + 10 < m_spellInfo->spellLevel)
                            return SPELL_FAILED_LOWLEVEL;
                }
            }
        }

        // check pet presents
        for(int j=0;j<3;j++)
        {
            if(m_spellInfo->EffectImplicitTargetA[j] == TARGET_PET)
            {
                target = m_caster->GetPet();
                if(!target)
                {
                    if(m_triggeredByAuraSpell)              // not report pet not existence for triggered spells
                        return SPELL_FAILED_DONT_REPORT;
                    else
                        return SPELL_FAILED_NO_PET;
                }
                break;
            }
        }

        //check creature type
        //ignore self casts (including area casts when caster selected as target)
        if(target != m_caster)
        {
            if(!CheckTargetCreatureType(target))
            {
                if(target->GetTypeId()==TYPEID_PLAYER)
                    return SPELL_FAILED_TARGET_IS_PLAYER;
                else
                    return SPELL_FAILED_BAD_TARGETS;
            }
        }

        // TODO: this check can be applied and for player to prevent cheating when IsPositiveSpell will return always correct result.
        // check target for pet/charmed casts (not self targeted), self targeted cast used for area effects and etc
        if(m_caster != target && m_caster->GetTypeId()==TYPEID_UNIT && m_caster->GetCharmerOrOwnerGUID())
        {
            // check correctness positive/negative cast target (pet cast real check and cheating check)
            if(IsPositiveSpell(m_spellInfo->Id))
            {
                if(m_caster->IsHostileTo(target))
                    return SPELL_FAILED_BAD_TARGETS;
            }
            else
            {
                if(m_caster->IsFriendlyTo(target))
                    return SPELL_FAILED_BAD_TARGETS;
            }
        }

        if(IsPositiveSpell(m_spellInfo->Id))
        {
            if(target->IsImmunedToSpell(m_spellInfo,false))
                return SPELL_FAILED_TARGET_AURASTATE;
        }

        /* Causes problems with berserking
        if (m_spellInfo->CasterAuraState && !(m_caster->HasFlag(UNIT_FIELD_AURASTATE, (1<<(m_spellInfo->CasterAuraState-1)))))
            return SPELL_FAILED_CASTER_AURASTATE;

        if (m_spellInfo->TargetAuraState && !(target->HasFlag(UNIT_FIELD_AURASTATE, (1<<(m_spellInfo->TargetAuraState-1)))))
            return SPELL_FAILED_TARGET_AURASTATE;*/

        //Must be behind the target.
        if( m_spellInfo->AttributesEx2 == 0x100000 && (m_spellInfo->AttributesEx & 0x200) == 0x200 && target->HasInArc(M_PI, m_caster) )
        {
            SendInterrupted(2);
            return SPELL_FAILED_NOT_BEHIND;
        }

        //Target must be facing you.
        if((m_spellInfo->Attributes == 0x150010) && !target->HasInArc(M_PI, m_caster) )
        {
            SendInterrupted(2);
            return SPELL_FAILED_NOT_INFRONT;
        }
    }

    if((m_spellInfo->AttributesEx3 & 0x800) != 0)
        return SPELL_FAILED_ONLY_BATTLEGROUNDS;

    // zone check
    if(!IsSpellAllowedInLocation(m_spellInfo,m_caster->GetMapId(),m_caster->GetZoneId(),m_caster->GetAreaId()))
        return SPELL_FAILED_REQUIRES_AREA;

    // not let players cast spells at mount (and let do it to creatures)
    if(m_caster->IsMounted() && m_caster->GetTypeId()==TYPEID_PLAYER && !m_IsTriggeredSpell && !IsPassiveSpell(m_spellInfo->Id) && !(m_spellInfo->Attributes & 0x1000000))
    {
        if(m_caster->isInFlight())
            return SPELL_FAILED_NOT_FLYING;
        else
            return SPELL_FAILED_NOT_MOUNTED;
    }

    // always (except passive spells) check items (focus object can be required for any type casts)
    if(!IsPassiveSpell(m_spellInfo->Id))
        if(uint8 castResult = CheckItems())
            return castResult;

    //ImpliciteTargetA-B = 38, If fact there is 0 Spell with  ImpliciteTargetB=38
    if(m_UniqueTargetInfo.empty())                          // skip second canCast apply (for delayed spells for example)
    {
        for(uint8 j = 0; j < 3; j++)
        {
            if( m_spellInfo->EffectImplicitTargetA[j] == TARGET_SCRIPT || m_spellInfo->EffectImplicitTargetB[j] == TARGET_SCRIPT )
            {
                bool okDoo = false;

                SpellScriptTarget::const_iterator lower = spellmgr.GetBeginSpellScriptTarget(m_spellInfo->Id);
                SpellScriptTarget::const_iterator upper = spellmgr.GetEndSpellScriptTraget(m_spellInfo->Id);
                if(lower==upper)
                    sLog.outErrorDb("Spell (ID: %u) has effect EffectImplicitTargetA/EffectImplicitTargetB = %u (TARGET_SCRIPT), but does not have record in `spell_script_target`",m_spellInfo->Id,TARGET_SCRIPT);

                SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex);
                float range = GetSpellMaxRange(srange);

                Creature* creatureScriptTarget = NULL;
                GameObject* goScriptTarget = NULL;

                for(SpellScriptTarget::const_iterator i_spellST = lower; i_spellST != upper; ++i_spellST)
                {
                    switch(i_spellST->second.type)
                    {
                        case SPELL_TARGET_TYPE_GAMEOBJECT:
                        {
                            GameObject* p_GameObject = NULL;

                            if(i_spellST->second.targetEntry)
                            {
                                CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                                Cell cell(p);
                                cell.data.Part.reserved = ALL_DISTRICT;

                                MaNGOS::NearestGameObjectEntryInObjectRangeCheck go_check(*m_caster,i_spellST->second.targetEntry,range);
                                MaNGOS::GameObjectLastSearcher<MaNGOS::NearestGameObjectEntryInObjectRangeCheck> checker(p_GameObject,go_check);

                                TypeContainerVisitor<MaNGOS::GameObjectLastSearcher<MaNGOS::NearestGameObjectEntryInObjectRangeCheck>, GridTypeMapContainer > object_checker(checker);
                                CellLock<GridReadGuard> cell_lock(cell, p);
                                cell_lock->Visit(cell_lock, object_checker, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));

                                if(p_GameObject)
                                {
                                    // remember found target and range, next attempt will find more near target with another entry
                                    creatureScriptTarget = NULL;
                                    goScriptTarget = p_GameObject;
                                    range = go_check.GetLastRange();
                                }
                            }
                            else if( focusObject )          //Focus Object
                            {
                                float frange = m_caster->GetDistance(focusObject);
                                if(range >= frange)
                                {
                                    creatureScriptTarget = NULL;
                                    goScriptTarget = focusObject;
                                    range = frange;
                                }
                            }
                            break;
                        }
                        case SPELL_TARGET_TYPE_CREATURE:
                        case SPELL_TARGET_TYPE_DEAD:
                        default:
                        {
                            Creature *p_Creature = NULL;

                            CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                            Cell cell(p);
                            cell.data.Part.reserved = ALL_DISTRICT;
                            cell.SetNoCreate();             // Really don't know what is that???

                            MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck u_check(*m_caster,i_spellST->second.targetEntry,i_spellST->second.type!=SPELL_TARGET_TYPE_DEAD,range);
                            MaNGOS::CreatureLastSearcher<MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck> searcher(p_Creature, u_check);

                            TypeContainerVisitor<MaNGOS::CreatureLastSearcher<MaNGOS::NearestCreatureEntryWithLiveStateInObjectRangeCheck>, GridTypeMapContainer >  grid_creature_searcher(searcher);

                            CellLock<GridReadGuard> cell_lock(cell, p);
                            cell_lock->Visit(cell_lock, grid_creature_searcher, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));

                            if(p_Creature )
                            {
                                creatureScriptTarget = p_Creature;
                                goScriptTarget = NULL;
                                range = u_check.GetLastRange();
                            }
                            break;
                        }
                    }
                }

                if(creatureScriptTarget)
                    AddUnitTarget(creatureScriptTarget, j);
                else if(goScriptTarget)
                    AddGOTarget(goScriptTarget, j);
                else
                    return SPELL_FAILED_BAD_TARGETS;        //Missing DB Entry or targets for this spellEffect.
            }
        }
    }

    if(uint8 castResult = CheckRange(strict))
        return castResult;

    {
        uint32 mana = 0;
        if(uint8 castResult = CheckMana(&mana))
            return castResult;
    }

    if(!m_triggeredByAuraSpell)                             // triggered spell not affected by stun/etc
        if(uint8 castResult = CheckCasterAuras())
            return castResult;

    for (int i = 0; i < 3; i++)
    {
        // for effects of spells that have only one target
        switch(m_spellInfo->Effect[i])
        {
            case SPELL_EFFECT_DUMMY:
            {
                if (!m_targets.getUnitTarget()&&!m_targets.getGOTarget()&&!m_targets.getItemTarget())
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                // Execute
                if(m_spellInfo->SpellIconID == 1648)
                {
                    if(!m_targets.getUnitTarget() || m_targets.getUnitTarget()->GetHealth() > m_targets.getUnitTarget()->GetMaxHealth()*0.2)
                        return SPELL_FAILED_BAD_TARGETS;
                }
                break;
            }
            case SPELL_EFFECT_SCHOOL_DAMAGE:
            {
                // Hammer of Wrath
                if(m_spellInfo->SpellVisual == 7250)
                {
                    if (!m_targets.getUnitTarget())
                        return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                    if(m_targets.getUnitTarget()->GetHealth() > m_targets.getUnitTarget()->GetMaxHealth()*0.2)
                        return SPELL_FAILED_BAD_TARGETS;
                }
                break;
            }
            case SPELL_EFFECT_TAMECREATURE:
            {
                if (!m_targets.getUnitTarget() || m_targets.getUnitTarget()->GetTypeId() == TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                if (m_targets.getUnitTarget()->getLevel() > m_caster->getLevel())
                    return SPELL_FAILED_HIGHLEVEL;

                CreatureInfo const *cinfo = ((Creature*)m_targets.getUnitTarget())->GetCreatureInfo();
                CreatureFamilyEntry const* cFamily = sCreatureFamilyStore.LookupEntry(cinfo->family);
                if( cinfo->type != CREATURE_TYPE_BEAST || !cFamily || !cFamily->tamable )
                    return SPELL_FAILED_BAD_TARGETS;

                if(m_caster->GetPetGUID())
                    return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                if(m_caster->GetCharmGUID())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                break;
            }
            case SPELL_EFFECT_LEARN_SPELL:
            {
                if(m_spellInfo->EffectImplicitTargetA[i] != TARGET_PET)
                    break;

                Pet* pet = m_caster->GetPet();

                if(!pet)
                    return SPELL_FAILED_NO_PET;

                SpellEntry const *learn_spellproto = sSpellStore.LookupEntry(m_spellInfo->EffectTriggerSpell[i]);

                if(!learn_spellproto)
                    return SPELL_FAILED_NOT_KNOWN;

                if(!pet->CanTakeMoreActiveSpells(learn_spellproto->Id))
                    return SPELL_FAILED_TOO_MANY_SKILLS;

                if(m_spellInfo->spellLevel > pet->getLevel())
                    return SPELL_FAILED_LOWLEVEL;

                if(!pet->HasTPForSpell(learn_spellproto->Id))
                    return SPELL_FAILED_TRAINING_POINTS;

                break;
            }
            case SPELL_EFFECT_LEARN_PET_SPELL:
            {
                Pet* pet = m_caster->GetPet();

                if(!pet)
                    return SPELL_FAILED_NO_PET;

                SpellEntry const *learn_spellproto = sSpellStore.LookupEntry(m_spellInfo->EffectTriggerSpell[i]);

                if(!learn_spellproto)
                    return SPELL_FAILED_NOT_KNOWN;

                if(!pet->CanTakeMoreActiveSpells(learn_spellproto->Id))
                    return SPELL_FAILED_TOO_MANY_SKILLS;

                if(m_spellInfo->spellLevel > pet->getLevel())
                    return SPELL_FAILED_LOWLEVEL;

                if(!pet->HasTPForSpell(learn_spellproto->Id))
                    return SPELL_FAILED_TRAINING_POINTS;

                break;
            }
            case SPELL_EFFECT_FEED_PET:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER || !m_targets.getItemTarget() )
                    return SPELL_FAILED_BAD_TARGETS;

                Pet* pet = m_caster->GetPet();

                if(!pet)
                    return SPELL_FAILED_NO_PET;

                if(!pet->HaveInDiet(m_targets.getItemTarget()->GetProto()))
                    return SPELL_FAILED_WRONG_PET_FOOD;

                if(!pet->GetCurrentFoodBenefitLevel(m_targets.getItemTarget()->GetProto()->ItemLevel))
                    return SPELL_FAILED_FOOD_LOWLEVEL;

                if(m_caster->isInCombat() || pet->isInCombat())
                    return SPELL_FAILED_AFFECTING_COMBAT;

                break;
            }
            case SPELL_EFFECT_SKINNING:
            {
                if (m_caster->GetTypeId() != TYPEID_PLAYER || !m_targets.getUnitTarget() || m_targets.getUnitTarget()->GetTypeId() != TYPEID_UNIT)
                    return SPELL_FAILED_BAD_TARGETS;

                if( !(m_targets.getUnitTarget()->GetUInt32Value(UNIT_FIELD_FLAGS) & UNIT_FLAG_SKINNABLE) )
                    return SPELL_FAILED_TARGET_UNSKINNABLE;

                if ( m_targets.getUnitTarget()->GetCreatureType() != CREATURE_TYPE_CRITTER
                    && ( !((Creature*)m_targets.getUnitTarget())->lootForBody || !((Creature*)m_targets.getUnitTarget())->loot.empty() ) )
                {
                    return SPELL_FAILED_TARGET_NOT_LOOTED;
                }

                int32 SkinningValue = ((Player*)m_caster)->GetSkillValue(SKILL_SKINNING);
                int32 TargetLevel = m_targets.getUnitTarget()->getLevel();
                int32 ReqValue = (SkinningValue < 100 ? (TargetLevel-10)*10 : TargetLevel*5);
                if (ReqValue > SkinningValue)
                    return SPELL_FAILED_LOW_CASTLEVEL;

                // chance for fail at orange skinning attempt
                if( (m_selfContainer && (*m_selfContainer) == this) && 
                    SkinningValue < sWorld.GetConfigMaxSkillValue() &&
                    (ReqValue < 0 ? 0 : ReqValue) > irand(SkinningValue-25, SkinningValue+37) )
                    return SPELL_FAILED_TRY_AGAIN;

                break;
            }
            case SPELL_EFFECT_OPEN_LOCK_ITEM:
            case SPELL_EFFECT_OPEN_LOCK:
            {
                if( m_spellInfo->EffectImplicitTargetA[i] != TARGET_GAMEOBJECT &&
                    m_spellInfo->EffectImplicitTargetA[i] != TARGET_GAMEOBJECT_ITEM )
                    break;

                if( m_caster->GetTypeId() != TYPEID_PLAYER  // only players can open locks, gather etc.
                    // we need a go target in case of TARGET_GAMEOBJECT
                    || m_spellInfo->EffectImplicitTargetA[i] == TARGET_GAMEOBJECT && !m_targets.getGOTarget()
                    // we need a go target, or an openable item target in case of TARGET_GAMEOBJECT_ITEM
                    || m_spellInfo->EffectImplicitTargetA[i] == TARGET_GAMEOBJECT_ITEM && !m_targets.getGOTarget() &&
                    (!m_targets.getItemTarget() || !(m_targets.getItemTarget()->GetProto()->LockID)) )
                    return SPELL_FAILED_BAD_TARGETS;

                // get the lock entry
                LockEntry const *lockInfo = NULL;
                if (GameObject* go=m_targets.getGOTarget())
                    lockInfo = sLockStore.LookupEntry(go->GetLockId());
                else if(Item* itm=m_targets.getItemTarget())
                    lockInfo = sLockStore.LookupEntry(itm->GetProto()->LockID);

                // check lock compatibility
                if (lockInfo)
                {
                    // check for lock - key pair (checked by client also, just prevent cheating
                    bool ok_key = false;
                    for(int it = 0; it < 5; ++it)
                    {
                        switch(lockInfo->keytype[it])
                        {
                            case LOCK_KEY_NONE:
                                break;
                            case LOCK_KEY_ITEM:
                            {
                                if(lockInfo->key[it])
                                {
                                    if(m_CastItem && m_CastItem->GetEntry()==lockInfo->key[it])
                                        ok_key =true;
                                    break;
                                }
                            }
                            case LOCK_KEY_SKILL:
                            {
                                if(uint32(m_spellInfo->EffectMiscValue[i])!=lockInfo->key[it])
                                    break;

                                switch(lockInfo->key[it])
                                {
                                    case LOCKTYPE_HERBALISM:
                                        if(((Player*)m_caster)->HasSkill(SKILL_HERBALISM))
                                            ok_key =true;
                                        break;
                                    case LOCKTYPE_MINING:
                                        if(((Player*)m_caster)->HasSkill(SKILL_MINING))
                                            ok_key =true;
                                        break;
                                    default:
                                        ok_key =true;
                                        break;
                                }
                            }
                        }
                        if(ok_key)
                            break;
                    }

                    if(!ok_key)
                        return SPELL_FAILED_BAD_TARGETS;
                }

                // chance for fail at orange mining/herb/LockPicking gathering attempt
                if (!m_selfContainer || ((*m_selfContainer) != this))
                    break;

                // get the skill value of the player
                int32 SkillValue = 0;
                bool canFailAtMax = true;
                if (m_spellInfo->EffectMiscValue[i] == LOCKTYPE_HERBALISM)
                {
                    SkillValue = ((Player*)m_caster)->GetSkillValue(SKILL_HERBALISM);
                    canFailAtMax = false;
                }
                else if (m_spellInfo->EffectMiscValue[i] == LOCKTYPE_MINING)
                {
                    SkillValue = ((Player*)m_caster)->GetSkillValue(SKILL_MINING);
                    canFailAtMax = false;
                }
                else if (m_spellInfo->EffectMiscValue[i] == LOCKTYPE_PICKLOCK)
                    SkillValue = ((Player*)m_caster)->GetSkillValue(SKILL_LOCKPICKING);

                // castitem check: rogue using skeleton keys. the skill values should not be added in this case.
                if(m_CastItem)
                    SkillValue = 0;

                // add the damage modifier from the spell casted (cheat lock / skeleton key etc.) (use m_currentBasePoints, CalculateDamage returns wrong value)
                SkillValue += m_currentBasePoints[i]+1;

                // get the required lock value
                int32 ReqValue=0;
                if (lockInfo)
                {
                    // check for lock - key pair
                    bool ok = false;
                    for(int it = 0; it < 5; ++it)
                    {
                        if(lockInfo->keytype[it]==LOCK_KEY_ITEM && lockInfo->key[it] && m_CastItem && m_CastItem->GetEntry()==lockInfo->key[it])
                        {
                            // if so, we're good to go
                            ok = true;
                            break;
                        }
                    }
                    if(ok)
                        break;

                    if (m_spellInfo->EffectMiscValue[i] == LOCKTYPE_PICKLOCK)
                        ReqValue = lockInfo->requiredlockskill;
                    else
                        ReqValue = lockInfo->requiredminingskill;
                }

                // skill doesn't meet the required value
                if (ReqValue > SkillValue)
                    return SPELL_FAILED_LOW_CASTLEVEL;

                // chance for failure in orange gather / lockpick (gathering skill can't fail at maxskill)
                if((canFailAtMax || SkillValue < sWorld.GetConfigMaxSkillValue()) && ReqValue > irand(SkillValue-25, SkillValue+37))
                    return SPELL_FAILED_TRY_AGAIN;

                break;
            }
            case SPELL_EFFECT_SUMMON_DEAD_PET:
            {
                Creature *pet = m_caster->GetPet();
                if(!pet)
                    return SPELL_FAILED_NO_PET;

                if(pet->isAlive())
                    return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                break;
            }
            case SPELL_EFFECT_SUMMON:
                //case SPELL_EFFECT_SUMMON_WILD:                //not store in pet field
                //case SPELL_EFFECT_SUMMON_GUARDIAN:            //not store in pet field
            case SPELL_EFFECT_SUMMON_POSSESSED:
            case SPELL_EFFECT_SUMMON_PHANTASM:
            case SPELL_EFFECT_SUMMON_CRITTER:               //not store in pet field
            case SPELL_EFFECT_SUMMON_DEMON:
            {
                if(m_caster->GetPetGUID())
                    return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                if(m_caster->GetCharmGUID())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                break;
            }
            case SPELL_EFFECT_SUMMON_PET:
            {

                if(m_caster->GetPetGUID())                  //let warlock do a replacement summon
                {

                    Pet* pet = ((Player*)m_caster)->GetPet();

                    if (m_caster->GetTypeId()==TYPEID_PLAYER && m_caster->getClass()==CLASS_WARLOCK)
                    {
                        if (strict)                         //starting cast, trigger pet stun (cast by pet so it doesn't attack player)
                            pet->CastSpell(pet, 32752, true, NULL, NULL, m_caster->GetGUID());
                    }
                    else
                        return SPELL_FAILED_ALREADY_HAVE_SUMMON;
                }

                if(m_caster->GetCharmGUID())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                break;
            }
            case SPELL_EFFECT_SUMMON_PLAYER:
            {
                if(m_caster->GetTypeId()!=TYPEID_PLAYER)
                    return SPELL_FAILED_BAD_TARGETS;
                if(!((Player*)m_caster)->GetSelection())
                    return SPELL_FAILED_BAD_TARGETS;

                Player* target = objmgr.GetPlayer(((Player*)m_caster)->GetSelection());
                if( !target || ((Player*)m_caster)==target || !target->IsInSameGroupWith((Player*)m_caster) )
                    return SPELL_FAILED_BAD_TARGETS;

                if( target->isInCombat() )
                    return SPELL_FAILED_TARGET_IN_COMBAT;

                // check if our map is dungeon
                if(MapManager::Instance().GetBaseMap(m_caster->GetMapId())->IsDungeon() )
                {
                    InstanceTemplate const* instance = ObjectMgr::GetInstanceTemplate(m_caster->GetMapId());
                    if(!instance)
                        return SPELL_FAILED_TARGET_NOT_IN_INSTANCE;
                    if ( instance->levelMin > target->getLevel() )
                        return SPELL_FAILED_LOWLEVEL;
                    if ( instance->levelMax < target->getLevel() )
                        return SPELL_FAILED_HIGHLEVEL;
                }
                break;
            }
            case SPELL_EFFECT_LEAP:
            case SPELL_EFFECT_TELEPORT_UNITS_FACE_CASTER:
            {
                float dis = GetSpellRadius(sSpellRadiusStore.LookupEntry(m_spellInfo->EffectRadiusIndex[i]));
                float fx = m_caster->GetPositionX() + dis * cos(m_caster->GetOrientation());
                float fy = m_caster->GetPositionY() + dis * sin(m_caster->GetOrientation());
                // teleport a bit above terrainlevel to avoid falling below it
                float fz = MapManager::Instance().GetBaseMap(m_caster->GetMapId())->GetHeight(fx,fy,m_caster->GetPositionZ(),true);
                if(fz <= INVALID_HEIGHT)                    // note: this also will prevent use effect in instances without vmaps height enabled
                    return SPELL_FAILED_TRY_AGAIN;

                float caster_pos_z = m_caster->GetPositionZ();
                // Control the caster to not climb or drop when +-fz > 8
                if(!(fz<=caster_pos_z+8 && fz>=caster_pos_z-8))
                    return SPELL_FAILED_TRY_AGAIN;

                // not allow use this effect at battleground until battleground start
                if(m_caster->GetTypeId()==TYPEID_PLAYER)
                    if(BattleGround const *bg = ((Player*)m_caster)->GetBattleGround())
                        if(bg->GetStatus() != STATUS_IN_PROGRESS)
                            return SPELL_FAILED_TRY_AGAIN;
                break;
            }
            default:break;
        }
    }

    for (int i = 0; i < 3; i++)
    {
        switch(m_spellInfo->EffectApplyAuraName[i])
        {
            case SPELL_AURA_MOD_POSSESS:
            case SPELL_AURA_MOD_CHARM:
            {
                if(m_caster->GetPetGUID())
                    return SPELL_FAILED_ALREADY_HAVE_SUMMON;

                if(m_caster->GetCharmGUID())
                    return SPELL_FAILED_ALREADY_HAVE_CHARM;

                if(m_caster->GetCharmerGUID())
                    return SPELL_FAILED_CHARMED;

                if(!m_targets.getUnitTarget())
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                if(m_targets.getUnitTarget()->GetCharmerGUID())
                    return SPELL_FAILED_CHARMED;

                if(int32(m_targets.getUnitTarget()->getLevel()) > CalculateDamage(i,m_targets.getUnitTarget()))
                    return SPELL_FAILED_HIGHLEVEL;
            };break;
            case SPELL_AURA_MOD_STEALTH:
            case SPELL_AURA_MOD_INVISIBILITY:
            {
                /*
                Using stealth does not depend on the distance to mobs (as far as I know: ralf)
                If this this code is in place, it is not possible to use stealth while in Vanish mode
                                //detect if any mod is in x range.if true,can't steath.FIX ME!
                                if(m_spellInfo->Attributes == 169148432 || m_caster->GetTypeId() != TYPEID_PLAYER)
                                    break;

                                // Find nearby creatures
                                CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
                                Cell cell(p);
                                cell.data.Part.reserved = ALL_DISTRICT;

                                Creature* found_creature = NULL;

                                MaNGOS::InAttackDistanceFromAnyHostileCreatureCheck u_check(m_caster);
                                MaNGOS::CreatureSearcher<MaNGOS::InAttackDistanceFromAnyHostileCreatureCheck> checker(found_creature, u_check);
                                TypeContainerVisitor<MaNGOS::CreatureSearcher<MaNGOS::InAttackDistanceFromAnyHostileCreatureCheck>, GridTypeMapContainer > object_checker(checker);
                                CellLock<GridReadGuard> cell_lock(cell, p);
                                cell_lock->Visit(cell_lock, object_checker, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));
                */
            };break;
            case SPELL_AURA_MOUNTED:
            {
                if (m_caster->IsInWater())
                    return SPELL_FAILED_ONLY_ABOVEWATER;

                if (m_caster->GetTypeId()==TYPEID_PLAYER && ((Player*)m_caster)->GetTransport())
                    return SPELL_FAILED_NO_MOUNTS_ALLOWED;

                // Ignore map check if spell have AreaId. AreaId already checked and this prevent special mount spells
                if (m_caster->GetTypeId()==TYPEID_PLAYER && !((Player*)m_caster)->GetBaseMap()->IsMountAllowed() && !m_IsTriggeredSpell && !m_spellInfo->AreaId)
                    return SPELL_FAILED_NO_MOUNTS_ALLOWED;

                if (m_caster->GetAreaId()==35)
                    return SPELL_FAILED_NO_MOUNTS_ALLOWED;

                uint32 form = m_caster->m_form;
                if( form == FORM_CAT          || form == FORM_TREE      || form == FORM_TRAVEL   || 
                    form == FORM_AQUA         || form == FORM_BEAR      || form == FORM_DIREBEAR ||
                    form == FORM_CREATUREBEAR || form == FORM_GHOSTWOLF || form == FORM_FLIGHT   ||
                    form == FORM_FLIGHT_EPIC  || form == FORM_MOONKIN )
                    return SPELL_FAILED_NOT_SHAPESHIFT;

                break;
            }
            case SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS:
            {
                if(!m_targets.getUnitTarget())
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;

                // can be casted at non-friendly unit or own pet/charm
                if(m_caster->IsFriendlyTo(m_targets.getUnitTarget()))
                    return SPELL_FAILED_TARGET_FRIENDLY;
            };break;
            case SPELL_AURA_MOD_INCREASE_FLIGHT_SPEED:
            case SPELL_AURA_FLY:
            {
                // not allow cast fly spells at old maps by players (all spells is self target)
                if(m_caster->GetTypeId()==TYPEID_PLAYER)
                {
                    if( !((Player*)m_caster)->isGameMaster() && 
                        GetVirtualMapForMapAndZone(m_caster->GetMapId(),m_caster->GetZoneId()) != 530)
                        return SPELL_FAILED_NOT_HERE;
                }
            };break;
            default:break;
        }
    }

    // all ok
    return 0;
}

int16 Spell::PetCanCast(Unit* target)
{
    if(!m_caster->isAlive())
        return SPELL_FAILED_CASTER_DEAD;

    if(m_caster->IsNonMeleeSpellCasted(false))              //prevent spellcast interuption by another spellcast
        return SPELL_FAILED_SPELL_IN_PROGRESS;
    if(m_caster->isInCombat() && IsNonCombatSpell(m_spellInfo))
        return SPELL_FAILED_AFFECTING_COMBAT;

    if(m_caster->GetTypeId()==TYPEID_UNIT && (((Creature*)m_caster)->isPet() || m_caster->isCharmed()))
    {
                                                            //dead owner (pets still alive when owners ressed?)
        if(m_caster->GetCharmerOrOwner() && !m_caster->GetCharmerOrOwner()->isAlive())
            return SPELL_FAILED_CASTER_DEAD;

        if(!target && m_targets.getUnitTarget())
            target = m_targets.getUnitTarget();

        bool need = false;
        for(uint32 i = 0;i<3;i++)
        {
            if(m_spellInfo->EffectImplicitTargetA[i] == TARGET_CHAIN_DAMAGE || m_spellInfo->EffectImplicitTargetA[i] == TARGET_SINGLE_FRIEND || m_spellInfo->EffectImplicitTargetA[i] == TARGET_DUELVSPLAYER || m_spellInfo->EffectImplicitTargetA[i] == TARGET_SINGLE_PARTY || m_spellInfo->EffectImplicitTargetA[i] == TARGET_CURRENT_SELECTED_ENEMY)
            {
                need = true;
                if(!target)
                    return SPELL_FAILED_BAD_IMPLICIT_TARGETS;
                break;
            }
        }
        if(need)
            m_targets.setUnitTarget(target);

        Unit* _target = m_targets.getUnitTarget();

        if(_target)                                         //for target dead/target not valid
        {
            if(!_target->isAlive())
                return SPELL_FAILED_BAD_TARGETS;

            if(IsPositiveSpell(m_spellInfo->Id))
            {
                if(m_caster->IsHostileTo(_target))
                    return SPELL_FAILED_BAD_TARGETS;
            }
            else
            {
                bool duelvsplayertar = false;
                for(int j=0;j<3;j++)
                {
                                                            //TARGET_DUELVSPLAYER is positive AND negative
                    duelvsplayertar |= (m_spellInfo->EffectImplicitTargetA[j] == TARGET_DUELVSPLAYER);
                }
                if(m_caster->IsFriendlyTo(target) && !duelvsplayertar)
                {
                    return SPELL_FAILED_BAD_TARGETS;
                }
            }
        }
                                                            //cooldown
        if(((Creature*)m_caster)->HasSpellCooldown(m_spellInfo->Id))
            return SPELL_FAILED_NOT_READY;
    }

    uint16 result = CanCast(true);
    if(result != 0)
        return result;
    else
        return -1;                                          //this allows to check spell fail 0, in combat
}

uint8 Spell::CheckCasterAuras() const
{
    // Flag drop spells totally immuned to caster auras
    // FIXME: find more nice check for all totally immuned spells
    if(m_spellInfo->Id==23336 || m_spellInfo->Id==23334 || m_spellInfo->Id==34991)
        return 0;

    uint8 school_immune = 0;
    uint32 mechanic_immune = 0;

    //Check if the spell grants school or mechanic immunity.
    //We use bitmasks so the loop is done only once and not on every aura check below.
    for(int i = 0;i < 3; i ++)
    {
        if(m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_SCHOOL_IMMUNITY)
            school_immune |= uint32(m_spellInfo->EffectMiscValue[i]);
        else if(m_spellInfo->EffectApplyAuraName[i] == SPELL_AURA_MECHANIC_IMMUNITY)
            mechanic_immune |= 1 << uint32(m_spellInfo->EffectMiscValue[i]);
    }
    //immune movement impairement and loss of control
    if(m_spellInfo->Id==(uint32)42292)mechanic_immune=0x9967da6;

    //Check whether the cast should be prevented by any state you might have.
    uint8 prevented_reason = 0;
    if(m_caster->hasUnitState(UNIT_STAT_STUNDED))
        prevented_reason = SPELL_FAILED_STUNNED;
    else if(m_caster->hasUnitState(UNIT_STAT_CONFUSED))
        prevented_reason = SPELL_FAILED_CONFUSED;
    else if(m_caster->hasUnitState(UNIT_STAT_FLEEING))
        prevented_reason = SPELL_FAILED_FLEEING;
    else if(m_caster->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SILENCED) && (GetSpellSchoolMask(m_spellInfo) & SPELL_SCHOOL_MASK_NORMAL)==0)
        prevented_reason = SPELL_FAILED_SILENCED;
    else if(m_caster->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PACIFIED) && (GetSpellSchoolMask(m_spellInfo) & SPELL_SCHOOL_MASK_NORMAL) && !m_triggeredByAuraSpell)
        prevented_reason = SPELL_FAILED_PACIFIED;

    // Attr must make flag drop spell totally immuned from all effects
    if(prevented_reason)
    {
        if(school_immune || mechanic_immune)
        {
            //Checking auras is needed now, because you are prevented by some state but the spell grants immunity.
            Unit::AuraMap auras = m_caster->GetAuras();
            for(Unit::AuraMap::iterator itr = auras.begin(); itr != auras.end(); itr++)
            {
                if(itr->second)
                {
                    if( (GetSpellSchoolMask(itr->second->GetSpellProto()) & school_immune) ||
                        ((1 << itr->second->GetSpellProto()->Mechanic) & mechanic_immune)  ||
                        ((1 << itr->second->GetSpellProto()->EffectMechanic[itr->second->GetEffIndex()]) & mechanic_immune))
                        continue;

                    //Make a second check for spell failed so the right SPELL_FAILED message is returned.
                    //That is needed when your casting is prevented by multiple states and you are only immune to some of them.
                    switch(itr->second->GetModifier()->m_auraname)
                    {
                        case SPELL_AURA_MOD_STUN:
                            return SPELL_FAILED_STUNNED;
                        case SPELL_AURA_MOD_CONFUSE:
                            return SPELL_FAILED_CONFUSED;
                        case SPELL_AURA_MOD_FEAR:
                            return SPELL_FAILED_FLEEING;
                        case SPELL_AURA_MOD_SILENCE:
                            if((GetSpellSchoolMask(m_spellInfo) & SPELL_SCHOOL_MASK_NORMAL)==0)
                                return SPELL_FAILED_SILENCED;
                            break;
                        case SPELL_AURA_MOD_PACIFY:
                            if(GetSpellSchoolMask(m_spellInfo) & SPELL_SCHOOL_MASK_NORMAL)
                                return SPELL_FAILED_PACIFIED;
                            break;
                    }
                }
            }
        }
        //You are prevented from casting and the spell casted does not grant immunity. Return a failed error.
        else
            return prevented_reason;
    }
    return 0;                                               // all ok
}

bool Spell::CanAutoCast(Unit* target)
{
    uint64 targetguid = target->GetGUID();

    for(uint32 j = 0;j<3;j++)
    {
        if((m_spellInfo->Effect[j] == SPELL_EFFECT_APPLY_AURA || m_spellInfo->Effect[j] == SPELL_EFFECT_APPLY_AREA_AURA) && target->HasAura(m_spellInfo->Id, j))
            return false;                                   //don't buff an already buffed unit
    }

    int16 result = PetCanCast(target);

    if(result == -1 || result == SPELL_FAILED_UNIT_NOT_INFRONT)
    {
        FillTargetMap();
        //check if among target units, our WANTED target is as well (->only self cast spells return false)
        for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
            if( ihit->targetGUID == targetguid )
                return true;
    }
    return false;                                           //target invalid
}

uint8 Spell::CheckRange(bool strict)
{

    float range_mod;

    // self cast doesn't need range checking -- also for Starshards fix
    if (m_spellInfo->rangeIndex == 1) return 0;

    if (strict)                                             //add radius of caster
        range_mod = 1.25;
    else                                                    //add radius of caster and ~5 yds "give"
        range_mod = 6.25;

    SpellRangeEntry const* srange = sSpellRangeStore.LookupEntry(m_spellInfo->rangeIndex);
    float max_range = GetSpellMaxRange(srange) + range_mod;
    float min_range = GetSpellMinRange(srange);

    if(Player* modOwner = m_caster->GetSpellModOwner())
        modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_RANGE, max_range, this);

    Unit *target = m_targets.getUnitTarget();

    if(target && target != m_caster)
    {
        float dist = m_caster->GetDistanceSq(target);
        if(dist > max_range * max_range)
            return SPELL_FAILED_OUT_OF_RANGE;               //0x5A;
        if(dist < min_range * min_range)
            return SPELL_FAILED_TOO_CLOSE;
        if( !m_IsTriggeredSpell && !m_caster->isInFront( target, max_range) )
        {
            if (m_rangedShoot)
                return SPELL_FAILED_UNIT_NOT_INFRONT;

            uint32 original_casttime = GetSpellCastTime(sCastTimesStore.LookupEntry(m_spellInfo->CastingTimeIndex));

            if (original_casttime!= 0 && !IsPositiveSpell(m_spellInfo->Id) && !IsSingleTargetSpell(m_spellInfo))
                return SPELL_FAILED_UNIT_NOT_INFRONT;
        }
    }

    if(m_targets.m_targetMask == TARGET_FLAG_DEST_LOCATION && m_targets.m_destX != 0 && m_targets.m_destY != 0 && m_targets.m_destZ != 0)
    {
        float dist = m_caster->GetDistanceSq(m_targets.m_destX, m_targets.m_destY, m_targets.m_destZ);
        if(dist > max_range * max_range)
            return SPELL_FAILED_OUT_OF_RANGE;
        if(dist < min_range * min_range)
            return SPELL_FAILED_TOO_CLOSE;
    }

    return 0;                                               // ok
}

uint8 Spell::CheckMana(uint32 *mana)
{
    // item cast not used power
    if(m_CastItem)
        return 0;

    // health as power used
    if(m_spellInfo->powerType == -2)
    {
        uint32 currentHealth = m_caster->GetHealth();

        uint32 healthCost;

        healthCost = m_spellInfo->manaCost + int32(float(m_spellInfo->ManaCostPercentage)/100.0f * m_caster->GetCreateHealth());

        *mana = healthCost;
        if(currentHealth <= healthCost)
            return SPELL_FAILED_CASTER_AURASTATE;

        return 0;
    }

    if(m_spellInfo->powerType <0 || m_spellInfo->powerType > POWER_HAPPINESS)
    {
        sLog.outError("Spell::CheckMana: Unknown power type '%d'", m_spellInfo->powerType);
        return SPELL_FAILED_UNKNOWN;
    }

    Powers powerType = Powers(m_spellInfo->powerType);

    uint32 currentPower = m_caster->GetPower(powerType);
    float manaCost = m_spellInfo->manaCost;
    if(m_spellInfo->manaCostPerlevel)
        manaCost += m_spellInfo->manaCostPerlevel*m_caster->getLevel();
    if(m_spellInfo->ManaCostPercentage)
    {
        if(powerType==POWER_MANA)
            manaCost += float(m_spellInfo->ManaCostPercentage)/100.0f * m_caster->GetCreateMana();
        else
            manaCost += float(m_spellInfo->ManaCostPercentage)/100.0f * m_caster->GetMaxPower(powerType);
    }

    SpellSchools school = GetFirstSchoolInMask(GetSpellSchoolMask(m_spellInfo));

    manaCost += m_caster->GetInt32Value(UNIT_FIELD_POWER_COST_MODIFIER+school);

    // Shiv - costs 20 + weaponSpeed*10 energy (apply only to non-triggered spell with energy cost)
    if ( m_spellInfo->manaCost > 0 && m_spellInfo->SpellFamilyName == SPELLFAMILY_ROGUE && m_spellInfo->SpellIconID == 1834 )
    {
        manaCost += m_caster->GetAttackTime(OFF_ATTACK)/100;
    }

    if(Player* modOwner = m_caster->GetSpellModOwner())
        modOwner->ApplySpellMod(m_spellInfo->Id, SPELLMOD_COST, manaCost, this);

    manaCost*= (1.0f+m_caster->GetFloatValue(UNIT_FIELD_POWER_COST_MULTIPLIER+school));
    if (manaCost < 0)
        manaCost = 0;

    *mana = uint32(manaCost);

    if(currentPower < *mana)
        return SPELL_FAILED_NO_POWER;
    else
        return 0;
}

uint8 Spell::CheckItems()
{
    if (m_caster->GetTypeId() != TYPEID_PLAYER)
        return 0;

    uint32 itemid, itemcount;
    Player* p_caster = (Player*)m_caster;

    if(m_CastItem)
    {
        itemid = m_CastItem->GetEntry();
        if( !p_caster->HasItemCount(itemid,1) )
            return SPELL_FAILED_ITEM_NOT_READY;
        else
        {
            ItemPrototype const *proto = m_CastItem->GetProto();
            if(!proto)
                return SPELL_FAILED_ITEM_NOT_READY;

            for (int i = 0; i<5; i++)
            {
                if (proto->Spells[i].SpellCharges)
                {
                    if(m_CastItem->GetSpellCharges(i)==0)
                        return SPELL_FAILED_NO_CHARGES_REMAIN;
                }
            }

            uint32 ItemClass = proto->Class;
            if (ItemClass == ITEM_CLASS_CONSUMABLE && m_targets.getUnitTarget())
            {
                for (int i = 0; i < 3; i++)
                {
                    if (m_spellInfo->Effect[i] == SPELL_EFFECT_HEAL)
                        if (m_targets.getUnitTarget()->GetHealth() == m_targets.getUnitTarget()->GetMaxHealth())
                            return (uint8)SPELL_FAILED_ALREADY_AT_FULL_HEALTH;

                    // Mana Potion, Rage Potion, Thistle Tea(Rogue), ...
                    if (m_spellInfo->Effect[i] == SPELL_EFFECT_ENERGIZE)
                    {
                        if(m_spellInfo->EffectMiscValue[i] < 0 || m_spellInfo->EffectMiscValue[i] >= MAX_POWERS)
                            return (uint8)SPELL_FAILED_ALREADY_AT_FULL_POWER;

                        Powers power = Powers(m_spellInfo->EffectMiscValue[i]);

                        if (m_targets.getUnitTarget()->GetPower(power) == m_targets.getUnitTarget()->GetMaxPower(power))
                            return (uint8)SPELL_FAILED_ALREADY_AT_FULL_POWER;
                    }
                }
            }
        }
    }

    if(m_targets.getItemTargetGUID())
    {
        if(m_caster->GetTypeId() != TYPEID_PLAYER)
            return SPELL_FAILED_BAD_TARGETS;

        if(!m_targets.getItemTarget())
            return SPELL_FAILED_ITEM_GONE;

        if(!m_targets.getItemTarget()->IsFitToSpellRequirements(m_spellInfo))
            return SPELL_FAILED_EQUIPPED_ITEM_CLASS;
    }
    // if not item target then required item must be equipped
    else
    {
        if(m_caster->GetTypeId() == TYPEID_PLAYER && !((Player*)m_caster)->HasItemFitToSpellReqirements(m_spellInfo))
            return SPELL_FAILED_EQUIPPED_ITEM_CLASS;
    }

    if(m_spellInfo->RequiresSpellFocus)
    {
        CellPair p(MaNGOS::ComputeCellPair(m_caster->GetPositionX(), m_caster->GetPositionY()));
        Cell cell(p);
        cell.data.Part.reserved = ALL_DISTRICT;

        GameObject* ok = NULL;
        MaNGOS::GameObjectFocusCheck go_check(m_caster,m_spellInfo->RequiresSpellFocus);
        MaNGOS::GameObjectSearcher<MaNGOS::GameObjectFocusCheck> checker(ok,go_check);

        TypeContainerVisitor<MaNGOS::GameObjectSearcher<MaNGOS::GameObjectFocusCheck>, GridTypeMapContainer > object_checker(checker);
        CellLock<GridReadGuard> cell_lock(cell, p);
        cell_lock->Visit(cell_lock, object_checker, *MapManager::Instance().GetMap(m_caster->GetMapId(), m_caster));

        if(!ok)
            return (uint8)SPELL_FAILED_REQUIRES_SPELL_FOCUS;

        focusObject = ok;                                   // game object found in range
    }

    for(uint32 i=0;i<8;i++)
    {
        if(m_spellInfo->Reagent[i] <= 0)
            continue;

        itemid    = m_spellInfo->Reagent[i];
        itemcount = m_spellInfo->ReagentCount[i];

        // if CastItem is also spell reagent
        if( m_CastItem && m_CastItem->GetEntry() == itemid )
        {
            ItemPrototype const *proto = m_CastItem->GetProto();
            if(!proto)
                return SPELL_FAILED_ITEM_NOT_READY;
            for(int s=0;s<5;s++)
            {
                // CastItem will be used up and does not count as reagent
                int32 charges = m_CastItem->GetSpellCharges(s);
                if (proto->Spells[s].SpellCharges < 0 && abs(charges) < 2)
                {
                    ++itemcount;
                    break;
                }
            }
        }
        if( !p_caster->HasItemCount(itemid,itemcount) )
            return (uint8)SPELL_FAILED_ITEM_NOT_READY;      //0x54
    }

    uint32 totems = 2;
    for(int i=0;i<2;++i)
    {
        if(m_spellInfo->Totem[i] != 0)
        {
            if( p_caster->HasItemCount(m_spellInfo->Totem[i],1) )
            {
                totems -= 1;
                continue;
            }
        }else
        totems -= 1;
    }
    if(totems != 0)
        return (uint8)SPELL_FAILED_TOTEMS;                  //0x7C

    //Check items for TotemCategory
    uint32 TotemCategory = 2;
    for(int i=0;i<2;++i)
    {
        if(m_spellInfo->TotemCategory[i] != 0)
        {
            if( p_caster->HasItemTotemCategory(m_spellInfo->TotemCategory[i]) )
            {
                TotemCategory -= 1;
                continue;
            }
        }
        else
            TotemCategory -= 1;
    }
    if(TotemCategory != 0)
        return (uint8)SPELL_FAILED_TOTEM_CATEGORY;          //0x7B

    for(int i = 0; i < 3; i++)
    {
        switch (m_spellInfo->Effect[i])
        {
            case SPELL_EFFECT_CREATE_ITEM:
            {
                if (m_spellInfo->EffectItemType[i])
                {
                    ItemPosCountVec dest;
                    uint8 msg = p_caster->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, m_spellInfo->EffectItemType[i], 1 );
                    if (msg != EQUIP_ERR_OK )
                    {
                        p_caster->SendEquipError( msg, NULL, NULL );
                                                            // TODO: don't show two errors
                        return uint8(SPELL_FAILED_DONT_REPORT);
                    }
                }
                break;
            }
            case SPELL_EFFECT_ENCHANT_ITEM:
            {
                Item* targetItem = m_targets.getItemTarget();
                if(!targetItem)
                    return SPELL_FAILED_ITEM_NOT_FOUND;

                if( targetItem->GetProto()->ItemLevel < m_spellInfo->baseLevel )
                    return SPELL_FAILED_LOWLEVEL;

                break;
            }
            case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
            {
                Item *item = m_targets.getItemTarget();
                if(!item)
                    return SPELL_FAILED_ITEM_NOT_FOUND;
                // not allow apply temporary enchantments in trade slot
                if( item->GetOwner() != m_caster )
                    return SPELL_FAILED_ITEM_NOT_FOUND;
                break;
            }
            case SPELL_EFFECT_ENCHANT_HELD_ITEM:
                // check item existence in effect code (not output errors at offhand hold item effect to main hand for example
                break;
            case SPELL_EFFECT_DISENCHANT:
            {
                if(!m_targets.getItemTarget())
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                // prevent disenchanting in trade slot
                if( m_targets.getItemTarget()->GetOwnerGUID() != m_caster->GetGUID() )
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                ItemPrototype const* itemProto = m_targets.getItemTarget()->GetProto();
                if(!itemProto)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;

                uint32 item_quality = itemProto->Quality;
                // 2.0.x addon: Check player enchanting level agains the item desenchanting requirements
                uint32 item_disenchantskilllevel = itemProto->RequiredDisenchantSkill;
                if (item_disenchantskilllevel == uint32(-1))
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                if (item_disenchantskilllevel > p_caster->GetSkillValue(SKILL_ENCHANTING))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                if(item_quality > 4 || item_quality < 2)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                if(itemProto->Class != ITEM_CLASS_WEAPON && itemProto->Class != ITEM_CLASS_ARMOR)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                if (!itemProto->DisenchantID)
                    return SPELL_FAILED_CANT_BE_DISENCHANTED;
                break;
            }
            case SPELL_EFFECT_PROSPECTING:
            {
                if(!m_targets.getItemTarget())
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                //ensure item is a prospectable ore
                if(!(m_targets.getItemTarget()->GetProto()->BagFamily & BAG_FAMILY_MASK_MINING_SUPP) || m_targets.getItemTarget()->GetProto()->Class != ITEM_CLASS_TRADE_GOODS)
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                //prevent prospecting in trade slot
                if( m_targets.getItemTarget()->GetOwnerGUID() != m_caster->GetGUID() )
                    return SPELL_FAILED_CANT_BE_PROSPECTED;
                //Check for enough skill in jewelcrafting
                uint32 item_prospectingskilllevel = m_targets.getItemTarget()->GetProto()->RequiredSkillRank;
                if(item_prospectingskilllevel >p_caster->GetSkillValue(SKILL_JEWELCRAFTING))
                    return SPELL_FAILED_LOW_CASTLEVEL;
                //make sure the player has the required ores in inventory
                if(m_targets.getItemTarget()->GetCount() < 5)
                    return SPELL_FAILED_PROSPECT_NEED_MORE;

                if(!LootTemplates_Prospecting.HaveLootFor(m_targets.getItemTargetEntry()))
                    return SPELL_FAILED_CANT_BE_PROSPECTED;

                break;
            }
            case SPELL_EFFECT_WEAPON_DAMAGE:
            case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
            {
                if(m_caster->GetTypeId() != TYPEID_PLAYER) return SPELL_FAILED_TARGET_NOT_PLAYER;
                if(m_spellInfo->rangeIndex == 1 || m_spellInfo->rangeIndex == 2 || m_spellInfo->rangeIndex == 7)
                    break;
                Item *pItem = ((Player*)m_caster)->GetItemByPos( INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_RANGED );
                if(!pItem || pItem->IsBroken() || pItem->GetProto()->Class != ITEM_CLASS_WEAPON )
                    return SPELL_FAILED_EQUIPPED_ITEM;

                switch(pItem->GetProto()->SubClass)
                {
                    case ITEM_SUBCLASS_WEAPON_THROWN:
                    {
                        uint32 ammo = pItem->GetEntry();
                        if( !((Player*)m_caster)->HasItemCount( ammo, 1 ) )
                            return SPELL_FAILED_NO_AMMO;
                    };  break;
                    case ITEM_SUBCLASS_WEAPON_GUN:
                    case ITEM_SUBCLASS_WEAPON_BOW:
                    case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                    {
                        uint32 ammo = ((Player*)m_caster)->GetUInt32Value(PLAYER_AMMO_ID);
                        if(!ammo)
                            return SPELL_FAILED_NO_AMMO;

                        ItemPrototype const *ammoProto = objmgr.GetItemPrototype( ammo );
                        if(!ammoProto)
                            return SPELL_FAILED_NO_AMMO;

                        if(ammoProto->Class != ITEM_CLASS_PROJECTILE)
                            return SPELL_FAILED_NO_AMMO;

                        // check ammo ws. weapon compatibility
                        switch(pItem->GetProto()->SubClass)
                        {
                            case ITEM_SUBCLASS_WEAPON_BOW:
                            case ITEM_SUBCLASS_WEAPON_CROSSBOW:
                                if(ammoProto->SubClass!=ITEM_SUBCLASS_ARROW)
                                    return SPELL_FAILED_NO_AMMO;
                                break;
                            case ITEM_SUBCLASS_WEAPON_GUN:
                                if(ammoProto->SubClass!=ITEM_SUBCLASS_BULLET)
                                    return SPELL_FAILED_NO_AMMO;
                                break;
                            default:
                                return SPELL_FAILED_NO_AMMO;
                        }

                        if( !((Player*)m_caster)->HasItemCount( ammo, 1 ) )
                            return SPELL_FAILED_NO_AMMO;
                    };  break;
                    case ITEM_SUBCLASS_WEAPON_WAND:
                    default:
                        break;
                }
                break;
            }
            default:break;
        }
    }

    return uint8(0);
}

void Spell::Delayed(int32 delaytime)
{
    if(!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER)
        return;

    if (m_spellState == SPELL_STATE_DELAYED)
        return;                                             // spell is active and can't be time-backed

    // spells not loosing casting time ( slam, dynamites, bombs.. )
    if(!(m_spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_DAMAGE))
        return;

    //check resist chance
    int32 resistChance = 100;                               //must be initialized to 100 for percent modifiers
    ((Player*)m_caster)->ApplySpellMod(m_spellInfo->Id,SPELLMOD_NOT_LOSE_CASTING_TIME,resistChance, this);
    resistChance += m_caster->GetTotalAuraModifier(SPELL_AURA_RESIST_PUSHBACK) - 100;
    if (roll_chance_f(resistChance))
        return;

    m_timer += delaytime;

    if(int32(m_timer) > m_casttime)
        ReSetTimer();

    WorldPacket data(SMSG_SPELL_DELAYED, 8+4);
    data.append(m_caster->GetPackGUID());
    data << uint32(delaytime);

    ((Player*)m_caster)->GetSession()->SendPacket(&data);
}

void Spell::DelayedChannel(int32 delaytime)
{
    if(!m_caster || m_caster->GetTypeId() != TYPEID_PLAYER || getState() != SPELL_STATE_CASTING)
        return;

    //check resist chance
    int32 resistChance = 100;                               //must be initialized to 100 for percent modifiers
    ((Player*)m_caster)->ApplySpellMod(m_spellInfo->Id,SPELLMOD_NOT_LOSE_CASTING_TIME,resistChance, this);
    resistChance += m_caster->GetTotalAuraModifier(SPELL_AURA_RESIST_PUSHBACK) - 100;
    if (roll_chance_f(resistChance))
        return;

    int32 appliedDelayTime = delaytime;

    if(int32(m_timer) < delaytime)
    {
        appliedDelayTime = m_timer;
        m_timer = 0;
    } else
    m_timer -= delaytime;

    sLog.outDebug("Spell %u partially interrupted for %i ms, new duration: %u ms", m_spellInfo->Id, appliedDelayTime, m_timer);

    for(std::list<TargetInfo>::iterator ihit= m_UniqueTargetInfo.begin();ihit != m_UniqueTargetInfo.end();++ihit)
    {
        if ((*ihit).missCondition == SPELL_MISS_NONE)
        {
            Unit* unit = m_caster->GetGUID()==ihit->targetGUID ? m_caster : ObjectAccessor::GetUnit(*m_caster, ihit->targetGUID);
            if (unit)
            {
                for (int j=0;j<3;j++)
                    if( ihit->effectMask & (1<<j) )
                        unit->DelayAura(m_spellInfo->Id, j, appliedDelayTime);
            }

        }
    }

    for(int j = 0; j < 3; j++)
    {
        // partially interrupt persistent area auras
        DynamicObject* dynObj = m_caster->GetDynObject(m_spellInfo->Id, j);
        if(dynObj)
            dynObj->Delay(appliedDelayTime);
    }

    SendChannelUpdate(m_timer);
}

void Spell::UpdatePointers()
{
    if(m_originalCasterGUID==m_caster->GetGUID())
        m_originalCaster = m_caster;
    else
        m_originalCaster = ObjectAccessor::GetUnit(*m_caster,m_originalCasterGUID);

    m_targets.Update(m_caster);
}

bool Spell::IsAffectedBy(SpellEntry const *spellInfo, uint32 effectId)
{
    return spellmgr.IsAffectedBySpell(m_spellInfo,spellInfo->Id,effectId,spellInfo->EffectItemType[effectId]);
}

bool Spell::CheckTargetCreatureType(Unit* target) const
{
    uint32 spellCreatureTargetMask = m_spellInfo->TargetCreatureType;

    // Curse of Doom : not find another way to fix spell target check :/
    if(m_spellInfo->SpellFamilyName==SPELLFAMILY_WARLOCK && m_spellInfo->SpellFamilyFlags == 0x0200000000LL)
    {
        // not allow cast at player
        if(target->GetTypeId()==TYPEID_PLAYER)
            return false;

        spellCreatureTargetMask = 0x7FF;
    }

    // Dismiss Pet and Taming Lesson skipped
    if(m_spellInfo->Id == 2641 || m_spellInfo->Id == 23356)
        spellCreatureTargetMask =  0;

    if (spellCreatureTargetMask)
    {
        uint32 TargetCreatureType = target->GetCreatureTypeMask();

        return !TargetCreatureType || (spellCreatureTargetMask & TargetCreatureType);
    }
    return true;
}

CurrentSpellTypes Spell::GetCurrentContainer()
{
    if (IsMeleeSpell())
        return(CURRENT_MELEE_SPELL);
    else if (IsAutoRepeat())
        return(CURRENT_AUTOREPEAT_SPELL);
    else if (IsChanneledSpell(m_spellInfo))
        return(CURRENT_CHANNELED_SPELL);
    else
        return(CURRENT_GENERIC_SPELL);
}

bool Spell::CheckTarget( Unit* target, uint32 eff, bool hitPhase )
{
    // Check targets for creature type mask and remove not appropriate (skip explicit self target case, maybe need other explicit targets)
    if(m_spellInfo->EffectImplicitTargetA[eff]!=TARGET_SELF )
    {
        if (!CheckTargetCreatureType(target))
            return false;
    }

    // Check targets for not_selectable unit flag and remove
    // A player can cast spells on his pet (or other controlled unit) though in any state
    if (target != m_caster && target->GetCharmerOrOwnerGUID() != m_caster->GetGUID())
    {
        // any unattackable target skipped
        if (target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE))
            return false;

        // unselectable targets skipped in all cases except TARGET_SCRIPT targeting
        // in case TARGET_SCRIPT target selected by server always and can't be cheated
        if( target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE) &&
            m_spellInfo->EffectImplicitTargetA[eff] != TARGET_SCRIPT &&
            m_spellInfo->EffectImplicitTargetB[eff] != TARGET_SCRIPT )
            return false;
    }

    //Check player targets and remove if in GM mode or GM invisibility (for not self casting case)
    if( target != m_caster && target->GetTypeId()==TYPEID_PLAYER)
    {
        if(((Player*)target)->GetVisibility()==VISIBILITY_OFF)
            return false;

        if(((Player*)target)->isGameMaster() && !IsPositiveSpell(m_spellInfo->Id))
            return false;
    }

    //Check targets for LOS visibility (except spells without range limitations )
    switch(m_spellInfo->Effect[eff])
    {
        case SPELL_EFFECT_SUMMON_PLAYER:                    // from anywhere
            break;
        case SPELL_EFFECT_DUMMY:
            if(m_spellInfo->Id!=20577)                      // Cannibalize
                break;
            //fall through
        case SPELL_EFFECT_RESURRECT_NEW:
            if(!target->IsWithinLOSInMap(m_caster))         // player far away, maybe his corpse near?
            {
                if(!m_targets.getCorpseTargetGUID())
                    return false;

                Corpse *corpse = ObjectAccessor::GetCorpse(*m_caster,m_targets.getCorpseTargetGUID());
                if(!corpse)
                    return false;

                if(target->GetGUID()!=corpse->GetOwnerGUID())
                    return false;

                if(!corpse->IsWithinLOSInMap(m_caster))
                    return false;
            }

            // all ok by some way or another, skip normal check
            break;
        default:                                            // normal case
            if(!target->IsWithinLOSInMap(m_caster))
                return false;
            break;
    }

    return true;
}

Unit* Spell::SelectMagnetTarget()
{
    Unit* target = m_targets.getUnitTarget();

    if(target && target->HasAuraType(SPELL_AURA_SPELL_MAGNET) && !(m_spellInfo->Attributes & 0x10))
    {
        Aura* magnet = target->GetAurasByType(SPELL_AURA_SPELL_MAGNET).front();
        if(Unit* caster = magnet->GetCaster())
        {
            target = caster;
            m_targets.setUnitTarget(target);
        }
    }

    return target;
}

bool Spell::IsNeedSendToClient() const
{
    return m_spellInfo->SpellVisual!=0 || IsChanneledSpell(m_spellInfo) || 
        m_spellInfo->speed > 0.0f || !m_triggeredByAuraSpell && !m_IsTriggeredSpell;
}

bool Spell::HaveTargetsForEffect( uint8 effect ) const
{
    for(std::list<TargetInfo>::const_iterator itr= m_UniqueTargetInfo.begin();itr != m_UniqueTargetInfo.end();++itr)
        if(itr->effectMask & (1<<effect))
            return true;

    for(std::list<GOTargetInfo>::const_iterator itr= m_UniqueGOTargetInfo.begin();itr != m_UniqueGOTargetInfo.end();++itr)
        if(itr->effectMask & (1<<effect))
            return true;

    for(std::list<ItemTargetInfo>::const_iterator itr= m_UniqueItemInfo.begin();itr != m_UniqueItemInfo.end();++itr)
        if(itr->effectMask & (1<<effect))
            return true;

    return false;
}

SpellEvent::SpellEvent(Spell* spell) : BasicEvent()
{
    m_Spell = spell;
}

SpellEvent::~SpellEvent()
{
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->cancel();

    if (m_Spell->IsDeletable())
    {
        delete m_Spell;
    }
    else
    {
        sLog.outError("~SpellEvent: %s %u tried to delete non-deletable spell %u. Was not deleted, causes memory leak.",
            (m_Spell->GetCaster()->GetTypeId()==TYPEID_PLAYER?"Player":"Creature"), m_Spell->GetCaster()->GetGUIDLow(),m_Spell->m_spellInfo->Id);
    }
}

bool SpellEvent::Execute(uint64 e_time, uint32 p_time)
{
    // update spell if it is not finished
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->update(p_time);

    // check spell state to process
    switch (m_Spell->getState())
    {
        case SPELL_STATE_FINISHED:
        {
            // spell was finished, check deletable state
            if (m_Spell->IsDeletable())
            {
                // check, if we do have unfinished triggered spells

                return(true);                               // spell is deletable, finish event
            }
            // event will be re-added automatically at the end of routine)
        } break;

        case SPELL_STATE_CASTING:
        {
            // this spell is in channeled state, process it on the next update
            // event will be re-added automatically at the end of routine)
        } break;

        case SPELL_STATE_DELAYED:
        {
            // first, check, if we have just started
            if (m_Spell->GetDelayStart() != 0)
            {
                // no, we aren't, do the typical update
                // check, if we have channeled spell on our hands
                if (IsChanneledSpell(m_Spell->m_spellInfo))
                {
                    // evented channeled spell is processed separately, casted once after delay, and not destroyed till finish
                    // check, if we have casting anything else except this channeled spell and autorepeat
                    if (m_Spell->GetCaster()->IsNonMeleeSpellCasted(false, true, true))
                    {
                        // another non-melee non-delayed spell is casted now, abort
                        m_Spell->cancel();
                    }
                    else
                    {
                        // do the action (pass spell to channeling state)
                        m_Spell->handle_immediate();
                    }
                    // event will be re-added automatically at the end of routine)
                }
                else
                {
                    // run the spell handler and think about what we can do next
                    uint64 t_offset = e_time - m_Spell->GetDelayStart();
                    uint64 n_offset = m_Spell->handle_delayed(t_offset);
                    if (n_offset)
                    {
                        // re-add us to the queue
                        m_Spell->GetCaster()->m_Events.AddEvent(this, m_Spell->GetDelayStart() + n_offset, false);
                        return(false);                      // event not complete
                    }
                    // event complete
                    // finish update event will be re-added automatically at the end of routine)
                }
            }
            else
            {
                // delaying had just started, record the moment
                m_Spell->SetDelayStart(e_time);
                // re-plan the event for the delay moment
                m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + m_Spell->GetDelayMoment(), false);
                return(false);                              // event not complete
            }
        } break;

        default:
        {
            // all other states
            // event will be re-added automatically at the end of routine)
        } break;
    }

    // spell processing not complete, plan event on the next update interval
    m_Spell->GetCaster()->m_Events.AddEvent(this, e_time + 1, false);
    return(false);                                          // event not complete
}

void SpellEvent::Abort(uint64 /*e_time*/)
{
    // oops, the spell we try to do is aborted
    if (m_Spell->getState() != SPELL_STATE_FINISHED)
        m_Spell->cancel();
}
