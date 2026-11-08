/*
*
*    This program is free software; you can redistribute it and/or modify it
*    under the terms of the GNU General Public License as published by the
*    Free Software Foundation; either version 2 of the License, or (at
*    your option) any later version.
*
*    This program is distributed in the hope that it will be useful, but
*    WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
*    General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*
*    In addition, as a special exception, the author gives permission to
*    link the code of this program with the Half-Life Game Engine ("HL
*    Engine") and Modified Game Libraries ("MODs") developed by Valve,
*    L.L.C ("Valve").  You must obey the GNU General Public License in all
*    respects for all of the code used other than the HL Engine and MODs
*    from Valve.  If you modify this file, you may extend this exception
*    to your version of the file, but you are not obligated to do so.  If
*    you do not wish to do so, delete this exception statement from your
*    version.
*
*/
#pragma once

#include "rehlds_api.h"

// Interfaces that only this fork of ReHLDS (github.com/mrc4tt/ReHLDS) has.
//
// They live apart from IRehldsApi on purpose: the vtables of IRehldsApi and IRehldsHookchains
// follow upstream ReHLDS, and an entry only the fork has would sit where upstream puts its next
// entry, so a module built against one would call the wrong function on the other.
//
// Look the interface up by name with the engine's CreateInterface. An upstream engine has no
// such interface and returns NULL, so a module can use the fork's features where they exist and
// still load everywhere else:
//
//     IRehldsForkApi *fork = (IRehldsForkApi *)engineFactory(VREHLDS_FORK_API_VERSION, NULL);
//     if (fork && fork->GetMajorVersion() == REHLDS_FORK_API_VERSION_MAJOR) ...
//
// Entries are only ever appended; a change that breaks the layout bumps the major version.

#define REHLDS_FORK_API_VERSION_MAJOR 1
#define REHLDS_FORK_API_VERSION_MINOR 0

//SV_TestEntityPosition hook: whether an entity is stuck in another; returns the blocking entity or NULL
typedef IHookChain<edict_t *, edict_t *> IRehldsHook_SV_TestEntityPosition;
typedef IHookChainRegistry<edict_t *, edict_t *> IRehldsHookRegistry_SV_TestEntityPosition;

class IRehldsForkApi {
public:
	virtual ~IRehldsForkApi() { }

	virtual int GetMajorVersion() = 0;
	virtual int GetMinorVersion() = 0;

	// 1.0
	virtual IRehldsHookRegistry_SV_TestEntityPosition *SV_TestEntityPosition() = 0;
};

#define VREHLDS_FORK_API_VERSION "VREHLDS_FORK_API_VERSION001"
