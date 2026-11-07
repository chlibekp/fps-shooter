//! Authoritative game state, independent of networking.
//!
//! The network layer feeds validated input commands in; `GameWorld::tick`
//! advances the simulation by exactly one fixed step.

use crate::combat::{self, BodyPart, Hitboxes, LagHistory};
use crate::destruction::{self, Destruction, Obstacle};
use crate::gadgets::{self, phase, GadgetEntity};
use tfps_protocol::snapshot::{
    entity_flags, quantize_lean, quantize_mm, quantize_unit, surface, EntityState, ShotEvent,
};
use tfps_sim::gadget::{GadgetKind, GadgetSet, GadgetStock};
use tfps_sim::weapon::{
    is_gadget_slot, Shot, ShotBuffer, WeaponDef, WeaponSet, WeaponState, GADGET_SLOTS, SLOTS,
};
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
    /// Gadgets in the two gadget slots and how many are left.
    pub gadgets: [GadgetStock; GADGET_SLOTS],
    /// Seconds until the next gadget use is accepted.
    pub gadget_cooldown: f32,
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
            weapon: self.held_item(),
            aim_q: quantize_unit(self.state.aim),
        }
    }

    /// Replicated held item: weapon id, or `0x80 | gadget id` for a gadget.
    pub fn held_item(&self) -> u8 {
        let a = self.weapon.active as usize;
        if a < SLOTS {
            self.loadout[a]
        } else {
            0x80 | self.gadgets[a - SLOTS].id
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
    /// A flashbang went off; `intensity` (0..=255) per affected player.
    Flash {
        pos: Vec3,
        /// (player id, intensity), only players with intensity > 0.
        victims: Vec<(u16, u8)>,
    },
}

pub struct GameWorld {
    pub map: MapDef,
    pub collision: CollisionWorld,
    pub weapons: WeaponSet,
    pub gadget_defs: GadgetSet,
    /// Live gadget entities, sorted by id.
    pub gadgets: Vec<GadgetEntity>,
    next_gadget_id: u16,
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
    destruction: Destruction,
    trace_buf: Vec<Obstacle>,
    /// Explosions this tick (centre, radius), for effects.
    pub explosions: Vec<(Vec3, f32)>,
}

impl GameWorld {
    pub fn new(
        map: MapDef,
        weapons: WeaponSet,
        gadget_defs: GadgetSet,
        rules: MatchRules,
        tick_rate: u16,
    ) -> Self {
        let collision = CollisionWorld::from_map(&map);
        let destruction = Destruction::new(&collision);
        // One second of history (enough for any accepted rewind).
        let history = LagHistory::new(tick_rate as usize + 8);
        Self {
            map,
            collision,
            weapons,
            gadget_defs,
            gadgets: Vec::new(),
            next_gadget_id: 1,
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
            destruction,
            trace_buf: Vec::with_capacity(64),
            explosions: Vec::new(),
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
        restore: Option<(MoveState, WeaponState, u8, [GadgetStock; GADGET_SLOTS])>,
    ) {
        let loadout = self.default_loadout();
        let (state, weapon, health, gadget_stock) = match restore {
            Some(r) => r,
            None => {
                let spawn = self.spawn_state(team);
                let weapon = WeaponState::new(&self.loadout_defs(&loadout));
                (
                    spawn,
                    weapon,
                    self.rules.max_health,
                    self.gadget_defs.loadout(team),
                )
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
            gadgets: gadget_stock,
            gadget_cooldown: 0.0,
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
        let stock = self.gadget_defs.loadout(team);
        let p = &mut self.players[idx];
        p.state = s;
        p.weapon = w;
        p.gadgets = stock;
        p.gadget_cooldown = 0.0;
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
        self.explosions.clear();
        self.destruction.destroyed_this_tick.clear();
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
            let trigger_was = p.weapon.trigger_prev;
            p.gadget_cooldown = (p.gadget_cooldown - dt).max(0.0);
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
            let pressed = cmd.buttons & buttons::FIRE != 0 && !trigger_was;
            if pressed && is_gadget_slot(p.weapon.active) && p.weapon.equip_timer <= 0.0 {
                self.use_gadget(i, &cmd);
            }
            if self.players[i].state.pos.z < kill_z {
                self.respawn(i);
            }
        }

        // 2. Resolve shots against the rewound world.
        let shots = std::mem::take(&mut self.pending_shots);
        for (shooter, render_time, shot) in &shots {
            self.resolve_shot(*shooter, *render_time, shot);
        }
        self.pending_shots = shots;

        // 3. Gadgets: flight, fuses, arming, smoke.
        self.update_gadgets();

        // 4. Record this tick for future rewinds.
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
        let t_rewind = self.rewind_tick(render_time);

        // Every player the ray would hit (against rewound state), nearest first.
        let mut player_hits: Vec<(f32, BodyPart, u16)> = Vec::new();
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
            if let Some((t, part)) = Hitboxes::from_entity(&past).raycast(origin, dir, def.range) {
                player_hits.push((t, part, p.id));
            }
        }
        player_hits.sort_by(|a, b| a.0.total_cmp(&b.0));

        // Walk the obstacles (current destruction state) in order: damage the
        // cells crossed, penetrate while the bullet has power left.
        let mut obs = std::mem::take(&mut self.trace_buf);
        destruction::trace(&self.collision, origin, dir, def.range, &mut obs);
        let mut power = def.penetration;
        let mut dmg_mult = 1.0f32;
        let mut blocked_at: Option<(f32, u8)> = None;
        let mut victim: Option<(f32, BodyPart, u16, f32)> = None;
        for ob in &obs {
            if let Some(&(t, part, id)) = player_hits.first() {
                if t < ob.t_in {
                    victim = Some((t, part, id, dmg_mult));
                    break;
                }
            }
            let pr = destruction::props(ob.material);
            if let Some(cell) = ob.cell {
                let amount =
                    def.damage_at(ob.t_in) * dmg_mult * pr.bullet_mult * def.pellets_damage_scale();
                self.destruction
                    .damage_cell(&mut self.collision, cell, amount);
            }
            let cost = (ob.t_out - ob.t_in) * pr.resistance;
            if !cost.is_finite() || cost > power {
                blocked_at = Some((ob.t_in, ob.material as u8));
                break;
            }
            power -= cost;
            dmg_mult *= (1.0 - 0.6 * cost / def.penetration).max(0.2);
        }
        if victim.is_none() && blocked_at.is_none() {
            if let Some(&(t, part, id)) = player_hits.first() {
                victim = Some((t, part, id, dmg_mult));
            }
        }
        self.trace_buf = obs;

        let (end_t, surface_kind) = match (victim, blocked_at) {
            (Some((t, _, _, _)), _) => (t, surface::PLAYER),
            (None, Some((t, m))) => (t, m),
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

        let Some((t, part, victim_id, mult_through)) = victim else {
            return;
        };
        let mult = match part {
            BodyPart::Head => def.head_mult,
            BodyPart::Torso => 1.0,
            BodyPart::Limb => def.limb_mult,
        };
        let raw = def.damage_at(t) * mult * mult_through * self.rules.damage_multiplier;
        self.apply_damage(
            victim_id,
            Some(shooter_id),
            raw,
            part == BodyPart::Head,
            def.id,
            shooter_pos,
        );
    }

    /// Applies damage and emits Hit/Kill events. `attacker` None = world.
    fn apply_damage(
        &mut self,
        victim_id: u16,
        attacker: Option<u16>,
        raw: f32,
        headshot: bool,
        weapon: u8,
        from: Vec3,
    ) {
        let damage = raw.round().clamp(1.0, 255.0) as u8;
        let respawn_delay = self.rules.respawn_delay;
        let Some(victim) = self.player_mut(victim_id) else {
            return;
        };
        if !victim.alive {
            return;
        }
        let killed = damage >= victim.health;
        victim.health = victim.health.saturating_sub(damage);
        if killed {
            victim.alive = false;
            victim.health = 0;
            victim.respawn_timer = respawn_delay;
            victim.deaths += 1;
            victim.state.vel = Vec3::ZERO;
        }
        let shooter = attacker.unwrap_or(victim_id);
        self.events.push(GameEvent::Hit {
            shooter,
            victim: victim_id,
            damage,
            headshot,
            killed,
            shooter_pos: from,
        });
        if killed {
            if let Some(a) = attacker.filter(|a| *a != victim_id) {
                if let Some(s) = self.player_mut(a) {
                    s.kills += 1;
                }
            }
            self.events.push(GameEvent::Kill {
                killer: shooter,
                victim: victim_id,
                weapon,
                headshot,
            });
        }
    }

    /// Explosion at `center` (frag grenade, breaching charge...): damages
    /// players with linear falloff (halved per destructible layer in between,
    /// blocked by indestructible geometry) and breaks destructible cells.
    pub fn explode(
        &mut self,
        center: Vec3,
        radius: f32,
        damage: f32,
        cell_damage: f32,
        source: Option<u16>,
        weapon: u8,
    ) {
        let mut targets: Vec<(u16, f32)> = Vec::new();
        let mut obs = std::mem::take(&mut self.trace_buf);
        let source_team = source.and_then(|id| self.player(id)).map(|p| p.team);
        for p in &self.players {
            if !p.alive {
                continue;
            }
            if !self.rules.friendly_fire && source_team == Some(p.team) && source != Some(p.id) {
                continue;
            }
            let chest = Vec3::new(
                p.state.pos.x,
                p.state.pos.y,
                p.state.pos.z + p.state.height() * 0.55,
            );
            let d = chest.distance(&center);
            if d >= radius {
                continue;
            }
            let mut amount = damage * (1.0 - d / radius);
            if d > 1e-3 {
                let dir = Vec3::new(
                    (chest.x - center.x) / d,
                    (chest.y - center.y) / d,
                    (chest.z - center.z) / d,
                );
                destruction::trace(&self.collision, center, dir, d, &mut obs);
                for ob in obs.iter().filter(|o| o.t_in > 1e-3) {
                    if ob.cell.is_none() {
                        amount = 0.0;
                        break;
                    }
                    amount *= 0.5;
                }
            }
            if amount >= 1.0 {
                targets.push((p.id, amount * self.rules.damage_multiplier));
            }
        }
        self.trace_buf = obs;
        for (id, amount) in targets {
            self.apply_damage(id, source, amount, false, weapon, center);
        }
        self.destruction
            .explode_cells(&mut self.collision, center, radius, cell_damage);
        self.explosions.push((center, radius));
    }

    /// Handles a gadget use (FIRE pressed with a gadget in hand) by player `i`.
    fn use_gadget(&mut self, i: usize, cmd: &MoveInput) {
        let p = &self.players[i];
        let slot = p.weapon.active as usize - SLOTS;
        let stock = p.gadgets[slot];
        let Some(def) = self.gadget_defs.get(stock.id).cloned() else {
            return;
        };
        let eye = p.state.eye_position(cmd.yaw_q);
        let dir = combat::direction(
            tfps_sim::math::dequantize_yaw_deg(cmd.yaw_q),
            tfps_sim::math::dequantize_pitch_deg(cmd.pitch_q),
        );
        let (owner, team, vel) = (p.id, p.team, p.state.vel);
        let aiming = cmd.buttons & buttons::AIM != 0;

        // Detonating placed charges does not consume a charge or need stock.
        if def.kind == GadgetKind::Breach && aiming {
            let ready: Vec<u16> = self
                .gadgets
                .iter()
                .filter(|g| g.owner == owner && g.def == def.id && g.phase == phase::ARMED)
                .map(|g| g.id)
                .collect();
            for id in ready {
                self.detonate(id);
            }
            return;
        }
        if stock.count == 0
            || p.gadget_cooldown > 0.0
            || self.gadgets.len() >= gadgets::MAX_GADGET_ENTITIES
        {
            return;
        }
        let entity = if def.kind.is_thrown() {
            let speed = def.throw_speed * if aiming { gadgets::LOB_SPEED_MULT } else { 1.0 };
            GadgetEntity {
                id: 0,
                def: def.id,
                owner,
                team,
                pos: gadgets::throw_origin(&self.collision, eye, dir),
                vel: Vec3::new(
                    dir.x * speed + vel.x,
                    dir.y * speed + vel.y,
                    dir.z * speed + vel.z.max(0.0),
                ),
                phase: phase::FLYING,
                timer: def.fuse,
                attached_cell: None,
            }
        } else {
            let Some((pos, cell)) = gadgets::placement(&self.collision, eye, dir, def.reach) else {
                return; // nothing within reach to place it on
            };
            GadgetEntity {
                id: 0,
                def: def.id,
                owner,
                team,
                pos,
                vel: Vec3::ZERO,
                phase: phase::PLANTED,
                timer: def.arm_time,
                attached_cell: cell,
            }
        };
        let p = &mut self.players[i];
        p.gadgets[slot].count -= 1;
        p.gadget_cooldown = gadgets::USE_COOLDOWN;
        self.spawn_gadget(entity);
    }

    fn spawn_gadget(&mut self, mut g: GadgetEntity) {
        // Ids wrap at u16 but skip 0 and ids still in use.
        loop {
            let id = self.next_gadget_id;
            self.next_gadget_id = self.next_gadget_id.wrapping_add(1).max(1);
            if !self.gadgets.iter().any(|o| o.id == id) {
                g.id = id;
                break;
            }
        }
        let at = self.gadgets.partition_point(|o| o.id < g.id);
        self.gadgets.insert(at, g);
    }

    fn update_gadgets(&mut self) {
        let dt = self.dt;
        let kill_z = self.map.kill_z;
        let mut due: Vec<u16> = Vec::new();
        let mut gone: Vec<u16> = Vec::new();
        for g in self.gadgets.iter_mut() {
            match g.phase {
                phase::FLYING | phase::RESTING => {
                    gadgets::step_projectile(&self.collision, g, dt);
                    g.timer -= dt;
                    if g.timer <= 0.0 {
                        due.push(g.id);
                    }
                }
                phase::PLANTED | phase::ARMED => {
                    if g.attached_cell
                        .is_some_and(|c| self.collision.is_destroyed(c))
                    {
                        gone.push(g.id); // knocked off: lost without exploding
                        continue;
                    }
                    if g.phase == phase::PLANTED {
                        g.timer -= dt;
                        if g.timer <= 0.0 {
                            g.phase = phase::ARMED;
                        }
                    }
                }
                phase::SMOKING => {
                    g.timer -= dt;
                    if g.timer <= 0.0 {
                        gone.push(g.id);
                    }
                }
                _ => {}
            }
            if g.pos.z < kill_z {
                gone.push(g.id);
            }
        }
        self.gadgets.retain(|g| !gone.contains(&g.id));
        for id in due {
            self.detonate(id);
        }
    }

    /// Fuse end / remote detonation of gadget `id`.
    fn detonate(&mut self, id: u16) {
        let Ok(idx) = self.gadgets.binary_search_by_key(&id, |g| g.id) else {
            return;
        };
        let g = self.gadgets[idx].clone();
        let Some(def) = self.gadget_defs.get(g.def).cloned() else {
            self.gadgets.remove(idx);
            return;
        };
        let weapon = 0x80 | def.id;
        match def.kind {
            GadgetKind::Smoke => {
                let s = &mut self.gadgets[idx];
                s.phase = phase::SMOKING;
                s.timer = def.duration;
                s.vel = Vec3::ZERO;
            }
            GadgetKind::Frag | GadgetKind::Breach => {
                self.gadgets.remove(idx);
                self.explode(
                    g.pos,
                    def.radius,
                    def.damage,
                    def.cell_damage,
                    Some(g.owner),
                    weapon,
                );
            }
            GadgetKind::Flash => {
                self.gadgets.remove(idx);
                let victims = self.flash_victims(g.pos, def.radius);
                self.events.push(GameEvent::Flash {
                    pos: g.pos,
                    victims,
                });
            }
        }
    }

    /// Flash intensity per player: falls off with distance, stronger when
    /// looking towards the flash, zero without line of sight (glass lets it
    /// through).
    fn flash_victims(&mut self, pos: Vec3, radius: f32) -> Vec<(u16, u8)> {
        let mut out = Vec::new();
        let mut obs = std::mem::take(&mut self.trace_buf);
        for p in &self.players {
            if !p.alive {
                continue;
            }
            let eye = p.state.eye_position(p.cmds.last_cmd.yaw_q);
            let d = eye.distance(&pos);
            if d >= radius {
                continue;
            }
            if d > 1e-3 {
                let to = Vec3::new(
                    (pos.x - eye.x) / d,
                    (pos.y - eye.y) / d,
                    (pos.z - eye.z) / d,
                );
                destruction::trace(&self.collision, eye, to, d, &mut obs);
                let blocked = obs
                    .iter()
                    .any(|o| o.t_in < d - 0.05 && o.material != tfps_sim::Material::Glass);
                if blocked {
                    continue;
                }
                let view = combat::direction(
                    tfps_sim::math::dequantize_yaw_deg(p.cmds.last_cmd.yaw_q),
                    tfps_sim::math::dequantize_pitch_deg(p.cmds.last_cmd.pitch_q),
                );
                let facing = (view.x * to.x + view.y * to.y + view.z * to.z).max(0.0);
                let k = (1.0 - d / radius) * (0.3 + 0.7 * facing);
                let v = (k * 1.6).min(1.0);
                if v > 0.02 {
                    out.push((p.id, (v * 255.0).round() as u8));
                }
            } else {
                out.push((p.id, 255));
            }
        }
        self.trace_buf = obs;
        out
    }

    /// Cells destroyed during the last tick (to replicate).
    pub fn destroyed_this_tick(&self) -> &[u32] {
        &self.destruction.destroyed_this_tick
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
    fn gadget_defs() -> GadgetSet {
        GadgetSet::load(concat!(
            env!("CARGO_MANIFEST_DIR"),
            "/../Shared/Definitions/gadgets.json"
        ))
        .unwrap()
    }

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
            let mut w = GameWorld::new(test_map(extra), weapons(), gadget_defs(), rules, 64);
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

    #[test]
    fn bullets_penetrate_drywall_with_reduced_damage() {
        let drywall = r#",{"min":[10,-5,0],"max":[10.1,5,3],"material":"drywall"}"#;
        let mut a = Arena::new(drywall, MatchRules::default());
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), cmd(0), 32);
        a.step(
            aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.2),
            cmd(0),
            1,
        );
        let hp = a.hp(2);
        assert!(
            hp < 100 && hp > 64,
            "damage through drywall must be reduced: hp {hp}"
        );
        // The drywall cell was damaged (45 hp, rifle does 36 at 10 m): a second
        // shot along the same line destroys it and reports the cell.
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), cmd(0), 16);
        a.step(
            aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.2),
            cmd(0),
            1,
        );
        assert!(
            !a.w.destroyed_this_tick().is_empty(),
            "drywall cell should break"
        );
    }

    #[test]
    fn concrete_stops_bullets_and_is_not_destroyed() {
        let concrete = r#",{"min":[10,-5,0],"max":[10.2,5,3],"material":"concrete"}"#;
        let mut a = Arena::new(concrete, MatchRules::default());
        a.step(aim_at(buttons::AIM, 20.0, 0.0, 1.2), cmd(0), 32);
        for _ in 0..20 {
            a.step(
                aim_at(buttons::AIM | buttons::FIRE, 20.0, 0.0, 1.2),
                cmd(0),
                6,
            );
            assert!(a.w.destroyed_this_tick().is_empty());
        }
        assert_eq!(a.hp(2), 100);
    }

    #[test]
    fn explosions_damage_players_and_breach_walls() {
        let wall = r#",{"min":[18,-3,0],"max":[18.2,3,3],"material":"wood"},{"min":[3,-3,0],"max":[3.3,3,3],"material":"concrete"}"#;
        let mut a = Arena::new(wall, MatchRules::default());
        // Blast next to the target (x=20) on the other side of a wooden wall.
        a.w.explode(Vec3::new(18.0, 0.0, 1.0), 3.0, 150.0, 400.0, Some(1), 0);
        assert!(
            !a.w.destroyed_this_tick().is_empty(),
            "wood wall must be breached"
        );
        let hp = a.hp(2);
        assert!(hp < 100, "target near the blast must be hurt");
        assert!(a.w.events.iter().any(|e| matches!(
            e,
            GameEvent::Hit {
                shooter: 1,
                victim: 2,
                ..
            }
        )));
        // The shooter at x=0 is behind concrete and out of range: unhurt.
        assert_eq!(a.hp(1), 100);
        // Point-blank second blast kills; kill credited to the source.
        a.w.explode(Vec3::new(20.0, 0.0, 1.0), 3.0, 150.0, 400.0, Some(1), 0);
        assert!(!a.w.player(2).unwrap().alive);
        assert_eq!(a.w.player(1).unwrap().kills, 1);
    }

    fn slot_cmd(c: MoveInput, slot: u8) -> MoveInput {
        MoveInput { slot, ..c }
    }

    #[test]
    fn frag_grenade_is_thrown_bounces_and_kills_near_its_rest_point() {
        let mut a = Arena::new("", MatchRules::default());
        // Equip the frag (team 0, gadget slot 0 = input slot 2).
        let hold = slot_cmd(aim_at(0, 10.0, 0.0, 4.0), 2);
        a.step(hold, cmd(0), 30);
        assert_eq!(a.w.player(1).unwrap().held_item(), 0x81);
        let throw = slot_cmd(aim_at(buttons::FIRE, 10.0, 0.0, 4.0), 2);
        a.step(throw, cmd(0), 1);
        assert_eq!(a.w.gadgets.len(), 1, "grenade spawned");
        assert_eq!(a.w.player(1).unwrap().gadgets[0].count, 1);
        assert!(a.w.shot_events.is_empty(), "the weapon is holstered");
        // Let it land and roll, then put the target next to it.
        a.step(hold, cmd(0), 64 * 2);
        let g = a.w.gadgets[0].clone();
        assert_eq!(g.phase, phase::RESTING, "{g:?}");
        assert!(g.pos.x > 5.0 && g.pos.x < 29.0, "{g:?}");
        a.w.player_mut(2).unwrap().state.pos = Vec3::new(g.pos.x + 1.0, g.pos.y, 0.0);
        let mut killed = false;
        for _ in 0..64 {
            a.step(hold, cmd(0), 1);
            killed |= a.w.events.iter().any(|e| {
                matches!(
                    e,
                    GameEvent::Kill {
                        killer: 1,
                        victim: 2,
                        weapon: 0x81,
                        ..
                    }
                )
            });
        }
        assert!(killed, "frag at 1 m must kill");
        assert!(a.w.gadgets.is_empty());
        assert_eq!(a.hp(1), 100, "thrower far away is unhurt");
    }

    #[test]
    fn gadget_stock_is_limited_and_restocked_on_respawn() {
        let mut a = Arena::new("", MatchRules::default());
        let hold = slot_cmd(aim_at(0, 10.0, 0.0, 3.0), 2);
        let throw = slot_cmd(aim_at(buttons::FIRE, 10.0, 0.0, 3.0), 2);
        a.step(hold, cmd(0), 30);
        for _ in 0..4 {
            a.step(throw, cmd(0), 1);
            a.step(hold, cmd(0), 60); // release + cooldown
        }
        assert_eq!(a.w.player(1).unwrap().gadgets[0].count, 0);
        let thrown = a.w.gadgets.len() + a.w.explosions.len();
        assert!(thrown <= 2);
        // Weapon ammo untouched while the gadget was in hand.
        assert_eq!(a.w.player(1).unwrap().weapon.slots[0].ammo, 29);
        let idx = a.w.players.iter().position(|p| p.id == 1).unwrap();
        a.w.respawn(idx);
        assert_eq!(a.w.player(1).unwrap().gadgets[0].count, 2);
    }

    #[test]
    fn breach_charge_is_placed_armed_and_blows_a_hole_in_brick() {
        let wall = r#",{"min":[1.5,-3,0],"max":[1.7,3,3],"material":"brick"}"#;
        let mut a = Arena::new(wall, MatchRules::default());
        // Breach charge = team 0 gadget slot 1 = input slot 3.
        let hold = slot_cmd(aim_at(0, 1.5, 0.0, 1.2), 3);
        a.step(hold, cmd(0), 30);
        a.step(slot_cmd(aim_at(buttons::FIRE, 1.5, 0.0, 1.2), 3), cmd(0), 1);
        assert_eq!(a.w.gadgets.len(), 1);
        let charge = a.w.gadgets[0].clone();
        assert_eq!(charge.phase, phase::PLANTED);
        assert!(charge.attached_cell.is_some());
        assert!((charge.pos.x - 1.47).abs() < 0.01, "{charge:?}");
        assert_eq!(a.w.player(1).unwrap().gadgets[1].count, 2);
        // Not armed yet: detonation (AIM + FIRE) does nothing.
        let det = slot_cmd(aim_at(buttons::AIM | buttons::FIRE, 1.5, 0.0, 1.2), 3);
        let aimed = slot_cmd(aim_at(buttons::AIM, 1.5, 0.0, 1.2), 3);
        a.step(aimed, cmd(0), 1);
        a.step(det, cmd(0), 1);
        assert_eq!(a.w.gadgets.len(), 1);
        // Back off, wait for arming, detonate.
        a.w.player_mut(1).unwrap().state.pos = Vec3::new(-3.0, 0.0, 0.0);
        a.step(aimed, cmd(0), 64);
        assert_eq!(a.w.gadgets[0].phase, phase::ARMED);
        a.step(det, cmd(0), 1);
        assert!(a.w.gadgets.is_empty());
        assert!(
            a.w.destroyed_this_tick().len() >= 4,
            "brick must be breached: {}",
            a.w.destroyed_this_tick().len()
        );
        assert_eq!(a.w.explosions.len(), 1);
        assert_eq!(a.hp(1), 100);
        // Placing needs a surface within reach.
        a.step(hold, cmd(0), 60);
        a.step(
            slot_cmd(aim_at(buttons::FIRE, 10.0, 0.0, 1.65), 3),
            cmd(0),
            1,
        );
        assert!(a.w.gadgets.is_empty());
        assert_eq!(a.w.player(1).unwrap().gadgets[1].count, 2);
    }

    fn spawn_test_gadget(w: &mut GameWorld, def: u8, pos: Vec3) {
        w.spawn_gadget(GadgetEntity {
            id: 0,
            def,
            owner: 2,
            team: 1,
            pos,
            vel: Vec3::ZERO,
            phase: phase::RESTING,
            timer: 0.001,
            attached_cell: None,
        });
    }

    fn flash_of(w: &GameWorld) -> Vec<(u16, u8)> {
        w.events
            .iter()
            .find_map(|e| match e {
                GameEvent::Flash { victims, .. } => Some(victims.clone()),
                _ => None,
            })
            .expect("flash event")
    }

    #[test]
    fn flashbang_needs_line_of_sight_and_is_worse_when_facing_it() {
        let wall = r#",{"min":[14,-3,0],"max":[14.3,3,3],"material":"concrete"}"#;
        let mut a = Arena::new(wall, MatchRules::default());
        spawn_test_gadget(&mut a.w, 2, Vec3::new(6.0, 0.0, 0.1));
        a.step(cmd(0), cmd(0), 1);
        let v = flash_of(&a.w);
        let p1 = v.iter().find(|x| x.0 == 1).expect("player 1 sees it").1;
        assert!(p1 > 150, "facing, 6 m: {p1}");
        assert!(v.iter().all(|x| x.0 != 2), "player 2 is behind concrete");
        // Same distance, looking away: weaker.
        let away = MoveInput {
            yaw_q: quantize_yaw_deg(180.0),
            ..cmd(0)
        };
        a.step(away, cmd(0), 2);
        spawn_test_gadget(&mut a.w, 2, Vec3::new(6.0, 0.0, 0.1));
        a.step(away, cmd(0), 1);
        let back = flash_of(&a.w).iter().find(|x| x.0 == 1).unwrap().1;
        assert!(back < p1, "{back} vs {p1}");
    }

    #[test]
    fn smoke_grenade_emits_then_disappears() {
        let mut a = Arena::new("", MatchRules::default());
        spawn_test_gadget(&mut a.w, 3, Vec3::new(6.0, 0.0, 0.1));
        a.step(cmd(0), cmd(0), 1);
        assert_eq!(a.w.gadgets[0].phase, phase::SMOKING);
        a.step(cmd(0), cmd(0), 64 * 13);
        assert_eq!(a.w.gadgets.len(), 1);
        a.step(cmd(0), cmd(0), 64 * 2);
        assert!(a.w.gadgets.is_empty());
    }

    #[test]
    fn frag_respects_friendly_fire_rule() {
        let mut a = Arena::new("", MatchRules::default());
        a.w.add_player(3, 0, "mate".into(), None);
        a.w.player_mut(3).unwrap().state.pos = Vec3::new(10.0, 0.0, 0.0);
        a.w.explode(Vec3::new(10.0, 0.0, 0.5), 5.0, 150.0, 0.0, Some(1), 0x81);
        assert_eq!(a.w.player(3).unwrap().health, 100);
    }
}
