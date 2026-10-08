// shop.h — the player's gear: sneakers (permanent battle stat tiers) and energy
// drinks (one-use, picked on the pre-battle screen). Owns the SHOP and PRE-BATTLE
// screens (draw + tap handling) and turns gear into battle::Stats. Wallet stays in
// game_state; screens get a pointer to it. Inventory is saved in the "profile" NVS.
#pragma once
#include <LovyanGFX.hpp>
#include <Preferences.h>
#include <stdint.h>
#include "battle.h"
#include "input.h"

namespace shop {

enum Item : uint8_t { Sprint, Spring, Grip, Rush, Guard, Surge, ItemCount };
constexpr uint8_t SNEAKER_TIERS = 3;

void load(Preferences& p);           // read inventory (call once at boot)
void save(Preferences& p);           // write inventory (mode changes only)
uint8_t owned(Item i);               // sneaker tier 0..3, or drinks carried 0..DRINK_MAX
const char* name(Item i);

// SHOP screen. update() returns true when BACK is tapped.
void openShop();
bool updateShop(const input::State& in, uint32_t& wallet, float dt);
void composeShop(lgfx::LGFX_Sprite& band, int32_t bandY);

// PRE-BATTLE screen (only shown when the player carries a drink). Tap drinks to
// toggle one of each for this battle. update(): 0 = still choosing, 1 = FIGHT, 2 = LEAVE.
void openPreBattle(const char* enemyName, uint8_t level);
int  updatePreBattle(const input::State& in, float dt);
void composePreBattle(lgfx::LGFX_Sprite& band, int32_t bandY);
bool hasDrinks();

// Stats for the next battle: sneakers + the drinks chosen on the pre-battle screen
// (those drinks are consumed here). guardShield = 1 if GUARD was used.
battle::Stats takeBattleStats(uint8_t& guardShield);

// Drink drop for a DEFEATED enemy, rolled BEFORE the battle so the end screen can
// name it: returns a drink Item (DRINK_DROP_PCT chance, only kinds below max) or -1.
// Nothing is granted until grantDrink() is called on a win by defeat.
int8_t rollDefeatDrop();
void   grantDrink(int8_t item);

void setWalletView(const uint32_t* wallet);   // for the wallet readout on both screens

}
