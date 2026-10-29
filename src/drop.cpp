// drop — the warploom-game proof binary.
//
// A tower of 20 simulated cubes drops onto the ground plane under fixed-step
// integration. Three claims, all checked here and all through the engine's
// public API only:
//
//   1. Replay reproduces: session A captures (spawn + steps), session B loads
//      the file fresh; the DOCUMENTS must be byte-identical.
//   2. Determinism is external: sessions C and D, set up identically and
//      ticked identically, hold bit-identical body positions.
//   3. The sim actually ran: the pile fell and rests on the plane (guards a
//      no-op sim passing claims 1 and 2 vacuously).
//
// The game IS the host (the same role the viewport and headless_host play):
// Step commands are executed as session.tick() calls here and then forwarded
// to the session, which acknowledges and records them. Calling
// session.on_control(Step) alone only acknowledges — the session never ticks
// itself. Bodies are attached with the public spawn_physics_body, which is
// setup (like scene loading), not an edit: it is deliberately outside the
// command log, so claim 1 compares documents, not whole snapshots.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <warploom/core/control_server.hpp>
#include <warploom/core/editor_session.hpp>
#include <warploom/physics/physics_world.hpp>

namespace {

  using CK = warploom::core::ControlCommand::Kind;
  namespace editor = warploom::editor;

  constexpr int kCubes = 20;
  constexpr std::uint64_t kSteps = 600;
  constexpr double kDt = 1.0 / 60.0;
  constexpr char kReplayPath[] = "drop-tower.replay";

  int g_failures = 0;

  void check (bool cond, const std::string& what) {
    if (!cond) {
      ++g_failures;
      std::printf ("FAIL: %s\n", what.c_str ());
    }
  }

  // The minimal host: owns the logical frame counter, executes Steps as
  // ticks, forwards everything else to the session (which records it).
  struct Host {
    editor::EditorSession session;
    std::uint64_t frame{ 0 };
    std::string error;

    bool tick_once () {
      const auto report = session.tick ({ frame, kDt, 0.0, false });
      if (!report.ok ())
        return false;
      ++frame;
      return true;
    }

    bool on_control (const warploom::core::ControlCommand& c) {
      if (c.kind == CK::Step) {
        const std::uint64_t n
            = c.number_count > 0U ? static_cast<std::uint64_t> (c.numbers[0]) : 1U;
        for (std::uint64_t i = 0; i < n; ++i) {
          if (!tick_once ())
            return false;
        }
      }
      const auto r = session.on_control (c);
      if (!r.ok) {
        error = r.error;
        return false;
      }
      return true;
    }

    bool step (std::uint64_t n) {
      warploom::core::ControlCommand c;
      c.kind = CK::Step;
      c.numbers[0] = static_cast<double> (n);
      c.number_count = 1;
      return on_control (c);
    }
  };

  // The ListObjects detail is {"objects":[{...,"id":N,"type":"cube",...}]}.
  // Scrape (id, is-cube) pairs without a JSON dependency: the keys are
  // engine-emitted, so the spelling is stable.
  std::vector<std::uint64_t> cube_ids (const std::string& detail) {
    std::vector<std::uint64_t> ids;
    std::size_t pos = 0;
    while ((pos = detail.find ("\"id\":", pos)) != std::string::npos) {
      pos += 5;
      const std::uint64_t id
          = static_cast<std::uint64_t> (std::strtoull (detail.c_str () + pos, nullptr, 10));
      // The object literal containing this id is a cube when `"type":"cube"`
      // appears before the next `"id":` (or end). A fixed 512-byte window
      // covers the object literal; names are short.
      const std::string window = detail.substr (pos, 512);
      const std::size_t type_at = window.find ("\"type\":\"cube\"");
      const std::size_t next_at = window.find ("\"id\":");
      if (type_at != std::string::npos && (next_at == std::string::npos || type_at < next_at)) {
        ids.push_back (id);
      }
    }
    return ids;
  }

  // Spawns the tower, projects once, and attaches a body per cube. Bodies are
  // setup, not edits: attached identically in every session that needs them.
  bool build_tower (Host& h, std::vector<std::uint64_t>& ids) {
    for (int i = 0; i < kCubes; ++i) {
      warploom::core::ControlCommand c;
      c.kind = CK::SpawnCube;
      c.numbers[0] = (i % 5) * 1.2;       // x: 5-wide grid
      c.numbers[1] = 2.0 + (i / 5) * 1.3; // y: 4 staggered layers
      c.numbers[2] = 0.0;
      c.numbers[3] = 1.0;
      c.number_count = 4;
      if (!h.on_control (c))
        return false;
    }
    if (!h.step (1))
      return false; // project before attaching
    warploom::core::ControlCommand list;
    list.kind = CK::ListObjects;
    const auto r = h.session.on_control (list);
    if (!r.ok)
      return false;
    ids = cube_ids (r.detail);
    if (ids.size () != static_cast<std::size_t> (kCubes))
      return false;
    for (std::size_t i = 0; i < ids.size (); ++i) {
      warploom::physics::PhysicsBody b;
      b.position[0] = static_cast<float> ((i % 5) * 1.2);
      b.position[1] = static_cast<float> (2.0 + (i / 5) * 1.3);
      b.position[2] = 0.0F;
      b.radius = 0.5F;
      b.inverse_mass = 1.0F; // dynamic
      b.restitution = 0.4F;
      if (!h.session.spawn_physics_body (ids[i], b))
        return false;
    }
    return true;
  }

  double max_height (const editor::EditorSession& s, const std::vector<std::uint64_t>& ids) {
    double m = 0.0;
    for (const auto id : ids) {
      const auto* b = s.physics_body_for (id);
      if (b != nullptr && b->position[1] > m)
        m = b->position[1];
    }
    return m;
  }

  double min_height (const editor::EditorSession& s, const std::vector<std::uint64_t>& ids) {
    double m = 1e9;
    for (const auto id : ids) {
      const auto* b = s.physics_body_for (id);
      if (b != nullptr && b->position[1] < m)
        m = b->position[1];
    }
    return m;
  }

} // namespace

int main () {
  // Claim 1: replay reproduces. A captures with bodies attached; B loads
  // fresh. Documents must match byte-for-byte (bodies are setup, outside
  // the log, so the comparison is documents, not whole snapshots).
  Host a;
  std::vector<std::uint64_t> ids_a;
  check (a.session.capture_start (0, "drop-tower", a.error), "capture_start: " + a.error);
  check (build_tower (a, ids_a), "build_tower(A): " + a.error);
  check (a.step (kSteps - 1), "step(A): " + a.error);
  check (a.session.capture_stop (kReplayPath, a.error), "capture_stop: " + a.error);
  const std::string doc_a = a.session.document ().to_json ();

  editor::EditorSession b;
  std::string b_error;
  check (b.load_replay (kReplayPath, b_error), "load_replay: " + b_error);
  check (b.document ().to_json () == doc_a, "replay reproduces the document");

  // Claim 2: determinism is external. Two identically built sessions,
  // identically ticked, hold bit-identical positions.
  Host c;
  Host d;
  std::vector<std::uint64_t> ids_c;
  std::vector<std::uint64_t> ids_d;
  check (build_tower (c, ids_c), "build_tower(C): " + c.error);
  check (build_tower (d, ids_d), "build_tower(D): " + d.error);
  check (c.step (kSteps - 1), "step(C): " + c.error);
  check (d.step (kSteps - 1), "step(D): " + d.error);
  bool positions_match = ids_c.size () == ids_d.size ();
  for (std::size_t i = 0; positions_match && i < ids_c.size (); ++i) {
    const auto* bc = c.session.physics_body_for (ids_c[i]);
    const auto* bd = d.session.physics_body_for (ids_d[i]);
    positions_match = bc != nullptr && bd != nullptr && bc->position[0] == bd->position[0]
                      && bc->position[1] == bd->position[1] && bc->position[2] == bd->position[2];
  }
  check (positions_match, "identical setups integrate bit-identically");

  // The capture must not perturb the sim: A's pile matches C's exactly.
  bool capture_clean = ids_a.size () == ids_c.size ();
  for (std::size_t i = 0; capture_clean && i < ids_a.size (); ++i) {
    const auto* ba = a.session.physics_body_for (ids_a[i]);
    const auto* bc = c.session.physics_body_for (ids_c[i]);
    capture_clean = ba != nullptr && bc != nullptr && ba->position[1] == bc->position[1];
  }
  check (capture_clean, "capturing does not perturb the simulation");

  // Claim 3: the sim actually ran — the pile fell and rests on the plane.
  const double top = max_height (a.session, ids_a);
  const double bottom = min_height (a.session, ids_a);
  std::printf ("pile: %d cubes, top y=%.3f (from %.1f), bottom y=%.3f\n", kCubes, top,
      2.0 + 3 * 1.3, bottom);
  check (top < 2.0 + 3 * 1.3, "the pile fell");
  check (bottom >= 0.3 && bottom <= 1.5, "the pile rests on the plane");

  if (g_failures == 0)
    std::printf ("DROP-OK\n");
  return g_failures == 0 ? 0 : 1;
}
