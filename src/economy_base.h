/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file economy_base.h Base classes related to the economy. */

#ifndef ECONOMY_BASE_H
#define ECONOMY_BASE_H

#include "cargopacket.h"
#include "economy_type.h"

/** Type of pool to store cargo payments in; little over 1 million. */
using CargoPaymentPool = Pool<CargoPayment, CargoPaymentID, 512>;
/** The actual pool to store cargo payments in. */
extern CargoPaymentPool _cargo_payment_pool;

/**
 * Helper class to perform the cargo payment.
 */
struct CargoPayment : CargoPaymentPool::PoolItem<&_cargo_payment_pool> {
	/* CargoPaymentID index member of CargoPaymentPool is 4 bytes. */
	StationID current_station = StationID::Invalid(); ///< NOSAVE: The current station

	Vehicle *front = nullptr; ///< The front vehicle to do the payment of
	Money route_profit = 0; ///< The amount of money to add/remove from the bank account
	Money visual_profit = 0; ///< The visual profit to show
	Money visual_transfer = 0; ///< The transfer credits to be shown

	/* R3R (KI-71): per-segment payment accounting. NOSAVE -- these live only for
	 * the (un)loading cycle of one station call and never change the money the
	 * company receives; they only decide on which vehicle's books a payment ends
	 * up. Without a booked segment (ordinary trains, chains without segment
	 * markers) everything goes to 'front', exactly as before. */
	VehicleID r3r_recipient = VehicleID::Invalid(); ///< NOSAVE: segment head currently receiving payments
	Money r3r_recipient_base = 0; ///< NOSAVE: visual payments at the time the current recipient was set
	Money r3r_booked = 0; ///< NOSAVE: visual payments already credited to segment heads

	/* R3R (P3): the company whose vehicle is being (un)loaded right now. In a
	 * coupled chain spanning several companies that is not necessarily the chain
	 * front's owner, and then the money earned for that leg has to be handed over
	 * to this company instead of being banked on the chain front. */
	CompanyID r3r_payee_company = CompanyID::Invalid(); ///< NOSAVE: company earning the payments made right now

	CargoPayment(CargoPaymentID index) : PoolItemBase(index) {}
	CargoPayment(CargoPaymentID index, Vehicle *front);
	~CargoPayment();

	Money PayTransfer(CargoType cargo, CargoPacket *cp, uint count, TileIndex current_tile);
	void PayFinalDelivery(CargoType cargo, CargoPacket *cp, uint count, TileIndex current_tile);

	/**
	 * R3R (KI-71): route the cargo payments made from now on to the segment that
	 * @p v belongs to. Pass nullptr to close the scope; everything paid while a
	 * scope is open is credited to that segment's head immediately.
	 */
	void R3RSetPaymentRecipient(Vehicle *v);

	/**
	 * R3R (P3): the company that has to receive the money earned by the cargo
	 * handled right now. Inside a coupled chain spanning several companies this
	 * is the owner of the vehicle currently being (un)loaded; for an ordinary
	 * train, or for a chain owned by a single company, it is the chain front's
	 * owner, so nothing changes for those.
	 * @return The company to pay.
	 */
	CompanyID R3RGetPayeeCompany() const;
};

#endif /* ECONOMY_BASE_H */
