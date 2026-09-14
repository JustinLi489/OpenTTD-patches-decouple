/*
 * R3R performance probes (known issue KI-14).
 *
 * Purpose: localise a reported frame-rate drop / latency increase without
 * shipping a profiler. The game already measures PFE_GAMELOOP / PFE_GL_TRAINS /
 * PFE_DRAWING / PFE_DRAWWORLD / PFE_VIDEO (console command "framerates"), so
 * these probes only cover the R3R-specific suspects:
 *
 *   1. the fold-fix retry loop, which calls TryTrainCouple -> ChainFolded ->
 *      R3RCheckChainFold once per tick while it never converges, and every one
 *      of those calls does fopen/fprintf/fclose on R3R_debug.log (KI-06);
 *   2. the per-tick debug dumps (R3RDumpChainDbg / R3RDumpCoupleIdentity),
 *      which walk the whole chain and write it out;
 *   3. PositionHelper's "position in segment" rewrite (KI-10), which walks the
 *      chain backwards to the segment front and forwards to the segment end on
 *      the graphics path -- O(n) per vehicle becomes O(n^2) per chain whenever
 *      the NewGRF cache is invalidated.
 *
 * Everything here is counters plus one aggregate line per 128 rendered frames,
 * so the probes themselves are ~free. Remove this header (and the R3RPerf*
 * calls) once KI-14 is closed.
 *
 * Output files (game working directory): R3R_perf.log (aggregates).
 */

#ifndef R3R_PERF_H
#define R3R_PERF_H

#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

/** Monotonic nanoseconds, for the RAII timers below. */
inline uint64_t R3RPerfNowNs()
{
	return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct R3RPerfCounters {
	/* frame timing (filled by R3RPerfFrameTick) */
	uint64_t frames = 0;
	uint64_t frame_ms_sum = 0;
	uint64_t frame_ms_max = 0;

	/* hot paths */
	uint64_t try_couple = 0, try_couple_ns = 0;
	uint64_t fold_check = 0, fold_check_ns = 0;
	uint64_t dump_chain = 0, dump_chain_ns = 0;
	uint64_t dump_ident = 0, dump_ident_ns = 0;

	/* R3R_debug.log writes */
	uint64_t dbg_writes = 0, dbg_ns = 0;

	/* PositionHelper (NewGRF position-in-segment) */
	uint64_t pos_helper = 0, pos_steps = 0, pos_max = 0, pos_ns = 0;
};

/** Process-wide counters (single-threaded game logic). */
inline R3RPerfCounters &R3RP()
{
	static R3RPerfCounters c;
	return c;
}

/**
 * Compile-time default of the probes, used when R3R_DBG / R3R_PERF are unset.
 *
 * User decision 2026-09-14 (restated): the two build trees have two roles --
 *   build\\          = internal test build, probes ON, this is what you play;
 *   build-release\\  = what gets published, probes OFF.
 * So the default must follow the build type, not a hand-maintained flag.
 *
 * An explicit -DR3R_PROBES_DEFAULT=<n> always wins (R3R_release_build.cmd passes
 * 0). Without it the type is derived: MSVC defines _DEBUG only for the Debug
 * configuration (Debug CRT, /MTd), never for RelWithDebInfo/Release. Deriving it
 * is the safety net -- a Release build configured by hand still ships with the
 * probes off, which is exactly the intent for a published exe.
 *
 * A shipped exe can still force the probes back on at runtime with R3R_DBG=1
 * (see R3RDbgOn below), so a release binary stays debuggable when needed.
 */
#ifndef R3R_PROBES_DEFAULT
#  if defined(_DEBUG)
#    define R3R_PROBES_DEFAULT 1
#  else
#    define R3R_PROBES_DEFAULT 0
#  endif
#endif

/**
 * Master switch for every R3R probe and every R3R_debug.log write.
 *
 * With no environment variable set the behaviour follows R3R_PROBES_DEFAULT
 * (ON everywhere except a release build). R3R_DBG=0 forces a probe-free run --
 * that is the A/B baseline KI-26 asks for ("Release with the probes really
 * off"); R3R_DBG=1 forces the probes back on even in a release build. One
 * predicted branch on a cached static, so calling it at a log site is free
 * compared to the fopen/fprintf/fclose it guards (~160 us measured).
 */
inline bool R3RDbgOn()
{
	static const bool on = []() {
		const char *env = std::getenv("R3R_DBG");
		if (env != nullptr && env[0] == '0') return false;
		if (env != nullptr && env[0] == '1') return true;
		return R3R_PROBES_DEFAULT != 0;
	}();
	return on;
}

/**
 * Switch for the *timers* (R3RPerfTimer / R3RScopeTimer / the per-window PERF
 * line), i.e. the KI-14 / KI-26 frame-budget probes.
 *
 * Defaults to R3RDbgOn(), so R3R_DBG=0 really does shut the probes down (they
 * are not free: ~6.5k timer calls per frame plus the counters). R3R_PERF=1
 * overrides that and keeps the buckets alive while the log stays off -- the
 * useful combination for an A/B run, because the reported distribution is then
 * the one without any log-writing on top of it.
 */
inline bool R3RPerfOn()
{
	static const bool on = []() {
		const char *env = std::getenv("R3R_PERF");
		if (env != nullptr && env[0] == '0') return false;
		if (env != nullptr && env[0] == '1') return true;
		return R3RDbgOn();
	}();
	return on;
}

/** RAII accumulator: adds the elapsed time to *acc on scope exit. */
struct R3RPerfTimer {
	uint64_t *acc;
	uint64_t t0;
	bool on;
	explicit R3RPerfTimer(uint64_t *a) : acc(a), t0(0), on(R3RPerfOn())
	{
		if (this->on) this->t0 = R3RPerfNowNs();
	}
	~R3RPerfTimer() { if (this->on) *acc += R3RPerfNowNs() - t0; }
};

/**
 * fopen() for R3R_debug.log, honouring R3RDbgOn().
 * Returns nullptr when the switch is off, which every call site already handles
 * (they all test `if (dbg != nullptr)`), so turning the switch off costs nothing
 * more than failing the open.
 */
inline FILE *R3RFopenDbg(const char *mode)
{
	return R3RDbgOn() ? fopen("R3R_debug.log", mode) : nullptr;
}

/**
 * Central R3R debug-log write: counts the call and the fopen/fprintf/fclose cost.
 * Only worth routing the *suspected hot* sites through here -- the cold ones can
 * keep their plain fopen, they are not part of the frame budget.
 */
inline void R3RDbgWrite(const char *fmt, ...)
{
	if (!R3RDbgOn()) return;
	R3RPerfCounters &p = R3RP();
	p.dbg_writes++;
	const uint64_t t0 = R3RPerfNowNs();
	FILE *dbg = fopen("R3R_debug.log", "a");
	if (dbg != nullptr) {
		va_list ap;
		va_start(ap, fmt);
		vfprintf(dbg, fmt, ap);
		va_end(ap);
		fclose(dbg);
	}
	p.dbg_ns += R3RPerfNowNs() - t0;
}

/** Snapshot the world, write one aggregate line to R3R_perf.log, reset counters. */
void R3RPerfDumpAndReset();

/**
 * Called once per rendered frame from UpdateWindows().
 * @param delta_ms Real time since the previous frame -- the frame rate itself.
 */
inline void R3RPerfFrameTick(uint32_t delta_ms)
{
	R3RPerfCounters &p = R3RP();
	p.frames++;
	p.frame_ms_sum += delta_ms;
	if (delta_ms > p.frame_ms_max) p.frame_ms_max = delta_ms;
	if (p.frames >= 128) R3RPerfDumpAndReset();
}

#endif /* R3R_PERF_H */
