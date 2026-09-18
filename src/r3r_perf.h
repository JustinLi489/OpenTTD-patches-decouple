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
 *
 * Gating -- the layer has TWO switches (release audit, 2026-09-19):
 *   R3R_PROBES            compile time. 0 = the probes are not in the binary at
 *                         all (see the macro below); 1 = compiled in, and the
 *                         runtime switches below decide what actually runs.
 *   R3R_DBG / R3R_PERF    runtime override of a probe-enabled build.
 *
 * The two hot entry points used to run even with the probes off:
 *   - R3RPerfFrameTick(): every 128 rendered frames it ran a full
 *     Train::Iterate() scan and opened/created R3R_debug.log + R3R_perf.log;
 *   - the R3RDbgEdge() gate (train_cmd.cpp): one unordered_map lookup per call,
 *     reached once per vehicle per tick from ReserveTrackUnderConsist() and once
 *     per collision pair per movement step from CheckTrainCollision().
 * Both now bail out before doing any work, so "off" costs one predicted branch.
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
 * Compile-time master switch for the whole probe layer.
 *
 * Defaults to R3R_PROBES_DEFAULT, i.e. ON in the internal test build (build\)
 * and OFF in the published release (build-release\ also passes
 * -DR3R_PROBES_DEFAULT=0 explicitly).
 *
 *   1 -> everything below is compiled as before, and R3R_DBG / R3R_PERF can
 *        still force the probes on or off at runtime. This is the build you
 *        diagnose with; R3R_DBG=0 on it is the KI-26 "probes really off"
 *        baseline.
 *   0 -> the probes are removed at compile time. R3RDbgOn() / R3RPerfOn() become
 *        a constant false, so every guarded block collapses to `if (false)`,
 *        MSVC then drops the block together with its format strings, and
 *        neither R3R_debug.log nor R3R_perf.log is ever opened or written. Use
 *        this for a release you want provably probe-free: no environment
 *        variable can turn anything back on.
 *
 * A release that still needs the probes for field diagnosis can be built with
 * -DR3R_PROBES=1 (keeping -DR3R_PROBES_DEFAULT=0 so they stay off unless
 * R3R_DBG=1 asks for them).
 */
#ifndef R3R_PROBES
#define R3R_PROBES R3R_PROBES_DEFAULT
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
#if R3R_PROBES
	static const bool on = []() {
		const char *env = std::getenv("R3R_DBG");
		if (env != nullptr && env[0] == '0') return false;
		if (env != nullptr && env[0] == '1') return true;
		return R3R_PROBES_DEFAULT != 0;
	}();
	return on;
#else
	return false; ///< compiled out; no environment variable can re-enable it
#endif
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
#if R3R_PROBES
	static const bool on = []() {
		const char *env = std::getenv("R3R_PERF");
		if (env != nullptr && env[0] == '0') return false;
		if (env != nullptr && env[0] == '1') return true;
		return R3RDbgOn();
	}();
	return on;
#else
	return false; ///< compiled out; no environment variable can re-enable it
#endif
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
 *
 * R3R (release audit 2026-09-19): in an R3R_PROBES=0 build this becomes a macro
 * that discards the whole call, *arguments included*.
 *
 * Why the macro is needed even though the body already folds away: an inlined
 * `if (!R3RDbgOn()) return;` removes the body, but the caller still evaluates
 * every argument before the (now empty) call. The sites in the window paint
 * path pass things like `GetWidget<NWidgetCore>(...)`, which the compiler cannot
 * prove side-effect free, so they survived as real work in a "probe-free" exe
 * -- once per window repaint. `((void)0)` removes the call and its arguments,
 * and unlike a `do {} while (0)` helper it stays a valid single statement, so a
 * braceless `if (...)` / `else` around a call site keeps compiling.
 */
#if R3R_PROBES
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
#else
#define R3RDbgWrite(...) ((void)0)
#endif

/** Snapshot the world, write one aggregate line to R3R_perf.log, reset counters. */
void R3RPerfDumpAndReset();

/**
 * Called once per rendered frame from UpdateWindows().
 * @param delta_ms Real time since the previous frame -- the frame rate itself.
 *
 * R3R (release audit 2026-09-19): this is the only per-frame hook in the tree, and
 * it used to keep counting and then call R3RPerfDumpAndReset() every 128 frames
 * even with the probes off -- a full Train::Iterate() scan plus two fopen() and a
 * write to R3R_perf.log, i.e. a periodic I/O burst and a stray log file next to a
 * published exe. Bail out first; with R3R_PROBES=0 the whole body is compiled away.
 */
inline void R3RPerfFrameTick(uint32_t delta_ms)
{
	if (!R3RPerfOn()) return;
	R3RPerfCounters &p = R3RP();
	p.frames++;
	p.frame_ms_sum += delta_ms;
	if (delta_ms > p.frame_ms_max) p.frame_ms_max = delta_ms;
	if (p.frames >= 128) R3RPerfDumpAndReset();
}

#endif /* R3R_PERF_H */
