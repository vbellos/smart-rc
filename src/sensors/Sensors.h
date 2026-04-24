#pragma once

// Extension point for future sensors (battery monitor, IMU, ultrasonic,
// line follower, etc.). Intentionally empty today — kept as a separate
// module so adding a sensor doesn't pull churn into network/portal/motor
// code.
//
// Suggested API once sensors land:
//   namespace smartrc::sensors {
//       void begin();
//       void update();                 // call from loop()
//       void appendStatusJson(JsonObject& out);  // contributes to /api/status
//   }

namespace smartrc {
namespace sensors {

void begin();
void update();

}  // namespace sensors
}  // namespace smartrc
