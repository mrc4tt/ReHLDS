/*
*
*    Permission is hereby granted, free of charge, to any person obtaining a copy
*    of this software and associated documentation files (the "Software"), to deal
*    in the Software without restriction, subject to the terms of the MIT License.
*
*/

#include "precompiled.h"

#ifndef _WIN32
#include <time.h>
#include <sys/prctl.h>
#endif

// -1 = adaptive spin margin (default), 0 = pure sleep (lowest CPU), >0 = fixed spin margin in microseconds
cvar_t sys_precise_spin = { "sys_precise_spin", "-1", 0, 0.0f, NULL };
// Print a breakdown of any frame whose work time exceeds this many milliseconds (0 = off)
cvar_t rehlds_stats_hitch_ms = { "rehlds_stats_hitch_ms", "0", 0, 0.0f, NULL };

bool g_bPreciseFrameTiming = false;

static const char *s_sectionNames[HPS_COUNT] =
{
	"cmdbuf (console/rcon)",
	"net in  (usercmds/think)",
	"physics (game+plugins)",
	"net out (fullpack/send)",
	"steam",
	"misc",
};

/*
* Precise frame scheduler
*/

static double s_nextFrameTime = 0.0;   // Sys_FloatTime() at which the next frame is due
static double s_framePeriod = 0.0;
static double s_spinMargin = 50e-6;    // current spin margin (seconds)
static double s_overSleepAvg = 30e-6;  // EWMA of nanosleep oversleep

bool HostPerf_PreciseGate(double fps)
{
	const double now = Sys_FloatTime();
	const double period = 1.0 / fps;

	if (period != s_framePeriod || s_nextFrameTime == 0.0)
	{
		s_framePeriod = period;
		s_nextFrameTime = now;
	}

	// small tolerance so a wakeup a hair early doesn't burn an extra loop
	if (now < s_nextFrameTime - 2e-6)
		return false;

	// Advance on the grid (drift-free). A late frame does not push the next
	// one out, so the average rate stays exactly at sys_ticrate.
	s_nextFrameTime += period;

	// More than one full period behind (map load, hitch): resync instead of
	// bursting a pile of catch-up frames.
	if (now - s_nextFrameTime > period)
		s_nextFrameTime = now + period;

	return true;
}

/*
* Profiler state
*/

struct hostperf_window_t
{
	double start;
	int frames;

	double intervalSum, intervalSqSum, intervalMin, intervalMax;
	int lateFrames;            // interval > 1.5 * period

	double busySum, busyMax;   // work time of Host_Frame
	double sectionSum[HPS_COUNT];
	double sectionMax[HPS_COUNT];

	// breakdown of the slowest frame in the window
	double worstBusy;
	double worstSection[HPS_COUNT];

	// scheduler
	int waits;
	double sleepOverSum, sleepOverMax;
	double spinSum;
	double wakeLateSum, wakeLateMax; // how late vs the deadline we actually woke up
};

static hostperf_window_t s_cur;
static hostperf_window_t s_last;
static bool s_haveLast = false;

static double s_frameStart = 0.0;
static double s_lastFrameStart = 0.0;
static double s_secStart[HPS_COUNT];
static double s_frameSection[HPS_COUNT];

static const double HOSTPERF_WINDOW = 5.0;

static void HostPerf_ResetWindow(hostperf_window_t &w, double now)
{
	Q_memset(&w, 0, sizeof(w));
	w.start = now;
	w.intervalMin = 1e9;
}

void HostPerf_SectionBegin(hostperf_section_e sec)
{
	s_secStart[sec] = Sys_FloatTime();
}

void HostPerf_SectionEnd(hostperf_section_e sec)
{
	s_frameSection[sec] += Sys_FloatTime() - s_secStart[sec];
}

void HostPerf_FrameBegin()
{
	const double now = Sys_FloatTime();

	if (s_cur.start == 0.0)
		HostPerf_ResetWindow(s_cur, now);

	if (s_lastFrameStart != 0.0)
	{
		double interval = now - s_lastFrameStart;
		s_cur.intervalSum += interval;
		s_cur.intervalSqSum += interval * interval;
		if (interval < s_cur.intervalMin) s_cur.intervalMin = interval;
		if (interval > s_cur.intervalMax) s_cur.intervalMax = interval;
		double period = s_framePeriod;
		if (period <= 0.0 && sys_ticrate.value > 0.0f)
			period = 1.0 / sys_ticrate.value;
		if (period > 0.0 && interval > period * 1.5)
			s_cur.lateFrames++;
	}

	s_lastFrameStart = now;
	s_frameStart = now;
	Q_memset(s_frameSection, 0, sizeof(s_frameSection));
}

void HostPerf_FrameEnd()
{
	const double now = Sys_FloatTime();
	const double busy = now - s_frameStart;

	s_cur.frames++;
	s_cur.busySum += busy;
	if (busy > s_cur.busyMax)
		s_cur.busyMax = busy;

	double accounted = 0.0;
	for (int i = 0; i < HPS_MISC; i++)
		accounted += s_frameSection[i];

	// everything not covered by a named section goes to misc
	s_frameSection[HPS_MISC] += max(0.0, busy - accounted - s_frameSection[HPS_MISC]);

	for (int i = 0; i < HPS_COUNT; i++)
	{
		s_cur.sectionSum[i] += s_frameSection[i];
		if (s_frameSection[i] > s_cur.sectionMax[i])
			s_cur.sectionMax[i] = s_frameSection[i];
	}

	if (busy > s_cur.worstBusy)
	{
		s_cur.worstBusy = busy;
		Q_memcpy(s_cur.worstSection, s_frameSection, sizeof(s_frameSection));
	}

	if (rehlds_stats_hitch_ms.value > 0.0f && busy * 1000.0 > rehlds_stats_hitch_ms.value)
	{
		Con_Printf("[rehlds_stats] hitch %.2f ms:", busy * 1000.0);
		for (int i = 0; i < HPS_COUNT; i++)
		{
			if (s_frameSection[i] * 1000.0 >= 0.05)
				Con_Printf(" %s=%.2f", s_sectionNames[i], s_frameSection[i] * 1000.0);
		}
		Con_Printf("\n");
	}

	if (now - s_cur.start >= HOSTPERF_WINDOW)
	{
		s_last = s_cur;
		s_last.start = now - s_cur.start; // store the window duration
		s_haveLast = true;
		HostPerf_ResetWindow(s_cur, now);
	}
}

/*
* Wait for next frame (exported to hlds_linux for -pingboost 5)
*/

#ifndef _WIN32

static void HostPerf_NanoSleep(double seconds)
{
	if (seconds <= 0.0)
		return;

	struct timespec req;
	req.tv_sec = (time_t)seconds;
	req.tv_nsec = (long)((seconds - (double)req.tv_sec) * 1e9);

	while (nanosleep(&req, &req) == -1 && errno == EINTR)
		;
}

static inline void HostPerf_CpuRelax()
{
#if defined(__i386__) || defined(__x86_64__)
	__builtin_ia32_pause();
#endif
}

DLL_EXPORT void Host_WaitForNextFrame()
{
	if (!g_bPreciseFrameTiming)
	{
		g_bPreciseFrameTiming = true;

		// Default timer slack is 50us, which is added to every sleep and is
		// the main reason sub-millisecond sleeps are inaccurate. 1ns = exact.
		prctl(PR_SET_TIMERSLACK, 1UL, 0UL, 0UL, 0UL);
	}

	// Gate not initialised yet (first frame / ticrate <= 0): don't stall
	if (s_nextFrameTime == 0.0 || s_framePeriod <= 0.0)
	{
		HostPerf_NanoSleep(100e-6);
		return;
	}

	const double deadline = s_nextFrameTime;
	double now = Sys_FloatTime();
	double remain = deadline - now;

	// safety for absurd values (clock jump, ticrate changed to something tiny)
	if (remain > 0.1)
		remain = 0.1;

	const float spinCfg = sys_precise_spin.value;
	double margin;
	if (spinCfg < 0.0f)
		margin = s_spinMargin;
	else
		margin = min((double)spinCfg, 1000.0) * 1e-6;

	if (remain > margin)
	{
		const double want = remain - margin;
		HostPerf_NanoSleep(want);

		const double after = Sys_FloatTime();
		const double over = max(0.0, (after - now) - want);
		now = after;

		s_cur.sleepOverSum += over;
		if (over > s_cur.sleepOverMax)
			s_cur.sleepOverMax = over;

		// adapt: margin tracks typical oversleep with headroom
		s_overSleepAvg = s_overSleepAvg * 0.95 + over * 0.05;
		s_spinMargin = clamp(s_overSleepAvg * 1.5 + 5e-6, 5e-6, 300e-6);
	}

	// spin the remainder (only up to `margin`, so CPU cost is bounded)
	const double spinStart = now;
	if (margin > 0.0)
	{
		while (now < deadline)
		{
			HostPerf_CpuRelax();
			now = Sys_FloatTime();
		}
	}

	s_cur.spinSum += now - spinStart;
	s_cur.waits++;

	const double late = max(0.0, now - deadline);
	s_cur.wakeLateSum += late;
	if (late > s_cur.wakeLateMax)
		s_cur.wakeLateMax = late;
}

#endif // _WIN32

/*
* rehlds_stats
*/

static void HostPerf_Stats_f()
{
	if (Cmd_Argc() > 1 && !Q_stricmp(Cmd_Argv(1), "reset"))
	{
		HostPerf_ResetWindow(s_cur, Sys_FloatTime());
		s_haveLast = false;
		Con_Printf("rehlds_stats: reset\n");
		return;
	}

	const hostperf_window_t &w = s_haveLast ? s_last : s_cur;
	const double dur = s_haveLast ? w.start : (Sys_FloatTime() - w.start);

	if (w.frames < 2 || dur <= 0.0)
	{
		Con_Printf("rehlds_stats: not enough data yet\n");
		return;
	}

	const double fps = w.frames / dur;
	const int intervals = w.frames - 1;
	const double avgInt = w.intervalSum / max(1, intervals);
	const double var = max(0.0, w.intervalSqSum / max(1, intervals) - avgInt * avgInt);
	const double target = s_framePeriod > 0.0 ? 1.0 / s_framePeriod : sys_ticrate.value;
	const double load = w.busySum / dur;

	Con_Printf("---- rehlds_stats (%.1fs window, %s) ----\n", dur, s_haveLast ? "last" : "partial");
	Con_Printf("fps        : %.1f (target %.1f, scheduler: %s)\n", fps, target, g_bPreciseFrameTiming ? "precise grid" : "legacy");
	Con_Printf("interval   : avg %.3f ms  min %.3f  max %.3f  jitter(sd) %.3f ms  late frames %d\n",
		avgInt * 1000.0, w.intervalMin * 1000.0, w.intervalMax * 1000.0, sqrt(var) * 1000.0, w.lateFrames);
	Con_Printf("frame work : avg %.3f ms  max %.3f ms  main-thread load %.1f%%\n",
		w.busySum / w.frames * 1000.0, w.busyMax * 1000.0, load * 100.0);

	Con_Printf("sections (avg / max per frame, share of work):\n");
	for (int i = 0; i < HPS_COUNT; i++)
	{
		Con_Printf("  %-26s %8.4f / %8.3f ms  %5.1f%%\n", s_sectionNames[i],
			w.sectionSum[i] / w.frames * 1000.0, w.sectionMax[i] * 1000.0,
			w.busySum > 0.0 ? w.sectionSum[i] / w.busySum * 100.0 : 0.0);
	}

	Con_Printf("worst frame %.3f ms:", w.worstBusy * 1000.0);
	for (int i = 0; i < HPS_COUNT; i++)
	{
		if (w.worstSection[i] * 1000.0 >= 0.01)
			Con_Printf(" %s=%.3f", s_sectionNames[i], w.worstSection[i] * 1000.0);
	}
	Con_Printf("\n");

	if (w.waits > 0)
	{
		Con_Printf("scheduler  : oversleep avg %.1f us max %.1f us | spin %.1f%% of a core | wake late avg %.1f us max %.1f us | margin %.0f us\n",
			w.sleepOverSum / w.waits * 1e6, w.sleepOverMax * 1e6,
			w.spinSum / dur * 100.0,
			w.wakeLateSum / w.waits * 1e6, w.wakeLateMax * 1e6,
			s_spinMargin * 1e6);
	}

	// Verdict: where is the time going?
	const char *verdict;
	if (target > 0.0 && fps < target * 0.97)
	{
		if (load > 0.85)
			verdict = "CPU bound: frame work fills the tick budget (see sections above)";
		else if (w.wakeLateMax > s_framePeriod * 0.5)
			verdict = "machine/scheduler: late wakeups (CPU contention, power saving, VM steal)";
		else if (!g_bPreciseFrameTiming)
			verdict = "below target: legacy 1ms sleep loop, use -pingboost 5 for precise scheduling";
		else
			verdict = "below target: frame spikes / map change in window (see worst frame)";
	}
	else if (w.lateFrames > w.frames / 100)
		verdict = "hitches: check 'worst frame' and set rehlds_stats_hitch_ms";
	else
		verdict = "healthy";
	Con_Printf("verdict    : %s\n", verdict);

	// per-player connection quality
	int shown = 0;
	for (int i = 0; i < g_psvs.maxclients; i++)
	{
		client_t *cl = &g_psvs.clients[i];
		if (!cl->active || cl->fakeclient)
			continue;

		if (!shown++)
			Con_Printf("players    : %-24s %6s %6s %6s %8s\n", "name", "ping", "loss", "choke", "rate");

		int ping, packetloss;
		SV_GetNetInfo(cl, &ping, &packetloss);
		Con_Printf("             %-24.24s %6d %6d %6d %8d\n", cl->name, ping, packetloss,
			cl->chokecount, (int)cl->netchan.rate);
	}
}

void HostPerf_Init()
{
	Cvar_RegisterVariable(&sys_precise_spin);
	Cvar_RegisterVariable(&rehlds_stats_hitch_ms);
	Cmd_AddCommand("rehlds_stats", HostPerf_Stats_f);
}
