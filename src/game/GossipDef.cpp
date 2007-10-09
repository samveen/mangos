/* 
 * Copyright (C) 2005,2006,2007 MaNGOS <http://www.mangosproject.org/>
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

#include "QuestDef.h"
#include "GossipDef.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Util.h"

GossipMenu::GossipMenu()
{
    m_gItems.reserve(16);                                   // can be set for max from most often sizes to speedup push_back and less memory use
}

GossipMenu::~GossipMenu()
{
    ClearMenu();
}

void GossipMenu::AddMenuItem(uint8 Icon, std::string Message, uint32 dtSender, uint32 dtAction, bool Coded)
{
    ASSERT( m_gItems.size() <= GOSSIP_MAX_MENU_ITEMS  );

    GossipMenuItem gItem;

    gItem.m_gIcon     = Icon;
    gItem.m_gMessage  = Message;
    gItem.m_gCoded    = Coded;
    gItem.m_gSender   = dtSender;
    gItem.m_gAction   = dtAction;

    m_gItems.push_back(gItem);
}

void GossipMenu::AddMenuItem(uint8 Icon, std::string Message, bool Coded)
{
    AddMenuItem( Icon, Message, 0, 0, Coded);
}

uint32 GossipMenu::MenuItemSender( unsigned int ItemId )
{
    if ( ItemId >= m_gItems.size() ) return 0;

    return m_gItems[ ItemId ].m_gSender;
}

uint32 GossipMenu::MenuItemAction( unsigned int ItemId )
{
    if ( ItemId >= m_gItems.size() ) return 0;

    return m_gItems[ ItemId ].m_gAction;
}

void GossipMenu::ClearMenu()
{
    m_gItems.clear();
}

PlayerMenu::PlayerMenu( WorldSession *Session )
{
    pGossipMenu = new GossipMenu();
    pQuestMenu  = new QuestMenu();
    pSession    = Session;
}

PlayerMenu::~PlayerMenu()
{
    delete pGossipMenu;
    delete pQuestMenu;
}

void PlayerMenu::ClearMenus()
{
    pGossipMenu->ClearMenu();
    pQuestMenu->ClearMenu();
}

uint32 PlayerMenu::GossipOptionSender( unsigned int Selection )
{
    return pGossipMenu->MenuItemSender( Selection + pQuestMenu->MenuItemCount() );
}

uint32 PlayerMenu::GossipOptionAction( unsigned int Selection )
{
    return pGossipMenu->MenuItemAction( Selection + pQuestMenu->MenuItemCount() );
}

void PlayerMenu::SendGossipMenu( uint32 TitleTextId, uint64 npcGUID )
{
    WorldPacket data( SMSG_GOSSIP_MESSAGE, (100) );         // guess size
    data << npcGUID;
    data << uint32( TitleTextId );
    data << uint32( pGossipMenu->MenuItemCount() );

    for ( unsigned int iI = 0; iI < pGossipMenu->MenuItemCount(); iI++ )
    {
        data << uint32( iI );
        data << uint8( pGossipMenu->GetItem(iI).m_gIcon );
        // icons:
        // 0 unlearn talents/misc
        // 1 trader
        // 2 taxi
        // 3 trainer
        // 9 BG/arena
        data << uint8( pGossipMenu->GetItem(iI).m_gCoded );
        data << uint32(0);                                  // req money to open menu, 2.0.3
        data << pGossipMenu->GetItem(iI).m_gMessage;
        data << uint8(0);                                   // unknown, 2.0.3
    }

    data << uint32( pQuestMenu->MenuItemCount() );

    for ( uint16 iI = 0; iI < pQuestMenu->MenuItemCount(); iI++ )
    {
        uint32 questID = pQuestMenu->GetItem(iI).m_qId;
        Quest const* pQuest = objmgr.GetQuestTemplate(questID);

        data << questID;
        data << uint32( pQuestMenu->GetItem(iI).m_qIcon );
        data << uint32( pQuest ? pQuest->GetQuestLevel() : 0 );
        data << (pQuest ? pQuest->GetTitle() : "");
    }

    pSession->SendPacket( &data );
    //sLog.outDebug( "WORLD: Sent SMSG_GOSSIP_MESSAGE NPCGuid=%u",GUID_LOPART(npcGUID) );
}

void PlayerMenu::CloseGossip()
{
    WorldPacket data( SMSG_GOSSIP_COMPLETE, 0 );
    pSession->SendPacket( &data );

    //sLog.outDebug( "WORLD: Sent SMSG_GOSSIP_COMPLETE" );
}

void PlayerMenu::SendPointOfInterest( float X, float Y, uint32 Icon, uint32 Flags, uint32 Data, char const * locName )
{
    WorldPacket data( SMSG_GOSSIP_POI, (4+4+4+4+4+10) );    // guess size
    data << Flags;
    data << X << Y;
    data << uint32(Icon);
    data << uint32(Data);
    data << locName;

    pSession->SendPacket( &data );
    //sLog.outDebug("WORLD: Sent SMSG_GOSSIP_POI");
}

void PlayerMenu::SendTalking( uint32 textID )
{
    GossipText *pGossip;
    std::string GossipStr;

    pGossip = objmgr.GetGossipText(textID);

    WorldPacket data( SMSG_NPC_TEXT_UPDATE, 100 );          // guess size
    data << textID;

    if (!pGossip)
    {
        data << uint32( 0 );
        data << "Greetings $N";
        data << "Greetings $N";
    } else

    for (int i=0; i<8; i++)
    {
        data << pGossip->Options[i].Probability;

        if ( pGossip->Options[i].Text_0 == "" )
            data << pGossip->Options[i].Text_1;
        else
            data << pGossip->Options[i].Text_0;

        if ( pGossip->Options[i].Text_1 == "" )
            data << pGossip->Options[i].Text_0; 
        else
            data << pGossip->Options[i].Text_1;

        data << pGossip->Options[i].Language;

        data << pGossip->Options[i].Emotes[0]._Delay;
        data << pGossip->Options[i].Emotes[0]._Emote;

        data << pGossip->Options[i].Emotes[1]._Delay;
        data << pGossip->Options[i].Emotes[1]._Emote;

        data << pGossip->Options[i].Emotes[2]._Delay;
        data << pGossip->Options[i].Emotes[2]._Emote;
    }

    pSession->SendPacket( &data );

    sLog.outDetail( "WORLD: Sent SMSG_NPC_TEXT_UPDATE " );
}

void PlayerMenu::SendTalking( char const * title, char const * text )
{
    WorldPacket data( SMSG_NPC_TEXT_UPDATE, 50 );           // guess size
    data << uint32( 0 );
    data << uint32( 0 );
    data << title;
    data << text;

    pSession->SendPacket( &data );

    sLog.outDetail( "WORLD: Sent SMSG_NPC_TEXT_UPDATE " );
}

/*********************************************************/
/***                    QUEST SYSTEM                   ***/
/*********************************************************/

QuestMenu::QuestMenu()
{
    m_qItems.reserve(16);                                   // can be set for max from most often sizes to speedup push_back and less memory use
}

QuestMenu::~QuestMenu()
{
    ClearMenu();
}

void QuestMenu::AddMenuItem( uint32 QuestId, uint8 Icon)
{
    Quest const* qinfo = objmgr.GetQuestTemplate(QuestId);
    if (!qinfo) return;

    ASSERT( m_qItems.size() <= GOSSIP_MAX_MENU_ITEMS  );

    QuestMenuItem qItem;

    qItem.m_qId        = QuestId;
    qItem.m_qIcon      = Icon;

    m_qItems.push_back(qItem);
}

bool QuestMenu::HasItem( uint32 questid )
{
    for (QuestMenuItemList::iterator i = m_qItems.begin(); i != m_qItems.end(); i++)
    {
        if(i->m_qId==questid)
        {
            return true;
        }
    }
    return false;
}

void QuestMenu::ClearMenu()
{
    m_qItems.clear();
}

void PlayerMenu::SendQuestGiverQuestList( QEmote eEmote, std::string Title, uint64 npcGUID )
{
    WorldPacket data( SMSG_QUESTGIVER_QUEST_LIST, 100 );    // guess size
    data << uint64(npcGUID);
    data << Title;
    data << uint32(eEmote._Delay );                         // player emote
    data << uint32(eEmote._Emote );                         // NPC emote
    data << uint8 ( pQuestMenu->MenuItemCount() );

    for ( uint16 iI = 0; iI < pQuestMenu->MenuItemCount(); iI++ )
    {
        QuestMenuItem qmi=pQuestMenu->GetItem(iI);
        uint32 questID = qmi.m_qId;
        Quest const *pQuest = objmgr.GetQuestTemplate(questID);

        data << questID;
        data << uint32( qmi.m_qIcon );
        data << uint32( pQuest ? pQuest->GetQuestLevel() : 0 );
        data << ( pQuest ? pQuest->GetTitle() : "" );
    }
    pSession->SendPacket( &data );
    //uint32 fqid=pQuestMenu->GetItem(0).m_qId;
    //sLog.outDebug( "WORLD: Sent SMSG_QUESTGIVER_QUEST_LIST NPC Guid=%u, questid-0=%u",npcGUID,fqid);
}

void PlayerMenu::SendQuestGiverStatus( uint32 questStatus, uint64 npcGUID )
{
    WorldPacket data( SMSG_QUESTGIVER_STATUS, 12 );
    data << npcGUID << questStatus;

    pSession->SendPacket( &data );
    //sLog.outDebug( "WORLD: Sent SMSG_QUESTGIVER_STATUS NPC Guid=%u, status=%u",GUID_LOPART(npcGUID),questStatus);
}

void PlayerMenu::SendQuestGiverQuestDetails( Quest const *pQuest, uint64 npcGUID, bool ActivateAccept )
{
    WorldPacket data(SMSG_QUESTGIVER_QUEST_DETAILS, 100);   // guess size

    data << npcGUID;
    data << pQuest->GetQuestId() << pQuest->GetTitle() << pQuest->GetDetails();
    data << pQuest->GetObjectives();
    data << (uint32)ActivateAccept;
    data << pQuest->GetSuggestedPlayers();

    ItemPrototype const* IProto;

    data << pQuest->GetRewChoiceItemsCount();
    for (uint32 i=0; i < QUEST_REWARD_CHOICES_COUNT; i++)
    {
        if ( !pQuest->RewChoiceItemId[i] ) continue;
        data << uint32(pQuest->RewChoiceItemId[i]);
        data << uint32(pQuest->RewChoiceItemCount[i]);
        IProto = objmgr.GetItemPrototype(pQuest->RewChoiceItemId[i]);
        if ( IProto )
            data << uint32(IProto->DisplayInfoID);
        else
            data << uint32( 0x00 );
    }

    data << pQuest->GetRewItemsCount();
    for (uint32 i=0; i < QUEST_REWARDS_COUNT; i++)
    {
        if ( !pQuest->RewItemId[i] ) continue;
        data << pQuest->RewItemId[i];
        data << pQuest->RewItemCount[i];
        IProto = objmgr.GetItemPrototype(pQuest->RewItemId[i]);
        if ( IProto )
            data << IProto->DisplayInfoID;
        else
            data << uint32( 0x00 );
    }

    data << uint32(pQuest->GetRewOrReqMoney());

    // check if RewSpell is teaching another spell
    if(pQuest->GetRewSpell())
    {
        SpellEntry const *rewspell = sSpellStore.LookupEntry(pQuest->GetRewSpell());
        if(rewspell)
        {
            if(rewspell->Effect[0] == SPELL_EFFECT_LEARN_SPELL)
                data << uint32(rewspell->EffectTriggerSpell[0]);
            else
                data << uint32(0);
        }
        else
        {
            sLog.outErrorDb("Quest %u have non-existed RewSpell %u, ignored.",pQuest->GetQuestId(),pQuest->GetRewSpell());
            data << uint32(0);
        }
    }
    else
        data << uint32(0);                                  // reward spell

    data << uint32(pQuest->GetRewSpell());                  // cast spell

    data << uint32(QUEST_EMOTE_COUNT);
    for (uint32 i=0; i < QUEST_EMOTE_COUNT; i++)
    {
        data << pQuest->DetailsEmote[i];
        data << uint32(0);                                  // DetailsEmoteDelay
    }
    pSession->SendPacket( &data );

    //sLog.outDebug("WORLD: Sent SMSG_QUESTGIVER_QUEST_DETAILS NPCGuid=%u, questid=%u",GUID_LOPART(npcGUID),pQuest->GetQuestId());
}

void PlayerMenu::SendQuestQueryResponse( Quest const *pQuest )
{
    WorldPacket data( SMSG_QUEST_QUERY_RESPONSE, 100 );     // guess size

    data << uint32(pQuest->GetQuestId());
    data << uint32(pQuest->GetMinLevel());                  // it's not min lvl in 2.0.1+ (0...2 on official)
    data << uint32(pQuest->GetQuestLevel());
    data << uint32(pQuest->GetZoneOrSort());

    data << uint32(pQuest->GetType());
    data << uint32(pQuest->GetSuggestedPlayers());
    data << uint32(pQuest->GetRequiredMinRepFaction());
    data << uint32(pQuest->GetRequiredMinRepValue());
    data << uint32(pQuest->GetRequiredMaxRepFaction());
    data << uint32(pQuest->GetRequiredMaxRepValue());

    data << uint32(pQuest->GetNextQuestInChain());
    data << uint32(pQuest->GetRewOrReqMoney());
    data << uint32(pQuest->GetRewXpOrMoney());

    // check if RewSpell is teaching another spell
    if(pQuest->GetRewSpell())
    {
        SpellEntry const *rewspell = sSpellStore.LookupEntry(pQuest->GetRewSpell());
        if(rewspell)
        {
            if(rewspell->Effect[0] == SPELL_EFFECT_LEARN_SPELL)
                data << uint32(rewspell->EffectTriggerSpell[0]);
            else
                data << uint32(0);
        }
        else
        {
            sLog.outErrorDb("Quest %u have non-existed RewSpell %u, ignored.",pQuest->GetQuestId(),pQuest->GetRewSpell());
            data << uint32(0);
        }
    }
    else
        data << uint32(0);

    data << uint32(pQuest->GetRewSpell());                  // spellid, The following spell will be casted on you spell_name
    data << uint32(pQuest->GetSrcItemId());
    data << uint32(pQuest->GetSpecialFlags());

    int iI;

    for (iI = 0; iI < QUEST_REWARDS_COUNT; iI++)
    {
        data << uint32(pQuest->RewItemId[iI]);
        data << uint32(pQuest->RewItemCount[iI]);
    }
    for (iI = 0; iI < QUEST_REWARD_CHOICES_COUNT; iI++)
    {
        data << uint32(pQuest->RewChoiceItemId[iI]);
        data << uint32(pQuest->RewChoiceItemCount[iI]);
    }

    data << pQuest->GetPointMapId();
    data << pQuest->GetPointX();
    data << pQuest->GetPointY();
    data << pQuest->GetPointOpt();

    data << pQuest->GetTitle();
    data << pQuest->GetObjectives();
    data << pQuest->GetDetails();
    data << pQuest->GetEndText();

    for (iI = 0; iI < QUEST_OBJECTIVES_COUNT; iI++)
    {
        if (pQuest->ReqCreatureOrGOId[iI] < 0)
        {
            // client expected gameobject template id in form (id|0x80000000)
            data << uint32((pQuest->ReqCreatureOrGOId[iI]*(-1))|0x80000000);
        }
        else
        {
            data << uint32(pQuest->ReqCreatureOrGOId[iI]);
        }
        data << uint32(pQuest->ReqCreatureOrGOCount[iI]);
        data << uint32(pQuest->ReqItemId[iI]);
        data << uint32(pQuest->ReqItemCount[iI]);
    }

    for (iI = 0; iI < QUEST_OBJECTIVES_COUNT; iI++)
        data << pQuest->ObjectiveText[iI];

    pSession->SendPacket( &data );
    //sLog.outDebug( "WORLD: Sent SMSG_QUEST_QUERY_RESPONSE questid=%u",pQuest->GetQuestId() );
}

void PlayerMenu::SendQuestGiverOfferReward( uint32 quest_id, uint64 npcGUID, bool EnbleNext )
{
    Quest const* qInfo = objmgr.GetQuestTemplate(quest_id);
    if(!qInfo)
        return;

    WorldPacket data( SMSG_QUESTGIVER_OFFER_REWARD, 50 );   // guess size

    data << npcGUID;
    data << quest_id;
    data << qInfo->GetTitle();
    data << qInfo->GetOfferRewardText();

    data << uint32( EnbleNext );

    data << uint32(0);                                      // unk

    uint32 EmoteCount = 0;
    for (uint32 i = 0; i < QUEST_EMOTE_COUNT; i++)
    {
        if(qInfo->OfferRewardEmote[i] <= 0)
            break;
        EmoteCount++;
    }

    data << EmoteCount;                                     // Emote Count
    for (uint32 i = 0; i < EmoteCount; i++)
    {
        data << uint32(0);                                  // Delay Emote
        data << qInfo->OfferRewardEmote[i];
    }

    ItemPrototype const *pItem;

    data << uint32(qInfo->GetRewChoiceItemsCount());
    for (uint32 i=0; i < qInfo->GetRewChoiceItemsCount(); i++)
    {
        pItem = objmgr.GetItemPrototype( qInfo->RewChoiceItemId[i] );

        data << uint32(qInfo->RewChoiceItemId[i]);
        data << uint32(qInfo->RewChoiceItemCount[i]);

        if ( pItem )
            data << uint32(pItem->DisplayInfoID);
        else
            data << uint32(0);
    }

    data << uint32(qInfo->GetRewItemsCount());
    for (uint16 i=0; i < qInfo->GetRewItemsCount(); i++)
    {
        pItem = objmgr.GetItemPrototype(qInfo->RewItemId[i]);
        data << uint32(qInfo->RewItemId[i]);
        data << uint32(qInfo->RewItemCount[i]);

        if ( pItem )
            data << uint32(pItem->DisplayInfoID);
        else
            data << uint32(0);
    }

    data << uint32(qInfo->GetRewOrReqMoney());
    data << uint32(0x08);

    // check if RewSpell is teaching another spell
    if(qInfo->GetRewSpell())
    {
        SpellEntry const *rewspell = sSpellStore.LookupEntry(qInfo->GetRewSpell());
        if(rewspell)
        {
            if(rewspell->Effect[0] == SPELL_EFFECT_LEARN_SPELL)
                data << uint32(rewspell->EffectTriggerSpell[0]);
            else
                data << uint32(qInfo->GetRewSpell());
        }
        else
        {
            sLog.outErrorDb("Quest %u have non-existed RewSpell %u, ignored.",qInfo->GetQuestId(),qInfo->GetRewSpell());
            data << uint32(0);
        }
    }
    else
        data << uint32(0);

    data << uint32(0x00);                                   // new 2.0.3

    pSession->SendPacket( &data );
    //sLog.outDebug( "WORLD: Sent SMSG_QUESTGIVER_OFFER_REWARD NPCGuid=%u, questid=%u",GUID_LOPART(npcGUID),quest_id );
}

void PlayerMenu::SendQuestGiverRequestItems( Quest const *pQuest, uint64 npcGUID, bool Completable, bool CloseOnCancel )
{
    // We can always call to RequestItems, but this packet only goes out if there are actually
    // items.  Otherwise, we'll skip straight to the OfferReward

    // We may wish a better check, perhaps checking the real quest requirements
    if (strlen(pQuest->GetRequestItemsText()) == 0)
    {
        SendQuestGiverOfferReward(pQuest->GetQuestId(), npcGUID, true);
        return;
    }

    WorldPacket data( SMSG_QUESTGIVER_REQUEST_ITEMS, 50 );  // guess size
    data << npcGUID;
    data << pQuest->GetQuestId();
    data << pQuest->GetTitle();

    data << pQuest->GetRequestItemsText();

    data << uint32(0x00);                                   // unk

    if(Completable)
        data << pQuest->GetCompleteEmote();
    else
        data << pQuest->GetIncompleteEmote();

    // Close Window after cancel
    if (CloseOnCancel)
        data << uint32(0x01);
    else
        data << uint32(0x00);

    // Req Money
    data << uint32(pQuest->GetRewOrReqMoney() < 0 ? -pQuest->GetRewOrReqMoney() : 0);

    data << uint32(0x00);                                   // unk

    data << uint32( pQuest->GetReqItemsCount() );
    ItemPrototype const *pItem;
    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        if ( !pQuest->ReqItemId[i] ) continue;
        pItem = objmgr.GetItemPrototype(pQuest->ReqItemId[i]);
        data << uint32(pQuest->ReqItemId[i]);
        data << uint32(pQuest->ReqItemCount[i]);

        if ( pItem )
            data << uint32(pItem->DisplayInfoID);
        else
            data << uint32(0);
    }

    if ( !Completable )
        data << uint32(0x00);
    else
        data << uint32(0x03);

    data << uint32(0x04) << uint32(0x08) << uint32(0x10);

    pSession->SendPacket( &data );
    //sLog.outDebug( "WORLD: Sent SMSG_QUESTGIVER_REQUEST_ITEMS NPCGuid=%u, questid=%u",GUID_LOPART(npcGUID),pQuest->GetQuestId() );
}
