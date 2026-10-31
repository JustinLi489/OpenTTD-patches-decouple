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
#include <cstring>

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

/**
 * R3R (第 296 轮, 2026-10-25): name of the debug log file.
 *
 * Server and client used to share one R3R_debug.log whenever they shared a
 * working directory, which made it impossible to tell which line came from
 * which side -- the 第 296 轮 log showed every CPL-PAIR edge twice, once per
 * process. The name stays relative to the working directory, so launching the
 * two ends from build\srv and build\cli already separates the files;
 * R3R_DBG_FILE=<name> overrides it for the case where both ends must start from
 * the same directory.
 */
inline const char *R3RDbgFileName()
{
	static char name[64] = "R3R_debug.log";
	static const bool init = []() {
		const char *env = std::getenv("R3R_DBG_FILE");
		if (env != nullptr && env[0] != '\0') {
			std::strncpy(name, env, sizeof(name) - 1);
			name[sizeof(name) - 1] = '\0';
		}
		return true;
	}();
	(void)init;
	return name;
}

/**
 * R3R (第 296 轮, 2026-10-25): switch for the *bulk* probes.
 *
 * These five sites were 90% of the 第 296 轮 log (41658 lines) and none of them
 * says anything about the three reported problems:
 *   SPDD            19291 lines  per-vehicle per-tick speed dump (train_cmd.cpp)
 *   GRF-WEIGHT       8381 lines  every NewGRF weight query (newgrf_engine.cpp)
 *   CARGOWEIGHT-VEH  6144 lines  every cargo-weight recompute (ground_vehicle.cpp)
 *   GEO              1848 lines  per-commit chain geometry dump (train_cmd.cpp)
 *   SEGFE-VEH        1826 lines  per-vehicle segment-front probe (train_cmd.cpp)
 *
 * They stay in the binary but default to OFF; R3R_DBG_BULK=1 brings them back
 * when a geometry / weight question needs them. R3R_DBG=0 still wins over both.
 */
inline bool R3RDbgBulkOn()
{
#if R3R_PROBES
	static const bool on = []() {
		const char *env = std::getenv("R3R_DBG_BULK");
		return env != nullptr && (env[0] == '1' || env[0] == 'a' || env[0] == 'A');
	}();
	return on;
#else
	return false;
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
	/* R3R (第 274 轮续, 2026-10-22, KI-419③): this is the *second* log exit. It has
	 * ~120 call sites that fprintf their own lines, so it carries no tag the filter
	 * above could look at -- which is why GEO / CRT / TRP / TPR-WALK / DEPOT-ARR /
	 * R3R-RES-ONCE kept showing up in a log where every tagged probe was supposed to
	 * be quiet. Giving each site a tag would mean touching a hundred scopes; a coarse
	 * switch gives the same control at zero risk: R3R_DBG_RAW=0 silences every raw
	 * site at runtime (no rebuild), R3R_DBG=0 still silences everything. Defaults to
	 * on, because these are event-scale probes (282 lines in the 第 274 轮 log) and
	 * not the flood the aggregation work is about. */
#if R3R_PROBES
	static const bool raw_on = []() {
		const char *env = std::getenv("R3R_DBG_RAW");
		return !(env != nullptr && env[0] == '0');
	}();
	return (R3RDbgOn() && raw_on) ? fopen(R3RDbgFileName(), mode) : nullptr;
#else
	/* R3R (第 386 轮 / 探针分离): 编译期切掉整条日志出口。
	 *
	 * 原实现靠 `R3RDbgOn()` 在 R3R_PROBES=0 时恒 false 来折叠，但函数里那个
	 * `raw_on` 是 **function-local static**：它的初始化在第一次执行到声明时
	 * 发生，与后面的短路无关 —— 也就是说发布版仍会在这 169 个调用点共享的
	 * 第一次进入时跑一次 getenv("R3R_DBG_RAW") 并留下一个 static 守卫对象。
	 * 探针分离后这里连分支都不留：直接 nullptr，调用点的
	 * `if (dbg != nullptr) { fprintf(...); }` 在 /O2 下随之整块消失。 */
	(void)mode;
	return nullptr;
#endif
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
/**
 * R3R (第 274 轮, 2026-10-22, memo §7.10): tag filter for probe convergence.
 *
 * The player's call was "keep the reservation probes, temporarily disable the
 * rest". Every probe in the tree writes through R3RDbgWrite(), so the whole set
 * can be switched off from here -- one predicate instead of commenting out
 * several hundred call sites (and losing them).
 *
 * The tag is the first whitespace-delimited token of the format string, which is
 * the convention every call site already follows ("RESV-WATCH ...", "UNRES-RAIL
 * ...", "FOLDCHK ..."). Only the reservation side stays on by default:
 *   RESV-*      (RESV-WATCH / -GC / -REAP / -PURGE / -AUDIT-* / -LEDGER-* / ...)
 *   UNRES-RAIL  (the inner-most release line, see pbs.cpp)
 *
 * R3R_DBG_TAGS=all (or 1 / *) restores every probe at runtime, no rebuild needed.
 * That is deliberately an environment variable and not a config setting: this is
 * a diagnostic switch, and the probe-free and probe-full runs must be selectable
 * from the launcher that starts the exe.
 */
inline bool R3RDbgTagAllowed(const char *fmt)
{
#if R3R_PROBES
	static const bool all_tags = []() {
		const char *env = std::getenv("R3R_DBG_TAGS");
		if (env == nullptr) return false;
		return env[0] == 'a' || env[0] == 'A' || env[0] == '1' || env[0] == '*';
	}();
	if (all_tags) return true;
	if (fmt == nullptr) return false;
	/* Bounded compare: a short literal must not make us read past its terminator. */
	size_t n = 0;
	while (n < 8 && fmt[n] != '\0' && fmt[n] != ' ' && fmt[n] != '\t' && fmt[n] != '\n') n++;
	if (n >= 4 && std::strncmp(fmt, "RESV", 4) == 0) return true;
	if (n >= 5 && std::strncmp(fmt, "UNRES", 5) == 0) return true;
	/* R3R (第 274 轮续, 2026-10-22, KI-419①): the round-274 whitelist above switched
	 * EXTEND-BACKOUT off, and that was a mistake -- it is the only line that says
	 * *why* a trial extension failed, i.e. the one thing the reserved-path
	 * investigation needs. In the 第 274 轮 log the companion phase guard let
	 * UNRES-RAIL fire 1225 times, so it is proven that the back-out ran 1225 times
	 * while not a single line survived. LOOP-CLOSED is its companion ("the
	 * pathfinder gave up here"), and R3R-RES-ONCE names the ForceReserveOnce
	 * consumption that decides whether a stopped train ever retries at all. All
	 * three are reservation-side diagnosis, so they belong to the set that stays on.
	 * Their volume is bounded at the call sites (EXTEND-BACKOUT is throttled per
	 * key, see ExtendTrainReservation), not by this predicate. */
	if (n >= 6 && std::strncmp(fmt, "EXTEND", 6) == 0) return true;
	if (n >= 4 && std::strncmp(fmt, "LOOP", 4) == 0) return true;
	if (n >= 7 && std::strncmp(fmt, "R3R-RES", 7) == 0) return true;
	/* R3R (第 296 轮, 2026-10-25): the round-274 whitelist above was tuned for the
	 * reservation investigation and switched off every couple / clone / order
	 * probe. That is why the 第 296 轮 log had 0 UNIT-NEW, 0 CPL-HIT and 0
	 * COUPLE-FAIL, i.e. it could not answer any of the three reported problems
	 * (slow coupling, one pair that never couples, clones numbered 5/6 instead
	 * of 3/4). The set below is exactly what those need. Every one of these tags
	 * is event-scale or throttled at its call site (R3RDbgEdge, per-key maps),
	 * so none of them floods -- the flooding came from the raw exit, see
	 * R3RDbgBulkOn() above. */
	/* (1) coupling: "too slow" + "never couples". */
	if (n >= 6 && std::strncmp(fmt, "COUPLE", 6) == 0) return true; ///< COUPLE-OK/-FAIL/-PREFLIGHT/-POSTFOLD/-REFUSE-STILL-FOLDED/-WAIT-*
	if (n >= 3 && std::strncmp(fmt, "CPL", 3) == 0) return true;    ///< CPL-S0-CHK/-WAITHOLD/-NOTREADY/-REFUSE-REAL-ARTIC/-HOLD-NOBOOK
	if (n >= 4 && std::strncmp(fmt, "FOLD", 4) == 0) return true;   ///< FOLDCHK/-DIR/-DIR-OVERRIDE*/-REFUSE-*
	if (n >= 6 && std::strncmp(fmt, "SPLICE", 6) == 0) return true; ///< SPLICE-GAP-REJECT/-LAST-RESORT/-WRONG-END
	if (n >= 5 && std::strncmp(fmt, "ARTIC", 5) == 0) return true;  ///< ARTIC-SKIP-R3R (KI-274)
	if (n >= 5 && std::strncmp(fmt, "SKIP-", 5) == 0) return true;  ///< SKIP-STOPPED (why a parked train is not processed)
	/* (2) clone / unit numbers: "cloned as 5 and 6 instead of 3 and 4". */
	if (n >= 4 && std::strncmp(fmt, "UNIT", 4) == 0) return true;   ///< UNIT-NEW/-PARK/-RESTORE*/-FREEZE/-RELBK
	if (n >= 5 && std::strncmp(fmt, "CLONE", 5) == 0) return true;  ///< CLONE-MARKERS/-ORD/-TRAIT(S)
	/* (3) orders / shared order lists: the "shared schedule" half of KI-470. */
	if (n >= 3 && std::strncmp(fmt, "ORD", 3) == 0) return true;    ///< ORD-XFER/-IDX-WATCH/-RETURN-SHARED/-RETURN-ORPHAN
	if (n >= 4 && std::strncmp(fmt, "CTRL", 4) == 0) return true;   ///< CTRL-PARK/-RECOVER/-UNBORROW
	if (n >= 5 && std::strncmp(fmt, "AUTO-", 5) == 0) return true;  ///< AUTO-ORD* (KI-400 auto ranges)
	if (n >= 5 && std::strncmp(fmt, "OWNER", 5) == 0) return true;  ///< OWNER-MOVE (schedule ownership hand-over)
	if (n >= 4 && std::strncmp(fmt, "NAME", 4) == 0) return true;   ///< NAME-XFER/-RESTORE* (the clone/borrow counterpart of UNIT)
	/* (4) what the couple gate / segment bookkeeping looks at. */
	if (n >= 7 && std::strncmp(fmt, "RESERVE", 7) == 0) return true; ///< RESERVECONSIST (per-consist reservation state)
	if (n >= 8 && std::strncmp(fmt, "DECOUPLE", 8) == 0) return true; ///< DECOUPLE-INTERLOCK/-SEG-SKIP-REAL-ARTIC
	if (n >= 3 && std::strncmp(fmt, "SPD", 3) == 0) return true;      ///< SPD-DROP (KI-348: who zeroed the speed)
	/* R3R (第 371 轮, 2026-10-29, KI-534): the round-369/370 waypoint-wait probes
	 * (WP-LOCO-WAIT / WP-EXIT / WP-WAITHOLD / WP-HANDLE / WP-WAIT-KEEP) were all
	 * written through R3RDbgWrite and therefore swallowed right here: the 第 370 轮
	 * log had 0 "WP-" lines although the exe demonstrably contained the format
	 * strings, and that 0 was mis-read as "the wait block is unreachable". This is
	 * the third time the trap fires (after 第 274 轮 and 第 296 轮 above). Always
	 * check a new tag against this predicate before trusting a 0 count. */
	if (n >= 2 && std::strncmp(fmt, "WP", 2) == 0) return true;      ///< WP-LOCO-WAIT/-EXIT (KI-534 waypoint wait)
	if (n >= 8 && std::strncmp(fmt, "DUALHEAD", 8) == 0) return true; ///< DUALHEAD (KI-334)
	if (n >= 3 && std::strncmp(fmt, "TTB", 3) == 0) return true;      ///< TTB-PROBE (train-train boundary geometry)
	if (n >= 5 && std::strncmp(fmt, "DEPOT", 5) == 0) return true;    ///< DEPOT-XFER-ID/-BEFORE-HEAD*/-INSEG-SKIP
	if (n >= 5 && std::strncmp(fmt, "SEGID", 5) == 0) return true;    ///< SEGID-KEEP (segment id reconciliation)
	if (n >= 6 && std::strncmp(fmt, "SEGROW", 6) == 0) return true;   ///< SEGROW-RECONCILE
	if (n >= 6 && std::strncmp(fmt, "LEGACY", 6) == 0) return true;   ///< LEGACY-ARTIC-DEARTIC (read-game migration)
	if (n >= 4 && std::strncmp(fmt, "CGRP", 4) == 0) return true;     ///< CGRP-NORM/-CLOSE (couple groups)
	if (n >= 3 && std::strncmp(fmt, "GRP", 3) == 0) return true;      ///< GRP-NORM/-PARK/-RESTORE
	if (n >= 4 && std::strncmp(fmt, "R3R-", 4) == 0) return true;     ///< R3R-CLONE-GROUP-MISMATCH / R3R-FROZEN-*
	return false;
#else
	return false;
#endif
}

#if R3R_PROBES
inline void R3RDbgWrite(const char *fmt, ...)
{
	if (!R3RDbgOn()) return;
	if (!R3RDbgTagAllowed(fmt)) return;
	R3RPerfCounters &p = R3RP();
	p.dbg_writes++;
	const uint64_t t0 = R3RPerfNowNs();
	FILE *dbg = fopen(R3RDbgFileName(), "a");
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
