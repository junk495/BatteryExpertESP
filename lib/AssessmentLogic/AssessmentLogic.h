#pragma once
// Aging-assessment helpers — a pure, hardware-independent port of the Android
// app's com.batteryexpert.data.assessment.AssessmentLogic (computeDefaults).
//
// Keine Arduino-/Hardware-Abhängigkeit -> nativ unit-testbar.

namespace assessment {

// The four test currents the Android app stores per cell, derived from the
// nominal capacity. fast = quick screen (1 C), slow = accurate reference test.
struct TestCurrents {
    int fastChargeMa = 0;
    int fastDischargeMa = 0;
    int slowChargeMa = 0;
    int slowDischargeMa = 0;
};

// Mirrors AssessmentLogic.computeDefaults():
//   fastCharge    = min(capacity, 5000)   // 1 C, MC5000 charge limit 5 A
//   fastDischarge = min(capacity, 2000)   // 1 C, MC5000 discharge limit 2 A
//   slowCharge    = round(capacity * 0.5)
//   slowDischarge = round(capacity * 0.2)
TestCurrents computeTestCurrents(int nominalCapacityMah);

} // namespace assessment
