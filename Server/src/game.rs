//! Authoritative game state, independent of networking.
//!
//! The network layer feeds validated input commands in; `GameWorld::tick`
//! advances the simulation by exactly one fixed step.

use crate::combat::{self, BodyPart, Hitboxes, LagHistory};
use tfps_protocol::snapshot::{
    entity_flags, quantize_lean, quantize_mm, quantize_unit, surface, EntityState, ShotEvent,
};
use tfps_sim::weapon::{Shot, ShotBuffer, WeaponDef, WeaponSet, WeaponState, SLOTS};
use tfps_sim::{
    buttons, movement, simulate_player, CollisionWorld, MapDef, MoveInput, MoveState, Vec3,
};

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
    render_time: u32,
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
    pub fn push(&mut self, seq: u32, mut cmd: MoveInput, render_time: u32) {
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
            render_time,
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

    /// Takes the command to simulate this tick.
    /// Returns `(cmd, starved, render_time)`.
    pub fn take(&mut self) -> (MoveInput, bool, u32) {
        self.too_far_ahead = std::mem::take(&mut self.ahead_pending);
        let Some(mut next) = self.next_seq else {
            return (self.idle_cmd(), true, 0);
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
        let mut render_time = 0;
        if !starved {
            self.last_cmd = slot.cmd;
            render_time = slot.render_time;
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
        if starved {
            (self.idle_cmd(), true, 0)
        } else {
            (self.last_cmd, false, render_time)
        }
    }

    /// The command repeated when the real one is missing: movement and view
    /// continue, but a missing command never fires or reloads.
    fn idle_cmd(&self) -> MoveInput {
        let mut c = self.last_cmd;
        c.buttons &= !(buttons::FIRE | buttons::RELOAD);
        c
    }
}

/// Gameplay rules that the lobby can configure (Phase 6 adds the UI).
#[derive(Clone, Debug)]
pub struct MatchRules {
    pub max_health: u8,
    pub damage_multiplier: f32,
    pub friendly_fire: bool,
    pub respawn_delay: f32,
    /// Maximum time a shot may be rewound (lag compensation), seconds.
    pub max_rewind: f32,
}

impl Default for MatchRules {
    fn default() -> Self {
        Self {
            max_health: 100,
            damage_multiplier: 1.0,
            friendly_fire: false,
            respawn_delay: 5.0,
            max_rewind: 0.25,
        }
    }
}

pub struct Player {
    pub id: u16,
    pub team: u8,
    pub name: String,
    pub state: MoveState,
    pub weapon: WeaponState,
    pub loadout: [u8; SLOTS],
    pub cmds: CommandQueue,
    pub health: u8,
    pub alive: bool,
    pub respawn_timer: f32,
    pub kills: u32,
    pub deaths: u32,
    /// Last tick's starvation, reported to the client for time sync.
    pub last_starved: bool,
}

impl Player {
    pub fn entity_state(&self) -> EntityState {
        let mut flags = 0;
        if self.alive {
            flags |= entity_flags::ALIVE;
        }
        if self.state.on_ground {
            flags |= entity_flags::ON_GROUND;
        }
        if self.state.crouched {
            flags |= entity_flags::CROUCHED;
        }
        if self.state.prone {
            flags |= entity_flags::PRONE;
        }
        if self.weapon.is_reloading() {
            flags |= entity_flags::RELOADING;
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
            lean_q: quantize_lean(self.state.lean),
            weapon: self.loadout[self.weapon.active as usize],
            aim_q: quantize_unit(self.state.aim),
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

/// Things that happened during a tick, for the network layer to announce.
#[derive(Clone, Debug, PartialEq)]
pub enum GameEvent {
    Hit {
        shooter: u16,
        victim: u16,
        damage: u8,
        headshot: bool,
        killed: bool,
        shooter_pos: Vec3,
    },
    Kill {
        killer: u16,
        victim: u16,
        weapon: u8,
        headshot: bool,
    },
}

pub struct GameWorld {
    pub map: MapDef,
    pub collision: CollisionWorld,
    pub weapons: WeaponSet,
    pub rules: MatchRules,
    pub dt: f32,
    pub tick_rate: u16,
    pub tick: u32,
    /// Sorted by id.
    players: Vec<Player>,
    spawn_cursor: usize,
    history: LagHistory,
    /// Shots resolved this tick (for snapshots).
    pub shot_events: Vec<(u32, ShotEvent)>,
    /// Combat events produced this tick.
    pub events: Vec<GameEvent>,
    shot_buf: ShotBuffer,
    entity_buf: Vec<EntityState>,
    pending_shots: Vec<(u16, u32, Shot)>,
}

impl GameWorld {
    pub fn new(map: MapDef, weapons: WeaponSet, rules: MatchRules, tick_rate: u16) -> Self {
        let collision = CollisionWorld::from_map(&map);
        // One second of history (enough for any accepted rewind).
        let history = LagHistory::new(tick_rate as usize + 8);
        Self {
            map,
            collision,
            weapons,
            rules,
            dt: tfps_sim::tick_dt(tick_rate),
            tick_rate,
            tick: 0,
            players: Vec::new(),
            spawn_cursor: 0,
            history,
            shot_events: Vec::new(),
            events: Vec::new(),
            shot_buf: ShotBuffer::default(),
            entity_buf: Vec::with_capacity(64),
            pending_shots: Vec::with_capacity(64),
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

    fn loadout_defs(&self, loadout: &[u8; SLOTS]) -> [&WeaponDef; SLOTS] {
        [
            self.weapons.get(loadout[0]).expect("loadout validated"),
            self.weapons.get(loadout[1]).expect("loadout validated"),
        ]
    }

    pub fn default_loadout(&self) -> [u8; SLOTS] {
        self.weapons.default_loadout
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
                .any(|p| p.alive && p.state.bounds().overlaps(&candidate.bounds()));
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

    /// Inserts a player (new, or restored after reconnect with its state).
    pub fn add_player(
        &mut self,
        id: u16,
        team: u8,
        name: String,
        restore: Option<(MoveState, WeaponState, u8)>,
    ) {
        let loadout = self.default_loadout();
        let (state, weapon, health) = match restore {
            Some(r) => r,
            None => {
                let spawn = self.spawn_state(team);
                let weapon = WeaponState::new(&self.loadout_defs(&loadout));
                (spawn, weapon, self.rules.max_health)
            }
        };
        let yaw_q = self.spawn_yaw_q(&state);
        let mut cmds = CommandQueue::default();
        cmds.last_cmd.yaw_q = yaw_q;
        let p = Player {
            id,
            team,
            name,
            state,
            weapon,
            loadout,
            cmds,
            health,
            alive: health > 0,
            respawn_timer: if health > 0 {
                0.0
            } else {
                self.rules.respawn_delay
            },
            kills: 0,
            deaths: 0,
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

    fn respawn(&mut self, idx: usize) {
        let team = self.players[idx].team;
        let s = self.spawn_state(team);
        let yaw = self.spawn_yaw_q(&s);
        let loadout = self.players[idx].loadout;
        let w = WeaponState::new(&self.loadout_defs(&loadout));
        let max_health = self.rules.max_health;
        let p = &mut self.players[idx];
        p.state = s;
        p.weapon = w;
        p.health = max_health;
        p.alive = true;
        p.respawn_timer = 0.0;
        p.cmds.last_cmd.yaw_q = yaw;
    }

    /// Advances the world by one fixed step.
    pub fn tick(&mut self) {
        self.tick = self.tick.wrapping_add(1);
        self.shot_events.clear();
        self.events.clear();
        self.pending_shots.clear();
        let dt = self.dt;
        let kill_z = self.map.kill_z;

        // 1. Simulate every player from its command for this tick.
        for i in 0..self.players.len() {
            let (cmd, starved, render_time) = self.players[i].cmds.take();
            self.players[i].last_starved = starved;
            if !self.players[i].alive {
                self.players[i].respawn_timer -= dt;
                if self.players[i].respawn_timer <= 0.0 {
                    self.respawn(i);
                }
                continue;
            }
            let loadout = self.players[i].loadout;
            let defs = [
                self.weapons.get(loadout[0]).expect("loadout validated"),
                self.weapons.get(loadout[1]).expect("loadout validated"),
            ];
            self.shot_buf.clear();
            let p = &mut self.players[i];
            simulate_player(
                &mut p.state,
                &mut p.weapon,
                &cmd,
                dt,
                &self.collision,
                &defs,
                &mut self.shot_buf,
            );
            for s in self.shot_buf.as_slice() {
                self.pending_shots.push((p.id, render_time, *s));
            }
            if p.state.pos.z < kill_z {
                self.respawn(i);
            }
        }

        // 2. Resolve shots against the rewound world.
        let shots = std::mem::take(&mut self.pending_shots);
        for (shooter, render_time, shot) in &shots {
            self.resolve_shot(*shooter, *render_time, shot);
        }
        self.pending_shots = shots;

        // 3. Record this tick for future rewinds.
        let mut ents = std::mem::take(&mut self.entity_buf);
        self.collect_entities(&mut ents);
        self.history.record(self.tick, &ents);
        self.entity_buf = ents;
    }

    /// Converts a client render time (24.8 fixed-point ticks) into the
    /// fractional tick to rewind to, clamped to what the server accepts.
    fn rewind_tick(&self, render_time: u32) -> f64 {
        let now = self.tick as f64 - 1.0; // history holds up to the previous tick
        let max_back = self.rules.max_rewind as f64 * self.tick_rate as f64;
        let claimed = render_time as f64 / 256.0;
        claimed.clamp((now - max_back).max(0.0), now)
    }

    fn resolve_shot(&mut self, shooter_id: u16, render_time: u32, shot: &Shot) {
        let Some(shooter) = self.player(shooter_id) else {
            return;
        };
        if !shooter.alive {
            return;
        }
        let shooter_team = shooter.team;
        let shooter_pos = shooter.state.pos;
        let Some(def) = self.weapons.get(shot.weapon_id).cloned() else {
            return;
        };
        let dir = combat::direction(shot.yaw_deg, shot.pitch_deg);
        let origin = shot.origin;
        let world_hit = combat::raycast_world(&self.map, origin, dir, def.range);
        let max_t = world_hit.map(|(t, _)| t).unwrap_or(def.range);
        let t_rewind = self.rewind_tick(render_time);

        let mut best: Option<(f32, BodyPart, u16)> = None;
        for p in &self.players {
            if p.id == shooter_id
                || !p.alive
                || (!self.rules.friendly_fire && p.team == shooter_team)
            {
                continue;
            }
            let Some(past) = self.history.sample(p.id, t_rewind) else {
                continue;
            };
            if past.flags & entity_flags::ALIVE == 0 {
                continue;
            }
            if let Some((t, part)) = Hitboxes::from_entity(&past).raycast(origin, dir, max_t) {
                if best.is_none_or(|(bt, _, _)| t < bt) {
                    best = Some((t, part, p.id));
                }
            }
        }

        let (end_t, surface_kind) = match (best, world_hit) {
            (Some((t, _, _)), _) => (t, surface::PLAYER),
            (None, Some((t, m))) => (t, m as u8),
            (None, None) => (def.range, surface::NONE),
        };
        let end = combat::point_along(origin, dir, end_t);
        self.shot_events.push((
            self.tick,
            ShotEvent {
                shooter: shooter_id,
                weapon: def.id,
                end_mm: [quantize_mm(end.x), quantize_mm(end.y), quantize_mm(end.z)],
                surface: surface_kind,
            },
        ));

        let Some((t, part, victim_id)) = best else {
            return;
        };
        let mult = match part {
            BodyPart::Head => def.head_mult,
            BodyPart::Torso => 1.0,
            BodyPart::Limb => def.limb_mult,
        };
        let raw = def.damage_at(t) * mult * self.rules.damage_multiplier;
        let damage = raw.round().clamp(1.0, 255.0) as u8;
        let respawn_delay = self.rules.respawn_delay;
        let Some(victim) = self.player_mut(victim_id) else {
            return;
        };
        let killed = damage >= victim.health;
        victim.health = victim.health.saturating_sub(damage);
        if killed {
            victim.alive = false;
            victim.health = 0;
            victim.respawn_timer = respawn_delay;
            victim.deaths += 1;
            victim.state.vel = Vec3::ZERO;
        }
        let headshot = part == BodyPart::Head;
        self.events.push(GameEvent::Hit {
            shooter: shooter_id,
            victim: victim_id,
            damage,
            headshot,
            killed,
            shooter_pos,
        });
        if killed {
            if let Some(s) = self.player_mut(shooter_id) {
                s.kills += 1;
            }
            self.events.push(GameEvent::Kill {
                killer: shooter_id,
                victim: victim_id,
                weapon: def.id,
                headshot,
            });
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
    use tfps_sim::math::{quantize_pitch_deg, quantize_yaw_deg};

    fn cmd(b: u16) -> MoveInput {
        MoveInput {
            buttons: b,
            yaw_q: 0,
            pitch_q: 0,
            slot: 0,
        }
    }

    #[test]
    fn queue_processes_in_order_and_ignores_duplicates() {
        let mut q = CommandQueue::default();
        q.push(10, cmd(1), 100);
        q.push(11, cmd(2), 101);
        q.push(10, cmd(99), 0); // duplicate (redundant resend)
        q.push(12, cmd(3 | buttons::FIRE), 102);
        assert_eq!(q.take(), (cmd(1), false, 100));
        assert_eq!(q.take(), (cmd(2), false, 101));
        assert_eq!(q.take(), (cmd(3 | buttons::FIRE), false, 102));
        assert_eq!(q.last_processed_seq, 12);
        // Starved: repeats the last command without FIRE, skips seq 13.
        assert_eq!(q.take(), (cmd(3), true, 0));
        q.push(13, cmd(7), 0); // arrives late: ignored
        q.push(14, cmd(8), 0);
        assert_eq!(q.take(), (cmd(8), false, 0));
        for _ in 0..STARVATION_HOLD_TICKS {
            assert_eq!(q.take(), (cmd(8), true, 0));
        }
        assert_eq!(q.take(), (cmd(0), true, 0));
    }

    #[test]
    fn flooding_commands_does_not_speed_up_simulation() {
        let mut q = CommandQueue::default();
        let mut seq = 1;
        for _ in 0..100 {
            for _ in 0..10 {
                q.push(seq, cmd(1), 0);
                seq += 1;
            }
            q.take();
            assert!(q.depth() <= MAX_CMD_AHEAD);
        }
        assert!(q.dropped_cmds > 0);
    }

    #[test]
    fn strips_unknown_buttons_and_ignores_far_future() {
        let mut q = CommandQueue::default();
        q.push(1, cmd(0xFFFF), 0);
        q.push(1000, cmd(1), 0);
        assert_eq!(q.take().0.buttons, buttons::ALL);
        assert_eq!(q.depth(), 0);
    }

    fn weapons() -> WeaponSet {
        WeaponSet::load(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../Shared/Definitions/weapons.json"
        ))
        .unwrap()
    }

    /// Flat 40x40 m floor; team 0 spawns at x=0, team 1 at x=20 (facing -X).
    fn test_map(extra: &str) -> MapDef {
        MapDef::from_json(&format!(
            r#"{{"name":"t","kill_z":-10,"solids":[{{"min":[-20,-20,-1],"max":[30,20,0],"material":"concrete"}}{extra}],
                "spawns":[{{"pos":[0,0,0],"team":0}},{{"pos":[20,0,0],"team":1,"yaw":180}}]}}"#
        ))
        .unwrap()
    }

    struct Arena {
        w: GameWorld,
        seq: u32,
    }

    impl Arena {
        fn new(extra: &str, rules: MatchRules) -> Self {
            let mut w = GameWorld::new(test_map(extra), weapons(), rules, 64);
            w.add_player(1, 0, "shooter".into(), None);
            w.add_player(2, 1, "target".into(), None);
            let mut a = Arena { w, seq: 0 };
            a.step(cmd(0), cmd(0), 8);
            a
        }
        /// Runs `ticks` with the given commands; render time = current tick - 2.
        fn step(&mut self, c1: MoveInput, c2: MoveInput, ticks: usize) {
            for _ in 0..ticks {
                self.seq += 1;
                let rt = (self.w.tick.saturating_sub(2)) * 256;
                self.w.player_mut(1).unwrap().cmds.push(self.seq, c1, rt);
                self.w.player_mut(2).unwrap().cmds.push(self.seq, c2, rt);
                self.w.tick();
            }
        }
        fn step_with_render_time(&mut self, c1: MoveInput, c2: MoveInput, rt: u32) {
            self.seq += 1;
            self.w.player_mut(1).unwrap().cmds.push(self.seq, c1, rt);
            self.w.player_mut(2).unwrap().cmds.push(self.seq, c2, rt);
            self.w.tick();
        }
        fn hp(&self, id: u16) -> u8 {
            self.w.player(id).unwrap().health
        }
    }

    /// Aim from the shooter's eye (0,0,1.65) at a point.
    fn aim_at(buttons: u16, x: f32, y: f32, z: f32) -> MoveInput {
        let (dx, dy, dz) = (x, y, z - movement::STAND_EYE);
        let yaw = dy.atan2(dx).to_degrees();
        let pitch = dz.atan2((dx * dx + dy * dy).sqrt()).to_degrees();
        MoveInput {
            buttons,
            yaw_q: quantize_yaw_deg(yaw),
            pitch_q: quantize_pitch_deg(pitch),
            slot: 0,
        }
    }

    #[test]
    fn rifle_headshots_kill_and_produce_events() {
        let mut a = Arena::new("", MatchRules::default());
        // ADS for accuracy, then single taps at the head (1.66 m) 20 m away.
        let ads = aim_at(buttons::AIM, 20.0, 0.0, 1.66);
        a.step(ads, cmd(0), 32);
        let fire = aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.66);
        a.step(fire, cmd(0), 1);
        let events = a.w.events.clone();
        assert!(
            events.iter().any(|e| matches!(
                e,
                GameEvent::Hit {
                    victim: 2,
                    headshot: true,
                    killed: true,
                    ..
                }
            )),
            "{events:?}"
        );
        assert!(events.iter().any(|e| matches!(
            e,
            GameEvent::Kill {
                killer: 1,
                victim: 2,
                headshot: true,
                ..
            }
        )));
        assert_eq!(a.w.shot_events.len(), 1);
        assert_eq!(a.w.shot_events[0].1.surface, surface::PLAYER);
        let t = a.w.player(2).unwrap();
        assert!(!t.alive && t.health == 0 && t.deaths == 1);
        assert_eq!(a.w.player(1).unwrap().kills, 1);
        // Dead players do not move or shoot; they respawn after the delay.
        a.step(cmd(0), cmd(buttons::FORWARD | buttons::FIRE), 64 * 4);
        assert!(!a.w.player(2).unwrap().alive);
        a.step(cmd(0), cmd(0), 70);
        let t = a.w.player(2).unwrap();
        assert!(t.alive && t.health == 100);
        assert_eq!(t.weapon.slots[0].ammo, 29);
    }

    #[test]
    fn body_shots_deal_falloff_damage() {
        let mut a = Arena::new("", MatchRules::default());
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), cmd(0), 32);
        a.step(
            aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.2),
            cmd(0),
            1,
        );
        // AR-7 at 20 m: 36 damage (falloff starts at 25 m).
        assert_eq!(a.hp(2), 64);
    }

    #[test]
    fn walls_block_shots() {
        let wall = r#",{"min":[10,-5,0],"max":[10.3,5,3],"material":"brick"}"#;
        let mut a = Arena::new(wall, MatchRules::default());
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), cmd(0), 32);
        a.step(
            aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.2),
            cmd(0),
            1,
        );
        assert_eq!(a.hp(2), 100);
        let ev = a.w.shot_events[0].1;
        assert_eq!(ev.surface, tfps_sim::Material::Brick as u8);
        assert!((ev.end_mm[0] - 10_000).abs() < 5, "{ev:?}");
    }

    #[test]
    fn lag_compensation_rewinds_targets_within_limits() {
        let mut a = Arena::new("", MatchRules::default());
        // Target strafes along +Y for half a second.
        let strafe = MoveInput {
            buttons: buttons::LEFT,
            yaw_q: quantize_yaw_deg(180.0),
            pitch_q: 0,
            slot: 0,
        };
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), strafe, 32);
        let then_tick = a.w.tick;
        let then_y = a.w.player(2).unwrap().state.pos.y;
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), strafe, 8);
        let now_y = a.w.player(2).unwrap().state.pos.y;
        assert!(
            now_y - then_y > 0.3,
            "target must have moved: {then_y} -> {now_y}"
        );
        // Aim where the target WAS 8 ticks (125 ms) ago, claiming that render time: hit.
        let fire_old = aim_at(buttons::AIM | buttons::FIRE, 20.0, then_y, 1.2);
        a.step_with_render_time(fire_old, strafe, then_tick * 256);
        assert!(a.hp(2) < 100, "lag-compensated shot should hit");

        // Same trick with a render time far beyond the rewind limit is clamped: miss.
        let mut b = Arena::new("", MatchRules::default());
        b.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), strafe, 32);
        let old_tick = b.w.tick;
        let old_y = b.w.player(2).unwrap().state.pos.y;
        b.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), strafe, 40); // 625 ms later
        let fire_stale = aim_at(buttons::AIM | buttons::FIRE, 20.0, old_y, 1.2);
        b.step_with_render_time(fire_stale, strafe, old_tick * 256);
        assert_eq!(
            b.hp(2),
            100,
            "rewinds beyond max_rewind must not be honoured"
        );
    }

    #[test]
    fn friendly_fire_is_off_by_default() {
        let mut a = Arena::new("", MatchRules::default());
        a.w.player_mut(2).unwrap().team = 0;
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), cmd(0), 32);
        a.step(
            aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.2),
            cmd(0),
            1,
        );
        assert_eq!(a.hp(2), 100);
        let mut b = Arena::new(
            "",
            MatchRules {
                friendly_fire: true,
                ..Default::default()
            },
        );
        b.w.player_mut(2).unwrap().team = 0;
        b.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), cmd(0), 32);
        b.step(
            aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.2),
            cmd(0),
            1,
        );
        assert!(b.hp(2) < 100);
    }

    #[test]
    fn players_move_from_inputs_and_respawn_below_kill_z() {
        let mut a = Arena::new("", MatchRules::default());
        a.step(cmd(buttons::FORWARD), cmd(0), 64);
        assert!(a.w.player(1).unwrap().state.pos.x > 2.0);
        // Walk off the edge -> fall -> respawn.
        a.step(
            MoveInput {
                buttons: buttons::FORWARD,
                yaw_q: quantize_yaw_deg(180.0),
                pitch_q: 0,
                slot: 0,
            },
            cmd(0),
            64 * 12,
        );
        let p = a.w.player(1).unwrap();
        assert!(
            p.state.pos.z > -10.0 && p.state.pos.x > -20.5,
            "{:?}",
            p.state
        );
        let mut ents = Vec::new();
        a.w.collect_entities(&mut ents);
        assert_eq!(ents.len(), 2);
        assert_eq!(ents[0].weapon, 1);
    }
}
