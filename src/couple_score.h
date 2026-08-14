/*
 * This file is part of OpenTTD (adapted for the couple-score feature).
 * Programmable scoring rules for coupling candidate selection (R6).
 */

#ifndef COUPLE_SCORE_H
#define COUPLE_SCORE_H

#include "order_type.h"
#include <vector>

struct Train;

/** A programmable scoring rule for coupling candidate selection (R6). */
struct CoupleScoreRule {
	OrderConditionVariable variable = OrderConditionVariable::Unconditionally; ///< Which property of the candidate consist to inspect.
	OrderConditionComparator comparator = OrderConditionComparator::Equal;     ///< How to compare the property against the threshold.
	int threshold = 0;                                                         ///< Comparison threshold.
	int weight = 0;                                                            ///< Weight added to the score when the rule is satisfied (may be negative).
};

/** Global programmable rule table (default ruleset, overridable per-vehicle later). */
extern std::vector<CoupleScoreRule> _couple_score_rules;
/** Minimum score a candidate must reach to be coupled onto. */
extern int _couple_min_score;

/**
 * Evaluate the score of a candidate consist against the programmable rules.
 * @param candidate The candidate consist (front vehicle).
 * @return The summed weight of all satisfied rules.
 */
int EvaluateCoupleScore(const Train *candidate);

/**
 * Reset the rule table to the built-in default ruleset (R5 speed-class matching).
 */
void ResetCoupleScoreRulesToDefault();

#endif /* COUPLE_SCORE_H */
