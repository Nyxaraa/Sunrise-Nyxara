#include <array>
#include <cassert>
#include "../Sunrise/src/server/activity/mission/mission_script_squad_sense.h"

int main() {
    using sunrise::middleware::bap::activity_message::sense_update::DecodedValue;
    using sunrise::server::activity::mission::read_squad_alive;

    using namespace sunrise::server::activity::mission;
    std::array<std::int32_t, 8> consumed{};
    std::array<DecodedValue, 4> accounting{};
    accounting[0].schemaRow = 0x80807ECF;
    accounting[0].unsignedValue = 2;
    accounting[1].schemaRow = accounting[2].schemaRow = 0x80809491;
    accounting[1].signedValue = 1;
    accounting[2].signedValue = 3;
    accounting[2].occurrence = 1;
    // A cost value or unrelated field with the same wire width is not accounting.
    accounting[3].schemaRow = 0x80807ECD;
    accounting[3].width = 32;
    accounting[3].signedValue = 999;
    assert(read_squad_consumed_counts(accounting, consumed) == 2);
    assert(consumed[0] == 1 && consumed[1] == 3 && consumed[2] == 0);
    accounting[2].present = false;
    assert(read_squad_consumed_counts(accounting, consumed) == 0);
    assert(consumed[1] == 3); // Partial/absent accounting must retain the prior level.
    accounting[2].present = true;
    accounting[2].signedValue = -1;
    assert(read_squad_consumed_counts(accounting, consumed) == 0);

    SquadObjectiveCosts costs{};
    std::array<DecodedValue,2> report{};
    report[0].schemaRow=0x80807ECD; report[0].occurrence=2; report[0].realValue=120;
    report[1].schemaRow=7; report[1].fieldOrdinal=1; report[1].signedValue=1;
    assert(update_squad_objective_costs(costs,report,7));
    assert(costs.revision==1 && costs.known==4 && costs.values[2]==120);
    assert(!update_squad_objective_costs(costs,report,7));
    report[0].present=false; report[0].realValue=0;
    assert(!update_squad_objective_costs(costs,report,7) && costs.values[2]==120);
    report[0].present=true; report[0].occurrence=24;
    assert(!update_squad_objective_costs(costs,report,7));
    report[0].occurrence=2; report[0].realValue=2040;
    assert(update_squad_objective_costs(costs,report,7) && costs.values[2]==2040);
    std::array<DecodedValue, 2> body{};
    body[0].schemaRow = 7;
    body[0].fieldOrdinal = 3;
    body[0].present = false;
    std::int32_t alive = 5;
    assert(!read_squad_alive({}, 7, alive) && alive == 5);
    assert(!read_squad_alive(body, 7, alive) && alive == 5);
    body[1].schemaRow = 8; // A present nested field cannot supply the root count.
    body[1].fieldOrdinal = 3;
    body[1].present = true;
    assert(!read_squad_alive(body, 7, alive) && alive == 5);
    body[0].present = true;
    body[0].signedValue = 4;
    assert(read_squad_alive(body, 7, alive) && alive == 4);
    body[0].present = false;
    body[0].signedValue = 0;
    assert(!read_squad_alive(body, 7, alive) && alive == 4);
    body[0].present = true;
    assert(read_squad_alive(body, 7, alive) && alive == 0);
    body[0].signedValue = -1;
    assert(!read_squad_alive(body, 7, alive));
    body[0].signedValue = 64;
    assert(!read_squad_alive(body, 7, alive));
}
