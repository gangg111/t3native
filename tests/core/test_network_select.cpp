// Native: NETWORK MODE's character select (GameFlow.guest_unlocked): the grid shows both players'
// characters, and each player confirms only their own; outside a session the select is the game's.
#include "content/fight_content.h"
#include "content/flow_data.h"
#include "core/fight/rule_set.h"
#include "core/game/character_select.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "platform/pad_state.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

gd::Ref<GameFlow> new_flow() {
    static gd::Ref<FightContent> content;
    static gd::Ref<FlowData> data;
    if (!content) {
        content = FightContent::load_from(t3test::paths().imported);
        data = FlowData::load_from(t3test::imported("tables/flow.json"));
    }
    return gd::make<GameFlow>(content, data, RuleSet::original(), 1);
}

bool grid_has(const gd::Ref<GameFlow>& flow, gd::Int key) {
    for (gd::Int c = 0; c < CharacterSelect::CELL_COUNT; ++c)
        if (flow->select->cell_key(c) == key) return true;
    return false;
}

// Source of the masks: chosen for the test. Player 1 has characters 0-9; player 2 also 12.
constexpr gd::Int HOST_MASK = 0x3FF;
constexpr gd::Int GUEST_KEY = 12;
constexpr gd::Int GUEST_MASK = HOST_MASK | (gd::Int(1) << GUEST_KEY);

}  // namespace

T3_TEST(network_select, the_grid_shows_both_players_characters) {
    if (!t.require(t3test::imported("tables/flow.json"), "the converted assets")) return;
    gd::Ref<GameFlow> flow = new_flow();
    flow->progress->unlocked = HOST_MASK;
    flow->select->grid_build();
    t.expect(!grid_has(flow, GUEST_KEY), "outside a session: player 1's characters only");
    flow->network_session = true;
    flow->guest_unlocked = GUEST_MASK;
    flow->select->grid_build();
    t.expect(grid_has(flow, GUEST_KEY), "in a session: player 2's character too");
    t.expect(grid_has(flow, 0), "and player 1's");
}

T3_TEST(network_select, each_player_confirms_only_their_own) {
    if (!t.require(t3test::imported("tables/flow.json"), "the converted assets")) return;
    gd::Ref<GameFlow> flow = new_flow();
    flow->progress->unlocked = HOST_MASK;
    flow->network_session = true;
    flow->guest_unlocked = GUEST_MASK;
    CharacterSelect& select = *flow->select;
    t.expect(!select._owns(0, GUEST_KEY), "player 1 does not have player 2's character");
    t.expect(select._owns(1, GUEST_KEY), "player 2 has it");
    t.expect(select._owns(0, 3) && select._owns(1, 3), "both have a shared one");
    t.expect(select._owns(0, CharacterSelect::NO_CELL), "an empty cell is nobody's to refuse");
    // The confirm of player 1 on it is refused while the select's timer runs: still choosing.
    select.ctx->put32(0x14, 100);
    select.w32(0, CharacterSelect::R_KEY, GUEST_KEY);
    t.expect(select._choose(0, PadState::CROSS, 0), "player 1's confirm refused");
    // Outside a session nothing is refused (the game's own select).
    flow->network_session = false;
    t.expect(select._owns(0, GUEST_KEY), "outside a session the game's rule");
}
