//! Authoritative game state, independent of networking.
//!
//! The network layer feeds validated input commands in; `GameWorld::tick`
//! advances the simulation by exactly one fixed step.

use tfps_protocol::snapshot::{entity_flags, quantize_mm, EntityState};
use tfps_sim::{buttons, movement, simulate, CollisionWorld, MapDef, MoveInput, MoveState};

/// Ring buffer size for queued commands (power of two).
const CMD_RING: usize = 32;
/// Commands further ahead than this are refused.
pub const MAX_CMD_AHEAD: u32 = 16;
/// If more commands than this are queued, the oldest are dropped so input
/// latency stays bounded.
pub const MAX_CMD_BUFFERED: u32 = 6;
/// After this many consecutive starved ticks the repeated command loses its
/// movement buttons (a player whose connection stalls stops instead of
/// running on).
pub const STARVATION_HOLD_TICKS: u32 = 8;

#[derive(Clone, Copy, Default)]
struct CmdSlot {
    seq: u32,
    valid: bool,
    cmd: MoveInput,
}

/// Per-player input command queue implementing the "one command per tick"
/// rule (see PROTOCOL.md, Input body).
pub struct CommandQueue {
    ring: [CmdSlot; CMD_RING],
    /// Next sequence number to simulate. `None` until the first command.
    next_seq: Option<u32>,
    newest_seq: u32,
    pub last_cmd: MoveInput,
    pub last_processed_seq: u32,
    /// Statistics for diagnostics / cheat heuristics.
    pub starved_ticks: u64,
    pub dropped_cmds: u64,
    /// Set when the client sent commands too far in the future since the last
    /// `take` (its clock runs ahead): reported so the client slows down.
    pub too_far_ahead: bool,
    ahead_pending: bool,
    consecutive_starved: u32,
}

impl Default for CommandQueue {
    fn default() -> Self {
        Self {
            ring: [CmdSlot::default(); CMD_RING],
            next_seq: None,
            newest_seq: 0,
            last_cmd: MoveInput::default(),
            last_processed_seq: 0,
            starved_ticks: 0,
            dropped_cmds: 0,
            too_far_ahead: false,
            ahead_pending: false,
            consecutive_starved: 0,
        }
    }
}

impl CommandQueue {
    /// Adds a command received from the network. Duplicates (from input
    /// redundancy), stale and absurdly-far-ahead commands are ignored.
    pub fn push(&mut self, seq: u32, mut cmd: MoveInput) {
        cmd.buttons &= buttons::ALL;
        if seq == 0 {
            return;
        }
        match self.next_seq {
            None => self.next_seq = Some(seq),
            Some(next) => {
                if seq < next {
                    return;
                }
                if seq >= next.saturating_add(MAX_CMD_AHEAD) {
                    self.ahead_pending = true;
                    return;
                }
            }
        }
        let slot = &mut self.ring[seq as usize % CMD_RING];
        if slot.valid && slot.seq == seq {
            return;
        }
        *slot = CmdSlot {
            seq,
            valid: true,
            cmd,
        };
        if seq > self.newest_seq {
            self.newest_seq = seq;
        }
    }

    /// Number of commands queued beyond the last processed one.
    pub fn depth(&self) -> u32 {
        match self.next_seq {
            Some(next) if self.newest_seq >= next => self.newest_seq - next + 1,
            _ => 0,
        }
    }

    /// Takes the command to simulate this tick. Returns `(cmd, starved)`.
    pub fn take(&mut self) -> (MoveInput, bool) {
        self.too_far_ahead = std::mem::take(&mut self.ahead_pending);
        let Some(mut next) = self.next_seq else {
            return (self.last_cmd, true);
        };
        // Bound latency: if too many commands are queued, skip the oldest.
        if self.newest_seq >= next && self.newest_seq - next + 1 > MAX_CMD_BUFFERED {
            let new_next = self.newest_seq + 1 - MAX_CMD_BUFFERED;
            for s in next..new_next {
                let slot = &mut self.ring[s as usize % CMD_RING];
                if slot.valid && slot.seq == s {
                    slot.valid = false;
                }
                self.dropped_cmds += 1;
            }
            next = new_next;
        }
        let slot = &mut self.ring[next as usize % CMD_RING];
        let starved = !(slot.valid && slot.seq == next);
        if !starved {
            self.last_cmd = slot.cmd;
            slot.valid = false;
            self.consecutive_starved = 0;
        } else {
            // Missing: repeat the previous command and skip this sequence so
            // the client can never bank simulation time.
            self.starved_ticks += 1;
            self.consecutive_starved += 1;
            if self.consecutive_starved > STARVATION_HOLD_TICKS {
                self.last_cmd.buttons = 0;
            }
        }
        self.last_processed_seq = next;
        self.next_seq = Some(next + 1);
        (self.last_cmd, starved)
    }
}

pub struct Player {
    pub id: u16,
    pub team: u8,
    pub name: String,
    pub state: MoveState,
    pub cmds: CommandQueue,
    pub health: u8,
    /// Last tick's starvation, reported to the client for time sync.
    pub last_starved: bool,
}

impl Player {
    pub fn entity_state(&self) -> EntityState {
        let mut flags = entity_flags::ALIVE;
        if self.state.on_ground {
            flags |= entity_flags::ON_GROUND;
        }
        if self.state.crouched {
            flags |= entity_flags::CROUCHED;
        }
        let horiz2 = self.state.vel.x * self.state.vel.x + self.state.vel.y * self.state.vel.y;
        if horiz2 > movement::RUN_SPEED * movement::RUN_SPEED * 1.05 {
            flags |= entity_flags::SPRINTING;
        }
        EntityState {
            id: self.id,
            pos_mm: [
                quantize_mm(self.state.pos.x),
                quantize_mm(self.state.pos.y),
                quantize_mm(self.state.pos.z),
            ],
            yaw_q: self.cmds.last_cmd.yaw_q,
            pitch_q: self.cmds.last_cmd.pitch_q,
            flags,
            health: self.health,
        }
    }

    /// Input buffer depth as reported in snapshots (-1 = starved this tick).
    pub fn reported_depth(&self) -> i8 {
        if self.cmds.too_far_ahead {
            i8::MAX
        } else if self.last_starved {
            -1
        } else {
            self.cmds.depth().min(127) as i8
        }
    }
}

pub struct GameWorld {
    pub map: MapDef,
    pub collision: CollisionWorld,
    pub dt: f32,
    pub tick: u32,
    /// Sorted by id.
    players: Vec<Player>,
    spawn_cursor: usize,
}

impl GameWorld {
    pub fn new(map: MapDef, tick_rate: u16) -> Self {
        let collision = CollisionWorld::from_map(&map);
        Self {
            map,
            collision,
            dt: tfps_sim::tick_dt(tick_rate),
            tick: 0,
            players: Vec::new(),
            spawn_cursor: 0,
        }
    }

    pub fn players(&self) -> &[Player] {
        &self.players
    }

    pub fn player(&self, id: u16) -> Option<&Player> {
        self.players
            .binary_search_by_key(&id, |p| p.id)
            .ok()
            .map(|i| &self.players[i])
    }

    pub fn player_mut(&mut self, id: u16) -> Option<&mut Player> {
        match self.players.binary_search_by_key(&id, |p| p.id) {
            Ok(i) => Some(&mut self.players[i]),
            Err(_) => None,
        }
    }

    /// Picks a spawn point for `team`, rotating through the team's spawns
    /// and skipping occupied ones.
    pub fn spawn_state(&mut self, team: u8) -> MoveState {
        let spawns: Vec<_> = self
            .map
            .spawns
            .iter()
            .filter(|s| s.team == team)
            .cloned()
            .collect();
        let list = if spawns.is_empty() {
            self.map.spawns.clone()
        } else {
            spawns
        };
        for i in 0..list.len() {
            let sp = &list[(self.spawn_cursor + i) % list.len()];
            let candidate = MoveState::at(sp.pos);
            let blocked = self
                .players
                .iter()
                .any(|p| p.state.bounds().overlaps(&candidate.bounds()));
            if !blocked {
                self.spawn_cursor = self.spawn_cursor.wrapping_add(i + 1);
                return candidate;
            }
        }
        self.spawn_cursor = self.spawn_cursor.wrapping_add(1);
        MoveState::at(list[self.spawn_cursor % list.len()].pos)
    }

    pub fn spawn_yaw_q(&self, state: &MoveState) -> u16 {
        self.map
            .spawns
            .iter()
            .find(|s| s.pos == state.pos)
            .map(|s| tfps_sim::math::quantize_yaw_deg(s.yaw_deg))
            .unwrap_or(0)
    }

    /// Inserts a player (new or restored after reconnect).
    pub fn add_player(&mut self, id: u16, team: u8, name: String, state: Option<MoveState>) {
        let state = state.unwrap_or_else(|| self.spawn_state(team));
        let yaw_q = self.spawn_yaw_q(&state);
        let mut cmds = CommandQueue::default();
        cmds.last_cmd.yaw_q = yaw_q;
        let p = Player {
            id,
            team,
            name,
            state,
            cmds,
            health: 100,
            last_starved: false,
        };
        match self.players.binary_search_by_key(&id, |p| p.id) {
            Ok(i) => self.players[i] = p,
            Err(i) => self.players.insert(i, p),
        }
    }

    pub fn remove_player(&mut self, id: u16) -> Option<Player> {
        match self.players.binary_search_by_key(&id, |p| p.id) {
            Ok(i) => Some(self.players.remove(i)),
            Err(_) => None,
        }
    }

    /// Advances the world by one fixed step.
    pub fn tick(&mut self) {
        self.tick = self.tick.wrapping_add(1);
        let dt = self.dt;
        let kill_z = self.map.kill_z;
        let mut respawn = Vec::new();
        for p in &mut self.players {
            let (cmd, starved) = p.cmds.take();
            p.last_starved = starved;
            simulate(&mut p.state, &cmd, dt, &self.collision);
            if p.state.pos.z < kill_z {
                respawn.push(p.id);
            }
        }
        for id in respawn {
            let team = self.player(id).map(|p| p.team).unwrap_or(0);
            let s = self.spawn_state(team);
            if let Some(p) = self.player_mut(id) {
                p.state = s;
            }
        }
    }

    /// Fills `out` with the replicated state of all players (sorted by id).
    pub fn collect_entities(&self, out: &mut Vec<EntityState>) {
        out.clear();
        out.extend(self.players.iter().map(|p| p.entity_state()));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cmd(b: u16) -> MoveInput {
        MoveInput {
            buttons: b,
            yaw_q: 0,
            pitch_q: 0,
        }
    }

    #[test]
    fn queue_processes_in_order_and_ignores_duplicates() {
        let mut q = CommandQueue::default();
        q.push(10, cmd(1));
        q.push(11, cmd(2));
        q.push(10, cmd(99)); // duplicate (redundant resend)
        q.push(12, cmd(3));
        assert_eq!(q.take(), (cmd(1), false));
        assert_eq!(q.take(), (cmd(2), false));
        assert_eq!(q.take(), (cmd(3), false));
        assert_eq!(q.last_processed_seq, 12);
        // starve: repeats last command, skips seq 13
        assert_eq!(q.take(), (cmd(3), true));
        q.push(13, cmd(7)); // arrives late: ignored
        q.push(14, cmd(8));
        assert_eq!(q.take(), (cmd(8), false));
        // Long stall: movement stops after the hold-over period.
        for _ in 0..STARVATION_HOLD_TICKS {
            assert_eq!(q.take(), (cmd(8), true));
        }
        assert_eq!(q.take(), (cmd(0), true));
    }

    #[test]
    fn flooding_commands_does_not_speed_up_simulation() {
        // A speed-hacking client sends 10 commands per server tick.
        let mut q = CommandQueue::default();
        let mut seq = 1;
        let mut processed = 0;
        for _ in 0..100 {
            for _ in 0..10 {
                q.push(seq, cmd(1));
                seq += 1;
            }
            q.take();
            processed += 1;
            assert!(q.depth() <= MAX_CMD_AHEAD);
        }
        assert_eq!(processed, 100);
        assert!(q.dropped_cmds > 0);
    }

    #[test]
    fn strips_unknown_buttons_and_ignores_far_future() {
        let mut q = CommandQueue::default();
        q.push(1, cmd(0xFFFF));
        q.push(1000, cmd(1));
        assert_eq!(q.take().0.buttons, buttons::ALL);
        assert_eq!(q.depth(), 0);
    }

    fn test_map() -> MapDef {
        MapDef::from_json(
            r#"{"name":"t","kill_z":-10,"solids":[{"min":[-10,-10,-1],"max":[10,10,0],"material":"concrete"}],
                "spawns":[{"pos":[0,0,0],"team":0},{"pos":[3,0,0],"team":1}]}"#,
        )
        .unwrap()
    }

    #[test]
    fn players_move_from_inputs_and_respawn_below_kill_z() {
        let mut w = GameWorld::new(test_map(), 64);
        w.add_player(1, 0, "a".into(), None);
        w.add_player(2, 1, "b".into(), None);
        for s in 1..=64 {
            w.player_mut(1).unwrap().cmds.push(s, cmd(buttons::FORWARD));
            w.tick();
        }
        assert!(w.player(1).unwrap().state.pos.x > 2.0);
        assert_eq!(w.player(2).unwrap().state.pos.x, 3.0);

        // Walk off the edge -> fall -> respawn
        for s in 65..64 * 12 {
            w.player_mut(1)
                .unwrap()
                .cmds
                .push(s, cmd(buttons::FORWARD | buttons::SPRINT));
            w.tick();
        }
        let p = w.player(1).unwrap();
        assert!(
            p.state.pos.z > -10.0 && p.state.pos.x < 10.5,
            "{:?}",
            p.state
        );
        let mut ents = Vec::new();
        w.collect_entities(&mut ents);
        assert_eq!(ents.len(), 2);
        assert_eq!(ents[0].id, 1);
    }
}
