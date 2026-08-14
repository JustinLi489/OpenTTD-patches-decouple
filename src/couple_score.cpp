/*
 * This file is part of OpenTTD (adapted for the couple-score feature).
 * Programmable scoring engine for coupling candidate selection (R6).
 */

#include "stdafx.h"
#include "couple_score.h"

#include "train.h"
#include "vehicle_func.h"
#include "order_func.h"
#include "date_func.h"

/* Built-in default ruleset: R5 speed-class matching.
 * A consist with max speed >= 160 km/h scores +10, one below 120 km/h scores -20,
 * so a 160 km/h locomotive prefers a 160 km/h consist over a 120 km/h one. */
std::vector<CoupleScoreRule> _couple_score_rules = {
	{ OrderConditionVariable::MaxSpeed, OrderConditionComparator::MoreThanOrEqual, 160, 10 },
	{ OrderConditionVariable::MaxSpeed, OrderConditionComparator::LessThan,        120, -20 },
};
int _couple_min_score = 0;

/**
 * Reset the rule table to the built-in default ruleset.
 */
void ResetCoupleScoreRulesToDefault()
{
	_couple_score_rules = {
		{ OrderConditionVariable::MaxSpeed, OrderConditionComparator::MoreThanOrEqual, 160, 10 },
		{ OrderConditionVariable::MaxSpeed, OrderConditionComparator::LessThan,        120, -20 },
	};
	_couple_min_score = 0;
}

/**
 * Evaluate a single rule against a candidate consist.
 * @param rule The rule to evaluate.
 * @param candidate The candidate consist (front vehicle).
 * @return Whether the rule is satisfied.
 */
static bool EvaluateCoupleRule(const CoupleScoreRule &rule, const Train *candidate)
{
	int value = 0;
	switch (rule.variable) {
		case OrderConditionVariable::MaxSpeed:      value = candidate->GetDisplayMaxSpeed() * 10 / 16; break;
		case OrderConditionVariable::LoadPercentage: value = CalcPercentVehicleFilled(candidate, nullptr); break;
		case OrderConditionVariable::Age:           value = DateDeltaToYearDelta(candidate->age).base(); break;
		case OrderConditionVariable::Unconditionally: return true;
		default: return false; /* Variables not meaningful for a consist are never satisfied. */
	}
	return OrderConditionCompare(rule.comparator, value, rule.threshold);
}

/**
 * Evaluate the score of a candidate consist against the programmable rules.
 * @param candidate The candidate consist (front vehicle).
 * @return The summed weight of all satisfied rules.
 */
int EvaluateCoupleScore(const Train *candidate)
{
	int score = 0;
	for (const CoupleScoreRule &rule : _couple_score_rules) {
		if (EvaluateCoupleRule(rule, candidate)) score += rule.weight;
	}
	return score;
}
