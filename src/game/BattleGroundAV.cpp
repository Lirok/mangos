/*
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
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

#include "Player.h"
#include "BattleGround.h"
#include "BattleGroundAV.h"
#include "Creature.h"
#include "Chat.h"
#include "Object.h"
#include "ObjectMgr.h"
#include "ObjectAccessor.h"
#include "MapManager.h"
#include "Language.h"
#include "SpellAuras.h"
#include "Formulas.h"
#include "WorldPacket.h"

BattleGroundAV::BattleGroundAV()
{
    m_BgObjects.resize(BG_AV_OBJECT_MAX);
    m_BgCreatures.resize(BG_AV_CPLACE_MAX);

    m_StartMessageIds[BG_STARTING_EVENT_FIRST]  = LANG_BG_AV_START_TWO_MINUTES;
    m_StartMessageIds[BG_STARTING_EVENT_SECOND] = LANG_BG_AV_START_ONE_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_THIRD]  = LANG_BG_AV_START_HALF_MINUTE;
    m_StartMessageIds[BG_STARTING_EVENT_FOURTH] = LANG_BG_AV_HAS_BEGUN;
}

BattleGroundAV::~BattleGroundAV()
{
}

void BattleGroundAV::HandleKillPlayer(Player *player, Player *killer)
{
    if(GetStatus() != STATUS_IN_PROGRESS)
        return;

    BattleGround::HandleKillPlayer(player, killer);
    UpdateScore(player->GetTeam(), -1);
}

void BattleGroundAV::HandleKillUnit(Creature *unit, Player *killer)
{
    sLog.outDebug("bg_av HandleKillUnit %i",unit->GetEntry());
    if(GetStatus() != STATUS_IN_PROGRESS)
        return;
    switch(unit->GetEntry())
    {
        case BG_AV_CREATURE_ENTRY_A_BOSS:
            CastSpellOnTeam(BG_AV_BOSS_KILL_QUEST_SPELL, HORDE);   // this is a spell which finishes a quest where a player has to kill the boss
            RewardReputationToTeam(729, m_RepBoss, HORDE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_BOSS), HORDE);
            EndBattleGround(HORDE);
            break;
        case BG_AV_CREATURE_ENTRY_H_BOSS:
            CastSpellOnTeam(BG_AV_BOSS_KILL_QUEST_SPELL, ALLIANCE); // this is a spell which finishes a quest where a player has to kill the boss
            RewardReputationToTeam(730, m_RepBoss, ALLIANCE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_BOSS), ALLIANCE);
            EndBattleGround(ALLIANCE);
            break;
        case BG_AV_CREATURE_ENTRY_A_CAPTAIN:
            RewardReputationToTeam(729, m_RepCaptain, HORDE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_CAPTAIN), HORDE);
            UpdateScore(ALLIANCE, (-1) * BG_AV_RES_CAPTAIN);
            // spawn destroyed aura
            for(uint8 i = 0; i<=9; i++)
                SpawnBGObject(BG_AV_OBJECT_BURN_BUILDING_ALLIANCE + i, RESPAWN_IMMEDIATELY);
            SendYellToAll(LANG_BG_AV_H_CAPTAIN_DEAD, LANG_UNIVERSAL, m_DB_Creature[BG_AV_CREATURE_HERALD]);
            m_captainAlive[0]=false;
            break;
        case BG_AV_CREATURE_ENTRY_H_CAPTAIN:
            RewardReputationToTeam(730, m_RepCaptain, ALLIANCE);
            RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_CAPTAIN), ALLIANCE);
            UpdateScore(HORDE, (-1) * BG_AV_RES_CAPTAIN);
            // spawn destroyed aura
            for(uint8 i = 0; i<=9; i++)
                SpawnBGObject(BG_AV_OBJECT_BURN_BUILDING_HORDE + i, RESPAWN_IMMEDIATELY);
            SendYellToAll(LANG_BG_AV_H_CAPTAIN_DEAD, LANG_UNIVERSAL, m_DB_Creature[BG_AV_CREATURE_HERALD]);
            m_captainAlive[1]=false;
            break;
        case BG_AV_CREATURE_ENTRY_NM_N_B:
        case BG_AV_CREATURE_ENTRY_NM_A_B:
        case BG_AV_CREATURE_ENTRY_NM_H_B:
            ChangeMineOwner(BG_AV_NORTH_MINE, killer->GetTeam());
            break;
        case BG_AV_CREATURE_ENTRY_SM_N_B:
        case BG_AV_CREATURE_ENTRY_SM_A_B:
        case BG_AV_CREATURE_ENTRY_SM_H_B:
            ChangeMineOwner(BG_AV_SOUTH_MINE, killer->GetTeam());
            break;
    }
}

void BattleGroundAV::HandleQuestComplete(uint32 questid, Player *player)
{
    if (GetStatus() != STATUS_IN_PROGRESS)
        return;
    uint8 team = GetTeamIndexByTeamId(player->GetTeam());
    uint32 reputation = 0;                                  // reputation for the whole team (other reputation must be done in db)
    // TODO add events (including quest not available anymore, next quest availabe, go/npc de/spawning)
    // maybe we can do it with sd2?
    sLog.outError("BG_AV Quest %i completed", questid);
    switch(questid)
    {
        case BG_AV_QUEST_A_SCRAPS1:
        case BG_AV_QUEST_A_SCRAPS2:
        case BG_AV_QUEST_H_SCRAPS1:
        case BG_AV_QUEST_H_SCRAPS2:
            m_Team_QuestStatus[team][0] += 20;
            reputation = 1;
            if( m_Team_QuestStatus[team][0] == 500 || m_Team_QuestStatus[team][0] == 1000 || m_Team_QuestStatus[team][0] == 1500 ) //25,50,75 turn ins
            {
                sLog.outDebug("BG_AV Quest %i completed starting with unit upgrading..", questid);
                for (BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i <= BG_AV_NODES_FROSTWOLF_HUT; ++i)
                {
                    if( m_Nodes[i].Owner == player->GetTeam() && m_Nodes[i].State == POINT_CONTROLLED )
                    {
                        DePopulateNode(i);
                        PopulateNode(i);
                     }
                }
            }
            break;
        case BG_AV_QUEST_A_COMMANDER1:
        case BG_AV_QUEST_H_COMMANDER1:
            m_Team_QuestStatus[team][1]++;
            reputation = 1;
            if( m_Team_QuestStatus[team][1] == 120 )
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
            break;
        case BG_AV_QUEST_A_COMMANDER2:
        case BG_AV_QUEST_H_COMMANDER2:
            m_Team_QuestStatus[team][2]++;
            reputation = 2;
            if( m_Team_QuestStatus[team][2] == 60 )
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
            break;
        case BG_AV_QUEST_A_COMMANDER3:
        case BG_AV_QUEST_H_COMMANDER3:
            m_Team_QuestStatus[team][3]++;
            reputation = 5;
            RewardReputationToTeam(team, 1, player->GetTeam());
            if( m_Team_QuestStatus[team][1] == 30 )
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
            break;
        case BG_AV_QUEST_A_BOSS1:
        case BG_AV_QUEST_H_BOSS1:
            m_Team_QuestStatus[team][4] += 4;               // there are 2 quests where you can turn in 5 or 1 item.. ( + 4 cause +1 will be done some lines below)
            reputation = 4;
        case BG_AV_QUEST_A_BOSS2:
        case BG_AV_QUEST_H_BOSS2:
            m_Team_QuestStatus[team][4]++;
            reputation += 1;
            if( m_Team_QuestStatus[team][4] >= 200 )
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
            break;
        case BG_AV_QUEST_A_NEAR_MINE:
        case BG_AV_QUEST_H_NEAR_MINE:
            m_Team_QuestStatus[team][5]++;
            reputation = 2;
            if( m_Team_QuestStatus[team][5] == 28 )
            {
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
                if( m_Team_QuestStatus[team][6] == 7 )
                    sLog.outDebug("BG_AV Quest %i completed (need to implement some events here - ground assault ready", questid);
            }
            break;
        case BG_AV_QUEST_A_OTHER_MINE:
        case BG_AV_QUEST_H_OTHER_MINE:
            m_Team_QuestStatus[team][6]++;
            reputation = 3;
            if( m_Team_QuestStatus[team][6] == 7 )
            {
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
                if( m_Team_QuestStatus[team][5] == 20 )
                    sLog.outDebug("BG_AV Quest %i completed (need to implement some events here - ground assault ready", questid);
            }
            break;
        case BG_AV_QUEST_A_RIDER_HIDE:
        case BG_AV_QUEST_H_RIDER_HIDE:
            m_Team_QuestStatus[team][7]++;
            reputation = 1;
            if( m_Team_QuestStatus[team][7] == 25 )
            {
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
                if( m_Team_QuestStatus[team][8] == 25 )
                    sLog.outDebug("BG_AV Quest %i completed (need to implement some events here - rider assault ready", questid);
            }
            break;
        case BG_AV_QUEST_A_RIDER_TAME:
        case BG_AV_QUEST_H_RIDER_TAME:
            m_Team_QuestStatus[team][8]++;
            reputation = 1;
            if( m_Team_QuestStatus[team][8] == 25 )
            {
                sLog.outDebug("BG_AV Quest %i completed (need to implement some events here", questid);
                if( m_Team_QuestStatus[team][7] == 25 )
                    sLog.outDebug("BG_AV Quest %i completed (need to implement some events here - rider assault ready", questid);
            }
            break;
        default:
            sLog.outDebug("BG_AV Quest %i completed but is not interesting for us", questid);
            return;
            break;
    }
    if(reputation)
        RewardReputationToTeam((player->GetTeam() == ALLIANCE) ? 730 : 729, reputation, player->GetTeam());
}

void BattleGroundAV::UpdateScore(uint32 team, int32 points )
{
    // note: to remove reinforcements points must be negative, for adding reinforcements points must be positive
    assert( team == ALLIANCE || team == HORDE);
    uint8 teamindex = GetTeamIndexByTeamId(team);
    m_TeamScores[teamindex] += points;                      // m_TeamScores is int32 - so no problems here

    if( points < 0 )
    {
        if( m_TeamScores[teamindex] < 1 )
        {
            m_TeamScores[teamindex] = 0;
            EndBattleGround(team);
        }
        else if(!m_IsInformedNearLose[teamindex] && m_TeamScores[teamindex] < BG_AV_SCORE_NEAR_LOSE)
        {
            SendMessageToAll((teamindex == BG_TEAM_HORDE) ? LANG_BG_AV_H_NEAR_LOSE : LANG_BG_AV_A_NEAR_LOSE, CHAT_MSG_BG_SYSTEM_NEUTRAL);
            PlaySoundToAll(BG_AV_SOUND_NEAR_LOSE);
            m_IsInformedNearLose[teamindex] = true;
        }
    }
    // must be called here, else it could display a negative value
    UpdateWorldState(((teamindex == BG_TEAM_HORDE) ? BG_AV_Horde_Score : BG_AV_Alliance_Score), m_TeamScores[teamindex]);
}

void BattleGroundAV::OnObjectCreate(GameObject* obj)
{
    switch(obj->GetEntry())
    {
        case BG_AV_OBJECTID_SNOWFALL_CANDY_A:
            m_SnowfallEyecandy[0].push_back(obj->GetGUID());
            SpawnBGObjectByGuid(obj->GetGUID(), RESPAWN_ONE_DAY);
            break;
        case BG_AV_OBJECTID_SNOWFALL_CANDY_PA:
            m_SnowfallEyecandy[1].push_back(obj->GetGUID());
            SpawnBGObjectByGuid(obj->GetGUID(), RESPAWN_ONE_DAY);
            break;
        case BG_AV_OBJECTID_SNOWFALL_CANDY_H:
            m_SnowfallEyecandy[2].push_back(obj->GetGUID());
            SpawnBGObjectByGuid(obj->GetGUID(), RESPAWN_ONE_DAY);
            break;
        case BG_AV_OBJECTID_SNOWFALL_CANDY_PH:
            m_SnowfallEyecandy[3].push_back(obj->GetGUID());
            SpawnBGObjectByGuid(obj->GetGUID(), RESPAWN_ONE_DAY);
            break;
        case BG_AV_OBJECTID_MINE_N:                         // irondeep mine supply
        case BG_AV_OBJECTID_MINE_S:                         // coldtooth mine supply
        default:
            // do nothing
            return;
    }

}

void BattleGroundAV::OnCreatureCreate(Creature* creature)
{
    switch(creature->GetEntry())
    {
        case BG_AV_CREATURE_ENTRY_H_BOSS:
            m_DB_Creature[BG_AV_CREATURE_H_BOSS] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_A_BOSS:
            m_DB_Creature[BG_AV_CREATURE_A_BOSS] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_N_HERALD:
            m_DB_Creature[BG_AV_CREATURE_HERALD] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_A_MARSHAL_SOUTH:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 0] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_A_MARSHAL_NORTH:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 1] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_A_MARSHAL_ICE:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 2] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_A_MARSHAL_STONE:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 3] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_H_MARSHAL_ICE:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 4] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_H_MARSHAL_TOWER:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 5] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_H_MARSHAL_ETOWER:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 6] = creature->GetGUID();
            break;
        case BG_AV_CREATURE_ENTRY_H_MARSHAL_WTOWER:
            m_DB_Creature[BG_AV_CREATURE_MARSHAL + 7] = creature->GetGUID();
            break;

        // TODO use BG_AV_MineCreature_Entries
        case 13396:                                         // northmine alliance TODO: get the right ids
        case 13080:
        case 13098:
        case 13078:
            SpawnBGCreatureByGuid(creature->GetGUID(), RESPAWN_ONE_DAY);
            m_MineCreatures[BG_AV_NORTH_MINE][0].push_back(creature->GetGUID());
            break;
        case 13397:                                         // northmine horde
        case 13099:
        case 13081:
        case 13079:
            SpawnBGCreatureByGuid(creature->GetGUID(), RESPAWN_ONE_DAY);
            m_MineCreatures[BG_AV_NORTH_MINE][1].push_back(creature->GetGUID());
            break;
        case 10987:                                         // northmine neutral -Trogg
        case 11600:                                         // Irondeep Shaman
        case 11602:                                         // Irondeep Skullthumper
        case 11657:                                         // Morloch
            m_MineCreatures[BG_AV_NORTH_MINE][2].push_back(creature->GetGUID());
            break;
         case 13317:                                        // southmine alliance
         case 13096:                                        // explorer
         case 13087:                                        // invader
         case 13086:
             SpawnBGCreatureByGuid(creature->GetGUID(), RESPAWN_ONE_DAY);
             m_MineCreatures[BG_AV_SOUTH_MINE][0].push_back(creature->GetGUID());
             break;
         case 13316:                                        // southmine horde
         case 13097:                                        // surveypr
         case 13089:                                        // guard
         case 13088:
             m_MineCreatures[BG_AV_SOUTH_MINE][1].push_back(creature->GetGUID());
             SpawnBGCreatureByGuid(creature->GetGUID(), RESPAWN_ONE_DAY);
             break;
         case 11603:                                        // southmine neutral
         case 11604:
         case 11605:
         case 11677:
         case 10982:                                        // vermin (special)
             if(creature->GetEntry() == 11677)
                 m_DB_Creature[BG_AV_CREATURE_SNIFFLE] = creature->GetGUID();
             m_MineCreatures[BG_AV_SOUTH_MINE][2].push_back(creature->GetGUID());
             break;

        case BG_AV_CREATURE_ENTRY_H_L1:
        case BG_AV_CREATURE_ENTRY_H_D_B:
        case BG_AV_CREATURE_ENTRY_H_D_C:
        case BG_AV_CREATURE_ENTRY_A_L1:
        case BG_AV_CREATURE_ENTRY_A_D_B:
        case BG_AV_CREATURE_ENTRY_A_D_C:
            // not impplemented yet
            break;
    }
    uint32 level = creature->getLevel();
    //FIXME after respawn they have their old level again
    if(level != 0)
        level += GetMaxLevel() - 60;                        // maybe we can do this more generic for custom level - range.. actually it's ok
    creature->SetLevel(level);
}

Creature* BattleGroundAV::AddAVCreature(uint32 cinfoid, uint32 type)
{
    uint32 level;
    Creature* creature = NULL;
    assert(type <= BG_AV_CPLACE_MAX);
    creature = AddCreature(BG_AV_CreatureInfo[cinfoid][0], type, BG_AV_CreatureInfo[cinfoid][1], BG_AV_CreaturePos[type][0], BG_AV_CreaturePos[type][1], BG_AV_CreaturePos[type][2], BG_AV_CreaturePos[type][3]);
    level = ( BG_AV_CreatureInfo[cinfoid][2] == BG_AV_CreatureInfo[cinfoid][3] ) ? BG_AV_CreatureInfo[cinfoid][2] : urand(BG_AV_CreatureInfo[cinfoid][2], BG_AV_CreatureInfo[cinfoid][3]);

    if(!creature)
        return NULL;

    if(cinfoid <= BG_AV_NPC_H_GRAVE_DEFENSE_3)                     // all gravedefender entries
    {
        CreatureData &data = objmgr.NewOrExistCreatureData(creature->GetDBTableGUIDLow());
        data.spawndist     = 5;
        creature->SetDefaultMovementType(RANDOM_MOTION_TYPE);
        creature->GetMotionMaster()->Initialize();
        creature->setDeathState(JUST_DIED);
        creature->Respawn();
        // TODO: find a way to add a motionmaster without killing the creature (i just copied this code from a gm - command)
    }

    // FIXME after respawn they have their old level again
    if(level != 0)
        level += GetMaxLevel() - 60;                        // maybe we can do this more generic for custom level - range.. actually it's ok
    creature->SetLevel(level);
    return creature;
}

void BattleGroundAV::Update(uint32 diff)
{
    BattleGround::Update(diff);
    if(GetStatus() != STATUS_IN_PROGRESS)
        return;

    // add points from mine owning, and look if the neutral team can reclaim the mine
    m_Mine_Timer -=diff;
    for(uint8 mine = 0; mine <2; mine++)
    {
        if(m_Mine_Owner[mine] == ALLIANCE || m_Mine_Owner[mine] == HORDE)
        {
            if( m_Mine_Timer <= 0)
                UpdateScore(m_Mine_Owner[mine], 1);

            if(m_Mine_Reclaim_Timer[mine] > diff)
                m_Mine_Reclaim_Timer[mine] -= diff;
            else{
                ChangeMineOwner(mine, BG_AV_NEUTRAL_TEAM);
            }
        }
    }
    if( m_Mine_Timer <= 0)
        m_Mine_Timer = BG_AV_MINE_TICK_TIMER;                  // this is at the end, cause we need to update both mines

    // looks for all timers of the nodes and destroy the building (for graveyards the building wont get destroyed, it goes just to the other team
    for(BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i < BG_AV_NODES_MAX; ++i)
    {
        if(m_Nodes[i].State == POINT_ASSAULTED)
        {
            if(m_Nodes[i].Timer > diff)
                m_Nodes[i].Timer -= diff;
            else
                 EventPlayerDestroyedPoint(i);
        }
    }
}

void BattleGroundAV::StartingEventCloseDoors()
{
    SpawnBGObject(BG_AV_OBJECT_DOOR_A, RESPAWN_IMMEDIATELY);
    SpawnBGObject(BG_AV_OBJECT_DOOR_A, RESPAWN_IMMEDIATELY);

    DoorClose(BG_AV_OBJECT_DOOR_A);
    DoorClose(BG_AV_OBJECT_DOOR_H);

    sLog.outDebug("Alterac Valley: entering state STATUS_WAIT_JOIN ...");
    // Initial Nodes spawning (grave/tower defending units - spiritguides)
    for(uint32 i = 0; i < BG_AV_OBJECT_MAX; i++)
        SpawnBGObject(i, RESPAWN_ONE_DAY);
    // mainspiritguides:
    sLog.outDebug("BG_AV: start spawning main - spiritguides");
    AddSpiritGuide(7, BG_AV_CreaturePos[7][0], BG_AV_CreaturePos[7][1], BG_AV_CreaturePos[7][2], BG_AV_CreaturePos[7][3], ALLIANCE);
    AddSpiritGuide(8, BG_AV_CreaturePos[8][0], BG_AV_CreaturePos[8][1], BG_AV_CreaturePos[8][2], BG_AV_CreaturePos[8][3], HORDE);
}

void BattleGroundAV::StartingEventOpenDoors()
{
    UpdateWorldState(BG_AV_SHOW_H_SCORE, 1);
    UpdateWorldState(BG_AV_SHOW_A_SCORE, 1);

    DoorOpen(BG_AV_OBJECT_DOOR_H);
    DoorOpen(BG_AV_OBJECT_DOOR_A);

    SpawnBGObject(BG_AV_OBJECT_AURA_N_SNOWFALL_GRAVE, RESPAWN_IMMEDIATELY);
    SpawnBGObject(BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE, RESPAWN_IMMEDIATELY);
    uint32 i;
    for(i = BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION; i <= BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE ; i++){
        SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + 3 * i,RESPAWN_IMMEDIATELY);
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    }
    for(i = BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH; i <= BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER ; i++)
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
    for(i = BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE; i <= BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER ; i++){
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);
        if(i<=BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT)
            SpawnBGObject(BG_AV_OBJECT_AURA_H_FIRSTAID_STATION + 3 * GetNodeThroughObject(i), RESPAWN_IMMEDIATELY);
    }
    for(i = BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH; i <= BG_AV_OBJECT_TFLAG_A_STONEHEART_BUNKER; i+=2)
    {
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);              // flag
        SpawnBGObject(i + 16, RESPAWN_IMMEDIATELY);         // aura
    }
    for(i = BG_AV_OBJECT_TFLAG_H_ICEBLOOD_TOWER; i <= BG_AV_OBJECT_TFLAG_H_FROSTWOLF_WTOWER; i+=2)
    {
        SpawnBGObject(i, RESPAWN_IMMEDIATELY);              // flag
        SpawnBGObject(i + 16, RESPAWN_IMMEDIATELY);         // aura
    }
    // creatures
    sLog.outDebug("BG_AV start populating nodes");
    for(BG_AV_Nodes i= BG_AV_NODES_FIRSTAID_STATION; i < BG_AV_NODES_MAX; ++i )
    {
        if(m_Nodes[i].Owner)
            PopulateNode(i);
    }

}

void BattleGroundAV::AddPlayer(Player *plr)
{
    BattleGround::AddPlayer(plr);
    // create score and add it to map, default values are set in constructor
    BattleGroundAVScore* sc = new BattleGroundAVScore;
    m_PlayerScores[plr->GetGUID()] = sc;
}

void BattleGroundAV::EndBattleGround(uint32 winner)
{
    // calculate bonuskills for both teams:
    uint32 ally_tower_survived =0;
    uint32 horde_tower_survived = 0;
    uint32 ally_graves_owned   =0;
    uint32 horde_graves_owned  =0;
    uint32 ally_mines_owned    =0;
    uint32 horde_mines_owned   =0;
    // towers all not destroyed:
    for(BG_AV_Nodes i = BG_AV_NODES_DUNBALDAR_SOUTH; i <= BG_AV_NODES_FROSTWOLF_WTOWER; ++i)
    {
            if(m_Nodes[i].State != POINT_DESTROYED)
            {
                if(m_Nodes[i].Owner == ALLIANCE)
                    ++ally_tower_survived;
                else
                    ++horde_tower_survived;
            }
    }
    // graves all controlled
    for(BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i < BG_AV_NODES_MAX; ++i)
    {
        if(m_Nodes[i].State == POINT_CONTROLLED)
        {
            if(m_Nodes[i].Owner == ALLIANCE)
                ++ally_graves_owned;
            else
                ++horde_graves_owned;
        }
    }
    // mines owned
    if(m_Mine_Owner[BG_AV_SOUTH_MINE] == ALLIANCE)
        ++ally_mines_owned;
    else if(m_Mine_Owner[BG_AV_SOUTH_MINE] == HORDE)
        ++horde_mines_owned;
    if(m_Mine_Owner[BG_AV_NORTH_MINE] == ALLIANCE)
        ++ally_mines_owned;
    else if(m_Mine_Owner[BG_AV_NORTH_MINE] == HORDE)
        ++horde_mines_owned;

    // now we have the values give the honor/reputation to the teams:

    // alliance:
    if( ally_tower_survived )
    {
        RewardReputationToTeam(730, ally_tower_survived * m_RepSurviveTower, ALLIANCE);
        RewardHonorToTeam(GetBonusHonorFromKill(ally_tower_survived * BG_AV_KILL_SURVIVING_TOWER), ALLIANCE);
    }
    if( ally_graves_owned )
        RewardReputationToTeam(730, ally_graves_owned * m_RepOwnedGrave, ALLIANCE);
    if( ally_mines_owned )
        RewardReputationToTeam(730, ally_mines_owned * m_RepOwnedMine, ALLIANCE);
    sLog.outDebug("alliance towers:%u honor:%u rep:%u", ally_tower_survived, GetBonusHonorFromKill(ally_tower_survived * BG_AV_KILL_SURVIVING_TOWER), ally_tower_survived * BG_AV_REP_SURVIVING_TOWER);
    // captain survived?:
    if( m_captainAlive[0] )
    {
        RewardReputationToTeam(730, m_RepSurviveCaptain, ALLIANCE);
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_SURVIVING_CAPTAIN), ALLIANCE);
    }
    // horde:
    if( horde_tower_survived )
    {
        RewardReputationToTeam(729, horde_tower_survived * m_RepSurviveTower, HORDE);
        RewardHonorToTeam(GetBonusHonorFromKill(horde_tower_survived * BG_AV_KILL_SURVIVING_TOWER), HORDE);
    }
    if( ally_graves_owned )
        RewardReputationToTeam(729, horde_graves_owned * m_RepOwnedGrave, HORDE);
    if( ally_mines_owned )
        RewardReputationToTeam(729, horde_mines_owned * m_RepOwnedMine, HORDE);
    sLog.outDebug("horde towers:%u honor:%u rep:%u", horde_tower_survived, GetBonusHonorFromKill(horde_tower_survived * BG_AV_KILL_SURVIVING_TOWER), horde_tower_survived * BG_AV_REP_SURVIVING_TOWER);
    // captain survived?:
    if( m_captainAlive[1] )
    {
        RewardReputationToTeam(729, m_RepSurviveCaptain, HORDE);
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_SURVIVING_CAPTAIN), HORDE);
    }

    // both teams:
    if( m_HonorMapComplete )
    {
        RewardHonorToTeam(m_HonorMapComplete, ALLIANCE);
        RewardHonorToTeam(m_HonorMapComplete, HORDE);
    }
    BattleGround::EndBattleGround(winner);
}

void BattleGroundAV::RemovePlayer(Player* plr,uint64 /*guid*/)
{

}


void BattleGroundAV::HandleAreaTrigger(Player *Source, uint32 Trigger)
{
    // this is wrong way to implement these things. On official it done by gameobject spell cast.
    if(GetStatus() != STATUS_IN_PROGRESS)
        return;

    uint32 SpellId = 0;
    switch(Trigger)
    {
        case 95:
        case 2608:
            if( Source->GetTeam() != ALLIANCE )
                Source->GetSession()->SendAreaTriggerMessage("Only The Alliance can use that portal");
            else
                Source->LeaveBattleground();
            break;
        case 2606:
            if( Source->GetTeam() != HORDE )
                Source->GetSession()->SendAreaTriggerMessage("Only The Horde can use that portal");
            else
                Source->LeaveBattleground();
            break;
        case 3326:
        case 3327:
        case 3328:
        case 3329:
        case 3330:
        case 3331:
            //Source->Unmount();
            break;
        default:
            sLog.outDebug("WARNING: Unhandled AreaTrigger in Battleground: %u", Trigger);
//            Source->GetSession()->SendAreaTriggerMessage("Warning: Unhandled AreaTrigger in Battleground: %u", Trigger);
            break;
    }

    if(SpellId)
        Source->CastSpell(Source, SpellId, true);
}

void BattleGroundAV::UpdatePlayerScore(Player* Source, uint32 type, uint32 value)
{

    std::map<uint64, BattleGroundScore*>::iterator itr = m_PlayerScores.find(Source->GetGUID());

    if(itr == m_PlayerScores.end())                         // player not found
        return;

    switch(type)
    {
        case SCORE_GRAVEYARDS_ASSAULTED:
            ((BattleGroundAVScore*)itr->second)->GraveyardsAssaulted += value;
            break;
        case SCORE_GRAVEYARDS_DEFENDED:
            ((BattleGroundAVScore*)itr->second)->GraveyardsDefended += value;
            break;
        case SCORE_TOWERS_ASSAULTED:
            ((BattleGroundAVScore*)itr->second)->TowersAssaulted += value;
            break;
        case SCORE_TOWERS_DEFENDED:
            ((BattleGroundAVScore*)itr->second)->TowersDefended += value;
            break;
        case SCORE_SECONDARY_OBJECTIVES:
            ((BattleGroundAVScore*)itr->second)->SecondaryObjectives += value;
            break;
        default:
            BattleGround::UpdatePlayerScore(Source, type, value);
            break;
    }
}



void BattleGroundAV::EventPlayerDestroyedPoint(BG_AV_Nodes node)
{

    uint32 object = GetObjectThroughNode(node);
    sLog.outDebug("BG_AV: player destroyed point node %i object %i", node, object);

    // despawn banner
    SpawnBGObject(object, RESPAWN_ONE_DAY);
    DestroyNode(node);
    UpdateNodeWorldState(node);

    uint32 owner = m_Nodes[node].Owner;
    if( IsTower(node) )
    {
        uint8 tmp = node - BG_AV_NODES_DUNBALDAR_SOUTH;
        // despawn marshal (one of those guys protecting the boss)
        if(m_DB_Creature[BG_AV_CREATURE_MARSHAL + tmp])
            SpawnBGCreatureByGuid(m_DB_Creature[BG_AV_CREATURE_MARSHAL + tmp], RESPAWN_ONE_DAY);
        else
            sLog.outError("BG_AV: playerdestroyedpoint: marshal %i doesn't exist", BG_AV_CREATURE_MARSHAL + tmp);
        // spawn destroyed aura
        for(uint8 i = 0; i <= 9; i++)
            SpawnBGObject(BG_AV_OBJECT_BURN_DUNBALDAR_SOUTH + i + (tmp * 10), RESPAWN_IMMEDIATELY);

        UpdateScore(GetOtherTeam(owner), (-1) * BG_AV_RES_TOWER);
        RewardReputationToTeam((owner == ALLIANCE) ? 730 : 729, m_RepTowerDestruction, owner);
        RewardHonorToTeam(GetBonusHonorFromKill(BG_AV_KILL_TOWER), owner);

        SpawnBGObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH + GetTeamIndexByTeamId(owner) + (2 * tmp), RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH + GetTeamIndexByTeamId(owner) + (2 * tmp), RESPAWN_ONE_DAY);
    }
    else
    {
        if( owner == ALLIANCE )
            SpawnBGObject(object - 11, RESPAWN_IMMEDIATELY);
        else
            SpawnBGObject(object + 11, RESPAWN_IMMEDIATELY);
        SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + 3 * node, RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + GetTeamIndexByTeamId(owner) + 3 * node, RESPAWN_IMMEDIATELY);
        PopulateNode(node);
        if(node == BG_AV_NODES_SNOWFALL_GRAVE)              // snowfall eyecandy
        {
            uint32 del_go = (owner == ALLIANCE) ? 1 : 3;
            uint32 add_go = (owner == ALLIANCE) ? 0 : 2;
            if(!m_SnowfallEyecandy[del_go].empty())
                for(BGObjects::const_iterator itr = m_SnowfallEyecandy[del_go].begin(); itr != m_SnowfallEyecandy[del_go].end(); ++itr)
                    SpawnBGObjectByGuid(*itr, RESPAWN_ONE_DAY);
            if(!m_SnowfallEyecandy[add_go].empty())
                for(BGObjects::const_iterator itr = m_SnowfallEyecandy[add_go].begin(); itr != m_SnowfallEyecandy[add_go].end(); ++itr)
                    SpawnBGObjectByGuid(*itr, RESPAWN_IMMEDIATELY);
        }
    }
    SendYell2ToAll((IsTower(node)) ? LANG_BG_AV_TOWER_TAKEN : LANG_BG_AV_GRAVE_TAKEN, LANG_UNIVERSAL, m_DB_Creature[BG_AV_CREATURE_HERALD], GetNodeName(node), ( owner == ALLIANCE ) ? LANG_BG_AV_ALLY : LANG_BG_AV_HORDE);
}

void BattleGroundAV::ChangeMineOwner(uint8 mine, uint32 team)
{
    // mine=0 northmine, mine=1 southmine
    // TODO changing the owner should result in setting respawntime to infinite for current creatures (they should fight the new ones), spawning new mine owners creatures and changing the chest - objects so that the current owning team can use them
    assert(mine == BG_AV_NORTH_MINE || mine == BG_AV_SOUTH_MINE);
    if( m_Mine_Owner[mine] == team )
        return;

    if( team != ALLIANCE && team != HORDE )
        team = BG_AV_NEUTRAL_TEAM;

    m_Mine_PrevOwner[mine] = m_Mine_Owner[mine];
    m_Mine_Owner[mine] = team;
    uint32 index;

    SendMineWorldStates(mine);

    sLog.outDebug("bg_av depopulating mine %i (0=north, 1=south)",mine);
    index = (m_Mine_PrevOwner[mine] == ALLIANCE) ? 0 : (m_Mine_PrevOwner[mine] == HORDE) ? 1 : 2;
    if( !m_MineCreatures[mine][index].empty() )
        for(BGCreatures::const_iterator itr = m_MineCreatures[mine][index].begin(); itr != m_MineCreatures[mine][index].end(); ++itr)
            SpawnBGCreatureByGuid(*itr, RESPAWN_ONE_DAY);

    sLog.outDebug("bg_av populating mine %i owner %i, prevowner %i",mine,m_Mine_Owner[mine], m_Mine_PrevOwner[mine]);
    index = (m_Mine_Owner[mine] == ALLIANCE)?0:(m_Mine_Owner[mine] == HORDE)?1:2;
    if( !m_MineCreatures[mine][index].empty() )
        for(BGCreatures::const_iterator itr = m_MineCreatures[mine][index].begin(); itr != m_MineCreatures[mine][index].end(); ++itr)
            SpawnBGCreatureByGuid(*itr, RESPAWN_IMMEDIATELY);

    // because the gameobjects in this mine have changed, update all surrounding players:
    // TODO: add gameobject - update code (currently this is done in a hacky way)
    if( team == ALLIANCE || team == HORDE )
    {
        PlaySoundToAll((team == ALLIANCE) ? BG_AV_SOUND_ALLIANCE_GOOD : BG_AV_SOUND_HORDE_GOOD);
        m_Mine_Reclaim_Timer[mine] = BG_AV_MINE_RECLAIM_TIMER;
        SendYell2ToAll(LANG_BG_AV_MINE_TAKEN , LANG_UNIVERSAL, m_DB_Creature[BG_AV_CREATURE_HERALD], (mine == BG_AV_NORTH_MINE) ? LANG_BG_AV_MINE_NORTH : LANG_BG_AV_MINE_SOUTH, (team == ALLIANCE ) ? LANG_BG_AV_ALLY : LANG_BG_AV_HORDE);
    }
}

bool BattleGroundAV::PlayerCanDoMineQuest(int32 GOId,uint32 team)
{
    if(GOId == BG_AV_OBJECTID_MINE_N)
         return (m_Mine_Owner[BG_AV_NORTH_MINE] == team);
    if(GOId == BG_AV_OBJECTID_MINE_S)
         return (m_Mine_Owner[BG_AV_SOUTH_MINE] == team);
    return true;                                            // cause it's no mine'object it is ok if this is true
}

void BattleGroundAV::PopulateNode(BG_AV_Nodes node)
{
    uint32 owner = m_Nodes[node].Owner;
    assert(owner);

    uint32 c_place = BG_AV_CPLACE_DEFENSE_STORM_AID + ( 4 * node );
    uint32 creatureid;
    if( IsTower(node) )
        creatureid = (owner == ALLIANCE) ? BG_AV_NPC_A_TOWER_DEFENSE : BG_AV_NPC_H_TOWER_DEFENSE;
    else
    {
        uint8 team = GetTeamIndexByTeamId(owner);
        if (m_Team_QuestStatus[team][0] < 500 )
            creatureid = ( owner == ALLIANCE )? BG_AV_NPC_A_GRAVE_DEFENSE_0 : BG_AV_NPC_H_GRAVE_DEFENSE_0;
        else if ( m_Team_QuestStatus[team][0] < 1000 )
            creatureid = ( owner == ALLIANCE )? BG_AV_NPC_A_GRAVE_DEFENSE_1 : BG_AV_NPC_H_GRAVE_DEFENSE_1;
        else if ( m_Team_QuestStatus[team][0] < 1500 )
            creatureid = ( owner == ALLIANCE )? BG_AV_NPC_A_GRAVE_DEFENSE_2 : BG_AV_NPC_H_GRAVE_DEFENSE_2;
        else
           creatureid = ( owner == ALLIANCE )? BG_AV_NPC_A_GRAVE_DEFENSE_3 : BG_AV_NPC_H_GRAVE_DEFENSE_3;

        // spiritguide
        if( m_BgCreatures[node] )
            DelCreature(node);
        if( !AddSpiritGuide(node, BG_AV_CreaturePos[node][0], BG_AV_CreaturePos[node][1], BG_AV_CreaturePos[node][2], BG_AV_CreaturePos[node][3], owner))
            sLog.outError("AV: couldn't spawn spiritguide at node %i", node);

    }
    for(uint32 i = 0; i < 4; i++)
    {
        Creature* cr = AddAVCreature(creatureid, c_place + i);
    }
}
void BattleGroundAV::DePopulateNode(BG_AV_Nodes node)
{
    uint32 c_place = BG_AV_CPLACE_DEFENSE_STORM_AID + ( 4 * node );
    for(uint8 i = 0; i < 4; i++)
        if( m_BgCreatures[c_place + i] )
            DelCreature(c_place + i);
    // spiritguide
    if( !IsTower(node) && m_BgCreatures[node] )
        DelCreature(node);
}


const BG_AV_Nodes BattleGroundAV::GetNodeThroughObject(uint32 object)
{
    sLog.outDebug("bg_AV getnodethroughobject %i", object);
    if( object <= BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER )
        return BG_AV_Nodes(object);
    if( object <= BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_HUT )
        return BG_AV_Nodes(object - 11);
    if( object <= BG_AV_OBJECT_FLAG_C_A_FROSTWOLF_WTOWER )
        return BG_AV_Nodes(object - 7);
    if( object <= BG_AV_OBJECT_FLAG_C_H_STONEHEART_BUNKER )
        return BG_AV_Nodes(object - 22);
    if( object <= BG_AV_OBJECT_FLAG_H_FROSTWOLF_HUT )
        return BG_AV_Nodes(object - 33);
    if( object <= BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER )
        return BG_AV_Nodes(object - 29);
    if( object == BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE )
        return BG_AV_NODES_SNOWFALL_GRAVE;
    sLog.outError("BattleGroundAV: ERROR! GetPlace got a wrong object :(");
    assert(false);
    return BG_AV_Nodes(0);
}

/// this function is the counterpart to GetNodeThroughObject()
const uint32 BattleGroundAV::GetObjectThroughNode(BG_AV_Nodes node)
{
    sLog.outDebug("bg_AV GetObjectThroughNode %i", node);
    if( m_Nodes[node].Owner == ALLIANCE )
    {
        if( m_Nodes[node].State == POINT_ASSAULTED )
        {
            if( node <= BG_AV_NODES_FROSTWOLF_HUT )
                return node + 11;
            if( node >= BG_AV_NODES_ICEBLOOD_TOWER && node <= BG_AV_NODES_FROSTWOLF_WTOWER)
                return node + 7;
        }
        else if ( m_Nodes[node].State == POINT_CONTROLLED )
            if( node <= BG_AV_NODES_STONEHEART_BUNKER )
                return node;
    }
    else if ( m_Nodes[node].Owner == HORDE )
    {
        if( m_Nodes[node].State == POINT_ASSAULTED )
            if( node <= BG_AV_NODES_STONEHEART_BUNKER )
                return node + 22;
        else if ( m_Nodes[node].State == POINT_CONTROLLED )
        {
            if( node <= BG_AV_NODES_FROSTWOLF_HUT )
                return node + 33;
            if( node >= BG_AV_NODES_ICEBLOOD_TOWER && node <= BG_AV_NODES_FROSTWOLF_WTOWER)
                return node + 29;
        }
    }
    else if ( m_Nodes[node].Owner == BG_AV_NEUTRAL_TEAM )
        return BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE;
    sLog.outError("BattleGroundAV: Error! GetPlaceNode couldn't resolve node %i", node);
    assert(false);
    return 0;
}


/// called when using a banner
void BattleGroundAV::EventPlayerClickedOnFlag(Player *source, GameObject* target_obj)
{
    if(GetStatus() != STATUS_IN_PROGRESS)
        return;
    int32 object = GetObjectType(target_obj->GetGUID());    // returns -1 on error
    sLog.outDebug("BG_AV using gameobject %i with type %i", target_obj->GetEntry(), object);
    if(object < 0)
        return;
    switch(target_obj->GetEntry())
    {
        case BG_AV_OBJECTID_BANNER_A:
        case BG_AV_OBJECTID_BANNER_A_B:
        case BG_AV_OBJECTID_BANNER_H:
        case BG_AV_OBJECTID_BANNER_H_B:
        case BG_AV_OBJECTID_BANNER_SNOWFALL_N:
            EventPlayerAssaultsPoint(source, object);
            break;
        case BG_AV_OBJECTID_BANNER_CONT_A:
        case BG_AV_OBJECTID_BANNER_CONT_A_B:
        case BG_AV_OBJECTID_BANNER_CONT_H:
        case BG_AV_OBJECTID_BANNER_CONT_H_B:
            EventPlayerDefendsPoint(source, object);
            break;
        default:
            break;
    }
}

void BattleGroundAV::EventPlayerDefendsPoint(Player* player, uint32 object)
{
    assert(GetStatus() == STATUS_IN_PROGRESS);
    BG_AV_Nodes node = GetNodeThroughObject(object);

    uint32 owner = m_Nodes[node].Owner;                     //maybe should name it prevowner
    uint32 team = player->GetTeam();

    if( owner == player->GetTeam() || m_Nodes[node].State != POINT_ASSAULTED )
        return;
    if( m_Nodes[node].TotalOwner == BG_AV_NEUTRAL_TEAM )    // initial snowfall capture
    {
        // until snowfall doesn't belong to anyone it is better handled in assault - code (best would be to have a special function
        // for neutral nodes.. but doing this just for snowfall will be a bit to much i think
        assert(node == BG_AV_NODES_SNOWFALL_GRAVE);         // currently the only neutral grave
        EventPlayerAssaultsPoint(player,object);
        return;
    }
    sLog.outDebug("player defends point object: %i node: %i", object, node);
    if( m_Nodes[node].PrevOwner != team )
    {
        sLog.outError("BG_AV: player defends point which doesn't belong to his team %i", node);
        return;
    }

    // spawn new banner (this thing which you can click)
    if( m_Nodes[node].Owner == ALLIANCE )
        SpawnBGObject(object + 22, RESPAWN_IMMEDIATELY);
    else
        SpawnBGObject(object - 22, RESPAWN_IMMEDIATELY);
    // despawn old banner
    SpawnBGObject(object, RESPAWN_ONE_DAY);

    if( !IsTower(node) )                                    // spawning + despawning of aura
    {
        SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + 3 * node, RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + GetTeamIndexByTeamId(team) + 3 * node, RESPAWN_IMMEDIATELY);

        if( node == BG_AV_NODES_SNOWFALL_GRAVE )            // snowfall eyecandy
        {
            uint32 del_go = (owner == ALLIANCE) ? 1 : 3;
            uint32 add_go = (team == ALLIANCE) ? 0 : 2;
            if( !m_SnowfallEyecandy[del_go].empty() )
                for(BGObjects::const_iterator itr = m_SnowfallEyecandy[del_go].begin(); itr != m_SnowfallEyecandy[del_go].end(); ++itr)
                    SpawnBGObjectByGuid(*itr, RESPAWN_ONE_DAY);
            if( !m_SnowfallEyecandy[add_go].empty() )
                for(BGObjects::const_iterator itr = m_SnowfallEyecandy[add_go].begin(); itr != m_SnowfallEyecandy[add_go].end(); ++itr)
                    SpawnBGObjectByGuid(*itr, RESPAWN_IMMEDIATELY);
        }
    }
    else
    {
        // spawn big flag + aura on top of tower
        SpawnBGObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        SpawnBGObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
    }

    DefendNode(node,team);                                  // set the right variables for nodeinfo
    PopulateNode(node);                                     // spawn node-creatures (defender for example)
    UpdateNodeWorldState(node);                             // send new mapicon to the player

    SendYell2ToAll(( IsTower(node) ) ? LANG_BG_AV_TOWER_DEFENDED : LANG_BG_AV_GRAVE_DEFENDED, LANG_UNIVERSAL, m_DB_Creature[BG_AV_CREATURE_HERALD], GetNodeName(node), ( team == ALLIANCE ) ? LANG_BG_AV_ALLY:LANG_BG_AV_HORDE);
    // update the statistic for the defending player
    UpdatePlayerScore(player, ( IsTower(node) ) ? SCORE_TOWERS_DEFENDED : SCORE_GRAVEYARDS_DEFENDED, 1);
    if( IsTower(node) )
        PlaySoundToAll(BG_AV_SOUND_BOTH_TOWER_DEFEND);
    else
        PlaySoundToAll((team == ALLIANCE)?BG_AV_SOUND_ALLIANCE_GOOD:BG_AV_SOUND_HORDE_GOOD);
}

void BattleGroundAV::EventPlayerAssaultsPoint(Player* player, uint32 object)
{
    BG_AV_Nodes node = GetNodeThroughObject(object);
    uint32 owner = m_Nodes[node].Owner;                     // maybe name it prevowner
    uint32 team  = player->GetTeam();
    sLog.outDebug("bg_av: player assaults point object %i node %i",object,node);
    if( owner == team || team == m_Nodes[node].TotalOwner )
        return;


    if( node == BG_AV_NODES_SNOWFALL_GRAVE )                // snowfall is a bit special in capping + it gets eyecandy stuff
    {
        if( object == BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE )  // initial capping
        {
            assert(owner == BG_AV_NEUTRAL_TEAM && m_Nodes[node].TotalOwner == BG_AV_NEUTRAL_TEAM);
            if( team == ALLIANCE )
                SpawnBGObject(BG_AV_OBJECT_FLAG_C_A_SNOWFALL_GRAVE, RESPAWN_IMMEDIATELY);
            else
                SpawnBGObject(BG_AV_OBJECT_FLAG_C_H_SNOWFALL_GRAVE, RESPAWN_IMMEDIATELY);
            SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + 3 * node, RESPAWN_IMMEDIATELY); // neutral aura spawn
        }
        else if( m_Nodes[node].TotalOwner == BG_AV_NEUTRAL_TEAM )   // recapping, when no team owns this node realy
        {
            assert(m_Nodes[node].State != POINT_CONTROLLED);
            if( team == ALLIANCE )
                SpawnBGObject(object - 11, RESPAWN_IMMEDIATELY);
            else
                SpawnBGObject(object + 11, RESPAWN_IMMEDIATELY);
        }
        // snowfall eyecandy
        uint32 del_go,add_go;
        if( team == ALLIANCE )
        {
            del_go = (m_Nodes[node].State == POINT_ASSAULTED) ? 3 : 2;
            add_go = 1;
        }
        else
        {
            del_go = (m_Nodes[node].State == POINT_ASSAULTED) ? 1 : 0;
            add_go = 3;
        }
        if( !m_SnowfallEyecandy[del_go].empty() && !m_SnowfallEyecandy[add_go].empty() )
        {
            for(BGObjects::const_iterator itr = m_SnowfallEyecandy[del_go].begin(); itr != m_SnowfallEyecandy[del_go].end(); ++itr)
                SpawnBGObjectByGuid(*itr, RESPAWN_ONE_DAY);
            for(BGObjects::const_iterator itr = m_SnowfallEyecandy[add_go].begin(); itr != m_SnowfallEyecandy[add_go].end(); ++itr)
                SpawnBGObjectByGuid(*itr, RESPAWN_IMMEDIATELY);
        }
    }

    // if snowfall gots capped it can be handled like all other graveyards
    if( m_Nodes[node].TotalOwner != BG_AV_NEUTRAL_TEAM )
    {
        assert(m_Nodes[node].Owner != BG_AV_NEUTRAL_TEAM);
        if( team == ALLIANCE )
            SpawnBGObject(object - 22, RESPAWN_IMMEDIATELY);
        else
            SpawnBGObject(object + 22, RESPAWN_IMMEDIATELY);
        if( IsTower(node) )
        {
            // spawning/despawning of bigflag + aura
            SpawnBGObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
            SpawnBGObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
            SpawnBGObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == ALLIANCE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
            SpawnBGObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH + (2 * (node - BG_AV_NODES_DUNBALDAR_SOUTH)), (team == HORDE) ? RESPAWN_IMMEDIATELY : RESPAWN_ONE_DAY);
        }
        else
        {
            // spawning/despawning of aura
            SpawnBGObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + 3 * node, RESPAWN_IMMEDIATELY);                            // neutral aura spawn
            SpawnBGObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + GetTeamIndexByTeamId(owner) + 3 * node, RESPAWN_ONE_DAY);  // teamaura despawn
            // Those who are waiting to resurrect at this object are taken to the closest next graveyard
            std::vector<uint64> ghost_list = m_ReviveQueue[m_BgCreatures[node]];
            if( !ghost_list.empty() )
            {
                WorldSafeLocsEntry const *ClosestGrave = NULL;
                for (std::vector<uint64>::const_iterator itr = ghost_list.begin(); itr != ghost_list.end(); ++itr)
                {
                    Player *plr = objmgr.GetPlayer( * itr);
                    if( !plr )
                        continue;
                    if(!ClosestGrave)
                        ClosestGrave = GetClosestGraveYard(plr);
                    if(ClosestGrave)
                        plr->TeleportTo(GetMapId(), ClosestGrave->x, ClosestGrave->y, ClosestGrave->z, plr->GetOrientation());
                }
                m_ReviveQueue[m_BgCreatures[node]].clear();
            }
        }
        DePopulateNode(node);
    }

    SpawnBGObject(object, RESPAWN_ONE_DAY);                 // delete old banner
    AssaultNode(node, team);                                // update nodeinfo variables
    UpdateNodeWorldState(node);                             // send mapicon

    SendYell2ToAll(( IsTower(node) ) ? LANG_BG_AV_TOWER_ASSAULTED: LANG_BG_AV_GRAVE_ASSAULTED, LANG_UNIVERSAL, m_DB_Creature[BG_AV_CREATURE_HERALD], GetNodeName(node), ( team == ALLIANCE ) ? LANG_BG_AV_ALLY:LANG_BG_AV_HORDE);
    // update the statistic for the assaulting player
    UpdatePlayerScore(player, ( IsTower(node) ) ? SCORE_TOWERS_ASSAULTED : SCORE_GRAVEYARDS_ASSAULTED, 1);
    PlaySoundToAll((team == ALLIANCE) ? BG_AV_SOUND_ALLIANCE_ASSAULTS : BG_AV_SOUND_HORDE_ASSAULTS);
}

void BattleGroundAV::FillInitialWorldStates(WorldPacket& data)
{
    bool stateok;
    // graveyards
    for (uint32 i = BG_AV_NODES_FIRSTAID_STATION; i <= BG_AV_NODES_FROSTWOLF_HUT; ++i)
    {
        for (uint8 j = 1; j <= 3; j+=2)
        {
            // j=1=assaulted j=3=controled
            stateok = (m_Nodes[i].State == j);
            data << uint32(BG_AV_NodeWorldStates[i][GetWorldStateType(j,ALLIANCE)]) << uint32((m_Nodes[i].Owner == ALLIANCE && stateok) ? 1 : 0);
            data << uint32(BG_AV_NodeWorldStates[i][GetWorldStateType(j,HORDE)]) << uint32((m_Nodes[i].Owner == HORDE && stateok) ? 1 : 0);
        }
    }

    // towers
    for (uint8 i = BG_AV_NODES_DUNBALDAR_SOUTH; i <= BG_AV_NODES_MAX; i++)
        for (uint8 j = 1; j <= 3; j+=2)
        {
            // j=1=assaulted j=3=controled
            // i dont have j = 2=destroyed cause destroyed is the same like enemy - team controll
            stateok = (m_Nodes[i].State == j || (m_Nodes[i].State == POINT_DESTROYED && j == 3));
            data << uint32(BG_AV_NodeWorldStates[i][GetWorldStateType(j, ALLIANCE)]) << uint32((m_Nodes[i].Owner == ALLIANCE && stateok) ? 1 : 0);
            data << uint32(BG_AV_NodeWorldStates[i][GetWorldStateType(j, HORDE)]) << uint32((m_Nodes[i].Owner == HORDE && stateok) ? 1 : 0);
        }
    if( m_Nodes[BG_AV_NODES_SNOWFALL_GRAVE].Owner == BG_AV_NEUTRAL_TEAM )   // cause neutral teams aren't handled generic
        data << uint32(AV_SNOWFALL_N) << uint32(1);
    data << uint32(BG_AV_Alliance_Score)  << uint32(m_TeamScores[0]);
    data << uint32(BG_AV_Horde_Score) << uint32(m_TeamScores[1]);
    if( GetStatus() == STATUS_IN_PROGRESS )                 // only if game is running the teamscores are displayed
    {
        data << uint32(BG_AV_SHOW_A_SCORE) << uint32(1);
        data << uint32(BG_AV_SHOW_H_SCORE) << uint32(1);
    }
    else
    {
        data << uint32(BG_AV_SHOW_A_SCORE) << uint32(0);
        data << uint32(BG_AV_SHOW_H_SCORE) << uint32(0);
    }
    SendMineWorldStates(BG_AV_NORTH_MINE);
    SendMineWorldStates(BG_AV_SOUTH_MINE);
}

/// this is used for node worldstates and returns values which fit good into the worldstates-array defined in header
uint8 BattleGroundAV::GetWorldStateType(uint8 state, uint32 team) const
{
    // neutral stuff cant get handled (currently its only snowfall) - if we want implement it we need a new column in this array
    assert(team != BG_AV_NEUTRAL_TEAM);
    // a_c a_a h_c h_a the positions in worldstate - array
    // a_c==alliance-controlled, a_a==alliance-assaulted, h_c==horde-controlled, h_a==horde-assaulted
    if( team == ALLIANCE )
    {
        if( state == POINT_CONTROLLED || state == POINT_DESTROYED )
            return 0;
        if( state == POINT_ASSAULTED )
            return 1;
    }
    if( team == HORDE )
    {
        if( state == POINT_DESTROYED || state == POINT_CONTROLLED )
            return 2;
        if( state == POINT_ASSAULTED )
            return 3;
    }
    sLog.outError("BG_AV: should update a strange worldstate state:%i team:%i", state, team);
    // we will crash the game with this.. so we have a good chance for a bugreport
    return 5;
}

void BattleGroundAV::UpdateNodeWorldState(BG_AV_Nodes node)
{
    UpdateWorldState(BG_AV_NodeWorldStates[node][GetWorldStateType(m_Nodes[node].State,m_Nodes[node].Owner)], 1);
    if( m_Nodes[node].PrevOwner == BG_AV_NEUTRAL_TEAM )     // currently only snowfall is supported as neutral node
        UpdateWorldState(AV_SNOWFALL_N, 0);
    else
        UpdateWorldState(BG_AV_NodeWorldStates[node][GetWorldStateType(m_Nodes[node].PrevState,m_Nodes[node].PrevOwner)], 0);
}

void BattleGroundAV::SendMineWorldStates(uint32 mine)
{
    assert(mine == BG_AV_NORTH_MINE || mine == BG_AV_SOUTH_MINE);
    assert(m_Mine_PrevOwner[mine] == ALLIANCE || m_Mine_PrevOwner[mine] == HORDE || m_Mine_PrevOwner[mine] == BG_AV_NEUTRAL_TEAM);
    assert(m_Mine_Owner[mine] == ALLIANCE || m_Mine_Owner[mine] == HORDE || m_Mine_Owner[mine] == BG_AV_NEUTRAL_TEAM);

    uint8 owner,prevowner;                                  // those variables are needed to access the right worldstate in the BG_AV_MineWorldStates array
    if( m_Mine_PrevOwner[mine] == ALLIANCE )
        prevowner = 0;
    else if( m_Mine_PrevOwner[mine] == HORDE )
        prevowner = 2;
    else
        prevowner = 1;
    if( m_Mine_Owner[mine] == ALLIANCE )
        owner = 0;
    else if( m_Mine_Owner[mine] == HORDE )
        owner = 2;
    else
        owner = 1;

    UpdateWorldState(BG_AV_MineWorldStates[mine][owner], 1);
    if( prevowner != owner )
        UpdateWorldState(BG_AV_MineWorldStates[mine][prevowner], 0);
}

WorldSafeLocsEntry const* BattleGroundAV::GetClosestGraveYard(Player *plr)
{
    float x = plr->GetPositionX();
    float y = plr->GetPositionY();
    uint32 team = plr->GetTeam();
    WorldSafeLocsEntry const* good_entry = NULL;
    if( GetStatus() == STATUS_IN_PROGRESS )
    {
        // Is there any occupied node for this team?
        float mindist = 9999999.0f;
        for(uint8 i = BG_AV_NODES_FIRSTAID_STATION; i <= BG_AV_NODES_FROSTWOLF_HUT; ++i)
        {
            if( m_Nodes[i].Owner != team || m_Nodes[i].State != POINT_CONTROLLED )
                continue;
            WorldSafeLocsEntry const * entry = sWorldSafeLocsStore.LookupEntry( BG_AV_GraveyardIds[i] );
            if( !entry )
                continue;
            float dist = (entry->x - x) * (entry->x - x) + (entry->y - y) * (entry->y - y);
            if( mindist > dist )
            {
                mindist = dist;
                good_entry = entry;
            }
        }
    }
    // If not, place ghost in the starting-cave
    if( !good_entry )
        good_entry = sWorldSafeLocsStore.LookupEntry( BG_AV_GraveyardIds[GetTeamIndexByTeamId(team) + 7] );

    return good_entry;
}

/// Create starting objects
bool BattleGroundAV::SetupBattleGround()
{
    if(
       // alliance gates
        !AddObject(BG_AV_OBJECT_DOOR_A, BG_AV_OBJECTID_GATE_A, BG_AV_DoorPositons[0][0], BG_AV_DoorPositons[0][1], BG_AV_DoorPositons[0][2], BG_AV_DoorPositons[0][3], 0, 0, sin(BG_AV_DoorPositons[0][3]/2), cos(BG_AV_DoorPositons[0][3]/2), RESPAWN_IMMEDIATELY)
        // horde gates
        || !AddObject(BG_AV_OBJECT_DOOR_H, BG_AV_OBJECTID_GATE_H, BG_AV_DoorPositons[1][0],BG_AV_DoorPositons[1][1], BG_AV_DoorPositons[1][2], BG_AV_DoorPositons[1][3], 0, 0, sin(BG_AV_DoorPositons[1][3]/2), cos(BG_AV_DoorPositons[1][3]/2), RESPAWN_IMMEDIATELY))
    {
        sLog.outErrorDb("BatteGroundAV: Failed to spawn doors, BattleGround not created!");
        return false;
    }

    // spawn node - objects
    for (uint8 i = BG_AV_NODES_FIRSTAID_STATION ; i < BG_AV_NODES_MAX; ++i)
    {
        if( i <= BG_AV_NODES_FROSTWOLF_HUT )
        {
            if(    !AddObject(i,BG_AV_OBJECTID_BANNER_A_B,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                || !AddObject(i + 11,BG_AV_OBJECTID_BANNER_CONT_A_B,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                || !AddObject(i + 33,BG_AV_OBJECTID_BANNER_H_B,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                || !AddObject(i + 22,BG_AV_OBJECTID_BANNER_CONT_H_B,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                // aura
                || !AddObject(BG_AV_OBJECT_AURA_N_FIRSTAID_STATION + i * 3,BG_AV_OBJECTID_AURA_N,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                || !AddObject(BG_AV_OBJECT_AURA_A_FIRSTAID_STATION + i * 3,BG_AV_OBJECTID_AURA_A,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                || !AddObject(BG_AV_OBJECT_AURA_H_FIRSTAID_STATION + i * 3,BG_AV_OBJECTID_AURA_H,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY))
            {
                sLog.outError("BatteGroundAV: Failed to spawn Node object BattleGround not created!");
                return false;
            }
        }
        else
        {
            if( i <= BG_AV_NODES_STONEHEART_BUNKER )        // alliance towers
            {
                if(   !AddObject(i,BG_AV_OBJECTID_BANNER_A,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(i + 22,BG_AV_OBJECTID_BANNER_CONT_H,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_AURA_A,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_AURA_N,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_TOWER_BANNER_A,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_TOWER_BANNER_PH,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY))
                {
                    sLog.outError("BatteGroundAV: Failed to spawn Alliance Tower object BattleGround not created!");
                    return false;
                }
            }
            else                                            // horde towers
            {
                if(     !AddObject(i + 7,BG_AV_OBJECTID_BANNER_CONT_A,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(i + 29,BG_AV_OBJECTID_BANNER_H,BG_AV_ObjectPos[i][0],BG_AV_ObjectPos[i][1],BG_AV_ObjectPos[i][2],BG_AV_ObjectPos[i][3], 0, 0, sin(BG_AV_ObjectPos[i][3]/2), cos(BG_AV_ObjectPos[i][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_A_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_AURA_N,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TAURA_H_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_AURA_H,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_A_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_TOWER_BANNER_PA,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY)
                    || !AddObject(BG_AV_OBJECT_TFLAG_H_DUNBALDAR_SOUTH + (2 * (i - BG_AV_NODES_DUNBALDAR_SOUTH)),BG_AV_OBJECTID_TOWER_BANNER_H,BG_AV_ObjectPos[i + 8][0],BG_AV_ObjectPos[i + 8][1],BG_AV_ObjectPos[i + 8][2],BG_AV_ObjectPos[i + 8][3], 0, 0, sin(BG_AV_ObjectPos[i + 8][3]/2), cos(BG_AV_ObjectPos[i + 8][3]/2),RESPAWN_ONE_DAY))
                {
                    sLog.outError("BatteGroundAV: Failed to spawn Horde Tower object BattleGround not created!");
                    return false;
                }
            }
            for(uint8 j = 0; j <= 9; j++)                   // burning aura for tower
            {
                if( !AddObject(BG_AV_OBJECT_BURN_DUNBALDAR_SOUTH + ((i - BG_AV_NODES_DUNBALDAR_SOUTH) * 10) + j,BG_AV_OBJECTID_FIRE,BG_AV_ObjectPos[BG_AV_OPLACE_BURN_DUNBALDAR_SOUTH + ((i - BG_AV_NODES_DUNBALDAR_SOUTH) * 10) + j][0],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_DUNBALDAR_SOUTH + ((i - BG_AV_NODES_DUNBALDAR_SOUTH) * 10) + j][1],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_DUNBALDAR_SOUTH + ((i - BG_AV_NODES_DUNBALDAR_SOUTH) * 10) + j][2],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_DUNBALDAR_SOUTH + ((i - BG_AV_NODES_DUNBALDAR_SOUTH) * 10) + j][3], 0, 0, sin(BG_AV_ObjectPos[BG_AV_OPLACE_BURN_DUNBALDAR_SOUTH + ((i - BG_AV_NODES_DUNBALDAR_SOUTH) * 10) + j][3]/2), cos(BG_AV_ObjectPos[BG_AV_OPLACE_BURN_DUNBALDAR_SOUTH + ((i - BG_AV_NODES_DUNBALDAR_SOUTH) * 10) + j][3]/2), RESPAWN_ONE_DAY) )
                {
                    sLog.outError("BatteGroundAV: Failed to spawn burning aura object node: %i BattleGround not created!", i);
                    return false;
                }
            }
        }
    }
    for(uint8 i = 0; i < BG_TEAMS_COUNT; i++)               // burning aura for buildings (after captain's death)
    {
        for(uint8 j = 0; j <= 9; j++)
        {
            if(j < 5)
            {
                if( !AddObject(BG_AV_OBJECT_BURN_BUILDING_ALLIANCE + (i * 10) + j,BG_AV_OBJECTID_SMOKE,BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][0],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][1],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][2],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][3], 0, 0, sin(BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][3]/2), cos(BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][3]/2), RESPAWN_ONE_DAY) )
                {
                    sLog.outError("BatteGroundAV: Failed to spawn burning aura object for captain's building %i, BattleGround not created!", j);
                    return false;
                }
            }
            else
            {
                if( !AddObject(BG_AV_OBJECT_BURN_BUILDING_ALLIANCE + (i * 10) + j,BG_AV_OBJECTID_FIRE,BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][0],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][1],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][2],BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][3], 0, 0, sin(BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][3]/2), cos(BG_AV_ObjectPos[BG_AV_OPLACE_BURN_BUILDING_A + (i * 10) + j][3]/2), RESPAWN_ONE_DAY) )
                {
                    sLog.outError("BatteGroundAV: Failed to spawn burning aura object for captain's building %i, BattleGround not created!", j);
                    return false;
                }
            }
        }
    }

    if( !AddObject(BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE, BG_AV_OBJECTID_BANNER_SNOWFALL_N ,BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE][0],BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE][1],BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE][2],BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE][3],0,0,sin(BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE][3]/2), cos(BG_AV_ObjectPos[BG_AV_NODES_SNOWFALL_GRAVE][3]/2), RESPAWN_ONE_DAY) )
    {
        sLog.outError("BatteGroundAV: Failed to spawn some banner object BattleGround not created!");
        return false;
    }
    return true;
}

uint32 BattleGroundAV::GetNodeName(BG_AV_Nodes node)
{
    switch (node)
    {
        case BG_AV_NODES_FIRSTAID_STATION:  return LANG_BG_AV_NODE_GRAVE_STORM_AID;
        case BG_AV_NODES_DUNBALDAR_SOUTH:   return LANG_BG_AV_NODE_TOWER_DUN_S;
        case BG_AV_NODES_DUNBALDAR_NORTH:   return LANG_BG_AV_NODE_TOWER_DUN_N;
        case BG_AV_NODES_STORMPIKE_GRAVE:   return LANG_BG_AV_NODE_GRAVE_STORMPIKE;
        case BG_AV_NODES_ICEWING_BUNKER:    return LANG_BG_AV_NODE_TOWER_ICEWING;
        case BG_AV_NODES_STONEHEART_GRAVE:  return LANG_BG_AV_NODE_GRAVE_STONE;
        case BG_AV_NODES_STONEHEART_BUNKER: return LANG_BG_AV_NODE_TOWER_STONE;
        case BG_AV_NODES_SNOWFALL_GRAVE:    return LANG_BG_AV_NODE_GRAVE_SNOW;
        case BG_AV_NODES_ICEBLOOD_TOWER:    return LANG_BG_AV_NODE_TOWER_ICE;
        case BG_AV_NODES_ICEBLOOD_GRAVE:    return LANG_BG_AV_NODE_GRAVE_ICE;
        case BG_AV_NODES_TOWER_POINT:       return LANG_BG_AV_NODE_TOWER_POINT;
        case BG_AV_NODES_FROSTWOLF_GRAVE:   return LANG_BG_AV_NODE_GRAVE_FROST;
        case BG_AV_NODES_FROSTWOLF_ETOWER:  return LANG_BG_AV_NODE_TOWER_FROST_E;
        case BG_AV_NODES_FROSTWOLF_WTOWER:  return LANG_BG_AV_NODE_TOWER_FROST_W;
        case BG_AV_NODES_FROSTWOLF_HUT:     return LANG_BG_AV_NODE_GRAVE_FROST_HUT;
        default: return 0; break;
    }
}

void BattleGroundAV::AssaultNode(BG_AV_Nodes node, uint32 team)
{
    assert(m_Nodes[node].TotalOwner != team);
    assert(m_Nodes[node].Owner != team);
    assert(m_Nodes[node].State != POINT_DESTROYED);
    assert(m_Nodes[node].State != POINT_ASSAULTED || !m_Nodes[node].TotalOwner ); // only assault an assaulted node if no totalowner exists
    // the timer gets another time, if the previous owner was 0 == Neutral
    m_Nodes[node].Timer      = (m_Nodes[node].PrevOwner) ? BG_AV_CAPTIME : BG_AV_SNOWFALL_FIRSTCAP;
    m_Nodes[node].PrevOwner  = m_Nodes[node].Owner;
    m_Nodes[node].Owner      = team;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = POINT_ASSAULTED;
}

void BattleGroundAV::DestroyNode(BG_AV_Nodes node)
{
    assert(m_Nodes[node].State == POINT_ASSAULTED);

    m_Nodes[node].TotalOwner = m_Nodes[node].Owner;
    m_Nodes[node].PrevOwner  = m_Nodes[node].Owner;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = (m_Nodes[node].Tower) ? POINT_DESTROYED : POINT_CONTROLLED;
    m_Nodes[node].Timer      = 0;
}

void BattleGroundAV::InitNode(BG_AV_Nodes node, uint32 team, bool tower)
{
    m_Nodes[node].TotalOwner = team;
    m_Nodes[node].Owner      = team;
    m_Nodes[node].PrevOwner  = 0;
    m_Nodes[node].State      = POINT_CONTROLLED;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = POINT_CONTROLLED;
    m_Nodes[node].Timer      = 0;
    m_Nodes[node].Tower      = tower;
}

void BattleGroundAV::DefendNode(BG_AV_Nodes node, uint32 team)
{
    assert(m_Nodes[node].TotalOwner == team);
    assert(m_Nodes[node].Owner != team);
    assert(m_Nodes[node].State != POINT_CONTROLLED && m_Nodes[node].State != POINT_DESTROYED);
    m_Nodes[node].PrevOwner  = m_Nodes[node].Owner;
    m_Nodes[node].Owner      = team;
    m_Nodes[node].PrevState  = m_Nodes[node].State;
    m_Nodes[node].State      = POINT_CONTROLLED;
    m_Nodes[node].Timer      = 0;
}

void BattleGroundAV::Reset()
{
    BattleGround::Reset();
    // set the reputation and honor variables:
    bool isBGWeekend = false;                               // TODO FIXME - call sBattleGroundMgr.IsBGWeekend(m_TypeID); - you must also implement that call!
    uint32 m_HonorMapComplete    = (isBGWeekend) ? BG_AV_KILL_MAP_COMPLETE_HOLIDAY : BG_AV_KILL_MAP_COMPLETE;
    uint32 m_RepTowerDestruction = (isBGWeekend) ? BG_AV_REP_TOWER_HOLIDAY         : BG_AV_REP_TOWER;
    uint32 m_RepCaptain          = (isBGWeekend) ? BG_AV_REP_CAPTAIN_HOLIDAY       : BG_AV_REP_CAPTAIN;
    uint32 m_RepBoss             = (isBGWeekend) ? BG_AV_REP_BOSS_HOLIDAY          : BG_AV_REP_BOSS;
    uint32 m_RepOwnedGrave       = (isBGWeekend) ? BG_AV_REP_OWNED_GRAVE_HOLIDAY   : BG_AV_REP_OWNED_GRAVE;
    uint32 m_RepSurviveCaptain   = (isBGWeekend) ? BG_AV_REP_SURVIVING_CAPTAIN_HOLIDAY : BG_AV_REP_SURVIVING_CAPTAIN;
    uint32 m_RepSurviveTower     = (isBGWeekend) ? BG_AV_REP_SURVIVING_TOWER_HOLIDAY : BG_AV_REP_SURVIVING_TOWER;


    for(uint8 i = 0; i < 2; i++)                            // forloop for both teams (it just make 0 == alliance and 1 == horde also for both mines 0=north 1=south
    {
        for(uint8 j = 0; j < 9; j++)
            m_Team_QuestStatus[i][j] = 0;
        m_TeamScores[i] = BG_AV_SCORE_INITIAL_POINTS;
        m_IsInformedNearLose[i] = false;
        m_Mine_Owner[i] = BG_AV_NEUTRAL_TEAM;
        m_Mine_PrevOwner[i] = m_Mine_Owner[i];
        m_captainAlive[i] = true;
    }
    for (uint8 i = 0; i < BG_AV_DB_CREATURE_MAX; i++)
        m_DB_Creature[i] = 0;

    for(uint8 i = 0; i < 4; i++)
    {
        m_SnowfallEyecandy[i].clear();
        m_SnowfallEyecandy[i].reserve(4);                   // official it's just 4, but making a vector here, allows more customisation
    }
    for(uint8 i = 0; i < 2; i++)
    {
        for(uint8 j = 0; j < 3; j++)
        {
            m_MineCreatures[i][j].clear();
            m_MineCreatures[i][j].reserve(100);
        }
    }

    for(BG_AV_Nodes i = BG_AV_NODES_FIRSTAID_STATION; i <= BG_AV_NODES_STONEHEART_GRAVE; ++i)   // alliance graves
        InitNode(i, ALLIANCE, false);
    for(BG_AV_Nodes i = BG_AV_NODES_DUNBALDAR_SOUTH; i <= BG_AV_NODES_STONEHEART_BUNKER; ++i)   // alliance towers
        InitNode(i, ALLIANCE, true);
    for(BG_AV_Nodes i = BG_AV_NODES_ICEBLOOD_GRAVE; i <= BG_AV_NODES_FROSTWOLF_HUT; ++i)        // horde graves
        InitNode(i, HORDE, false);
    for(BG_AV_Nodes i = BG_AV_NODES_ICEBLOOD_TOWER; i <= BG_AV_NODES_FROSTWOLF_WTOWER; ++i)     // horde towers
        InitNode(i, HORDE, true);
    InitNode(BG_AV_NODES_SNOWFALL_GRAVE, BG_AV_NEUTRAL_TEAM, false);                            // give snowfall neutral owner

    m_Mine_Timer = BG_AV_MINE_TICK_TIMER;
    for(uint32 i = 0; i < BG_AV_CPLACE_MAX; i++)
        if( m_BgCreatures[i] )
            DelCreature(i);
}
