#include "AssessmentLogic.h"

namespace assessment {

TestCurrents computeTestCurrents(int nominalCapacityMah) {
    const int c = nominalCapacityMah > 0 ? nominalCapacityMah : 0;
    TestCurrents t;
    t.fastChargeMa    = c < 5000 ? c : 5000;        // 1 C, cap at 5 A
    t.fastDischargeMa = c < 2000 ? c : 2000;        // 1 C, cap at 2 A
    t.slowChargeMa    = (c + 1) / 2;                // round(0.5 * c)
    t.slowDischargeMa = (c * 2 + 5) / 10;           // round(0.2 * c)
    return t;
}

} // namespace assessment
