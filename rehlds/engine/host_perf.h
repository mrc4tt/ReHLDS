/*
*
*    Permission is hereby granted, free of charge, to any person obtaining a copy
*    of this software and associated documentation files (the "Software"), to deal
*    in the Software without restriction, subject to the terms of the MIT License.
*
*/

#pragma once

// Host performance subsystem:
//
//  1) Precise frame scheduler (used by `-pingboost 5`).
//     The launcher calls the exported Host_WaitForNextFrame() instead of Sleep(1).
//     Frames are placed on a fixed time grid (1 / sys_ticrate) so the server hits
//     the configured tickrate evenly instead of "1 ms sleep + frame work" which
//     caps out around ~900 fps and jitters.
//
//  2) Lightweight frame profiler (`rehlds_stats` console command).
//     Splits every frame into sections (network in, game/plugins, network out, ...)
//     so lag can be attributed to the game DLL / plugins, the network, or the
//     machine (late wakeups / scheduler).

enum hostperf_section_e
{
	HPS_CMDBUF = 0,  // Cbuf_Execute (console / rcon / plugin server_cmd)
	HPS_NET_READ,    // SV_ReadPackets: packet parse, usercmds -> PlayerPreThink/PostThink, ClientCommand
	HPS_PHYSICS,     // SV_Physics: entity think/touch (game DLL + plugins), StartFrame
	HPS_NET_SEND,    // SV_SendClientMessages: packet entities, AddToFullPack/CheckVisibility (game + plugins)
	HPS_STEAM,       // Steam_RunFrame
	HPS_MISC,        // timeouts, resources, stats, security

	HPS_COUNT
};

extern bool g_bPreciseFrameTiming;
extern cvar_t sys_precise_spin;
extern cvar_t rehlds_stats_hitch_ms;

void HostPerf_Init();

// Precise grid gate, called from Host_FilterTime when precise timing is active.
bool HostPerf_PreciseGate(double fps);

// Frame profiler hooks
void HostPerf_FrameBegin();
void HostPerf_FrameEnd();
void HostPerf_SectionBegin(hostperf_section_e sec);
void HostPerf_SectionEnd(hostperf_section_e sec);

class CHostPerfScope
{
public:
	CHostPerfScope(hostperf_section_e sec) : m_sec(sec) { HostPerf_SectionBegin(sec); }
	~CHostPerfScope() { HostPerf_SectionEnd(m_sec); }

private:
	hostperf_section_e m_sec;
};

#define HOSTPERF_SCOPE(sec) CHostPerfScope _hostperf_scope_##sec(sec)

#ifndef _WIN32
// Exported to the dedicated launcher (hlds_linux, -pingboost 5).
DLL_EXPORT void Host_WaitForNextFrame();
#endif
