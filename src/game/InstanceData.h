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

#ifndef MANGOS_INSTANCE_DATA_H
#define MANGOS_INSTANCE_DATA_H

#include "Common.h"

class Map;
class Unit;
class Player;
class GameObject;
class Creature;

class MANGOS_DLL_SPEC InstanceData
{
    public:

        explicit InstanceData(Map *map) : instance(map) {}
        virtual ~InstanceData() {}

        Map *instance;

        //On creation, NOT load.
        virtual void Initialize() {}

        //On load
        virtual void Load(const char* /*data*/) {}

        //When save is needed, this function generates the data
        virtual const char* Save() { return ""; }

        void SaveToDB();

        //Called every map update
        virtual void Update(uint32 /*diff*/) {}

        //Used by the map's CanEnter function.
        //This is to prevent players from entering during boss encounters.
        virtual bool IsEncounterInProgress() const { return false; };

        //Called when a player successfully enters the instance.
        virtual void OnPlayerEnter(Player *) {}

        //Called when a gameobject is created
        virtual void OnObjectCreate(GameObject *) {}

        //called on creature creation
        virtual void OnCreatureCreate(Creature * /*creature*/, uint32 /*creature_entry*/) {}
};
#endif
