/*
 * This file is part of OpenTTD (adapted for the R3R consist-group feature).
 * A consist (car-only formation) is represented as a zero-power locomotive:
 * physically a normal train front, with no power/speed until coupled onto.
 */

#ifndef CONSIST_GROUP_H
#define CONSIST_GROUP_H

#include "engine_type.h"
#include "vehicle_type.h"

struct Train;

/**
 * Create (or recreate, after a NewGRF config change) the dedicated consist-group
 * engine: a zero-power locomotive that backs every car-only consist.
 * @return The engine ID, or #EngineID::Invalid() on failure.
 */
EngineID CreateConsistGroupEngine();

/**
 * Find (or create) the dedicated consist-group engine.
 * @return The engine ID, or #EngineID::Invalid() on failure.
 */
EngineID GetConsistGroupEngine();

/**
 * Turn a car-only chain into a consist: give its front vehicle the zero-power
 * locomotive identity, so every OpenTTD subsystem treats it as a normal train.
 * @param front_wagon First vehicle of the real consist.
 * @return The consist front (now an engine), or nullptr on failure.
 */
Train *CreateConsistGroup(Train *front_wagon);

/**
 * Remove the consist (zero-power locomotive) identity from a chain front.
 * @param front The consist front.
 */
void DestroyConsistGroup(Train *front);

/**
 * Whether a train front is a consist (car-only formation with the zero-power
 * locomotive identity).
 * @param v The train front.
 * @return True when it is a consist.
 */
bool IsConsistGroup(const Train *v);

#endif /* CONSIST_GROUP_H */
