//! Match flow: game modes, rounds, objective (plant/defuse), side swaps and
//! per-round map resets.
//!
//! Players belong to a persistent *squad* (0 or 1) that owns the score. Their
//! `team` field is the *side* they play this round: 0 attackers, 1
//! defenders. In team deathmatch the sides never change; in round modes the
//! squads swap sides at half-time.

use super::{GameEvent, GameWorld};
use crate::destruction::Destruction;
use tfps_sim::{buttons, CollisionWorld, MoveInput, Vec3};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum GameMode {
    /// Respawning team deathmatch, first squad to `score_limit` kills.
    Deathmatch = 0,
    /// Rounds, no respawns, last side standing (defenders win on time).
    Elimination = 1,
    /// Rounds; attackers plant a device in an objective site, defenders
    /// defuse it (or eliminate the attackers / run the clock down).
    Defuse = 2,
}

impl GameMode {
    pub fn has_rounds(self) -> bool {
        !matches!(self, GameMode::Deathmatch)
    }

    pub fn parse(s: &str) -> Option<Self> {
        match s {
            "deathmatch" | "tdm" => Some(GameMode::Deathmatch),
            "elimination" => Some(GameMode::Elimination),
            "defuse" => Some(GameMode::Defuse),
            _ => None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum MatchPhase {
    /// Waiting for both sides to have players (respawns on, no score).
    Warmup = 0,
    /// Round modes: attackers hold position (drones allowed), weapons cold.
    Prep = 1,
    Action = 2,
    RoundEnd = 3,
    MatchOver = 4,
}

/// Why a round ended (replicated).
pub mod reason {
    pub const ELIMINATED: u8 = 1;
    pub const TIME_UP: u8 = 2;
    pub const DETONATED: u8 = 3;
    pub const DEFUSED: u8 = 4;
    pub const SCORE_LIMIT: u8 = 5;
}

/// Hold-USE actions shown to the player.
pub mod action {
    pub const NONE: u8 = 0;
    pub const REINFORCE: u8 = 1;
    pub const PLANT: u8 = 2;
    pub const DEFUSE: u8 = 3;
}

/// Planted device.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Device {
    pub site: u8,
    pub pos: Vec3,
    pub fuse_left: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct MatchState {
    pub phase: MatchPhase,
    /// 1-based round number (0 before the first round).
    pub round: u8,
    /// Score per squad (rounds won, or kills in deathmatch).
    pub score: [u8; 2],
    /// The squad currently playing attackers (side 0).
    pub attacking_squad: u8,
    /// Seconds left in the current phase (or on the device fuse).
    pub time_left: f32,
    pub device: Option<Device>,
    /// Winner squad and reason of the last round (or of the match).
    pub last_result: Option<(u8, u8)>,
    /// Set when anything replicated changed this tick.
    pub changed: bool,
    /// Squads swapped sides this tick (players' `team` changed).
    pub sides_swapped: bool,
}

impl Default for MatchState {
    fn default() -> Self {
        Self {
            phase: MatchPhase::Warmup,
            round: 0,
            score: [0, 0],
            attacking_squad: 0,
            time_left: 0.0,
            device: None,
            last_result: None,
            changed: true,
            sides_swapped: false,
        }
    }
}

/// Seconds of warmup once both sides have players.
pub const WARMUP_COUNTDOWN: f32 = 5.0;
/// Defenders must be this close to the device to defuse it (m).
pub const DEFUSE_REACH: f32 = 1.5;

impl GameWorld {
    /// Side (0 attackers, 1 defenders) a squad plays right now.
    pub fn side_of_squad(&self, squad: u8) -> u8 {
        if squad == self.match_state.attacking_squad {
            0
        } else {
            1
        }
    }

    /// Input restrictions for a player on `side` in the current phase:
    /// (hold position, weapons cold). Clients apply the same rule.
    pub fn restrictions(&self, side: u8) -> (bool, bool) {
        phase_restrictions(self.rules.mode, self.match_state.phase, side)
    }

    pub(super) fn respawns_enabled(&self) -> bool {
        !self.rules.mode.has_rounds() || self.match_state.phase == MatchPhase::Warmup
    }

    /// Clears runtime destruction, barricades, reinforcements and gadgets.
    pub(super) fn reset_map(&mut self) {
        self.collision = CollisionWorld::from_map(&self.map);
        self.destruction = Destruction::new(&self.collision);
        self.gadgets.clear();
        self.drone_cmds.clear();
        self.added_solids.clear();
        self.reinforced.clear();
        self.open_doors.clear();
        self.map_reset_this_tick = true;
    }

    /// Advances phase timers and transitions; called at the start of a tick.
    pub(super) fn update_match(&mut self) {
        self.match_state.changed = false;
        self.match_state.sides_swapped = false;
        self.map_reset_this_tick = false;
        let dt = self.dt;
        let mode = self.rules.mode;
        let both_sides =
            self.players.iter().any(|p| p.team == 0) && self.players.iter().any(|p| p.team == 1);
        match self.match_state.phase {
            MatchPhase::Warmup => {
                if !mode.has_rounds() {
                    // Deathmatch starts as soon as anyone is there.
                    self.match_state.phase = MatchPhase::Action;
                    self.match_state.time_left = self.rules.time_limit;
                    self.match_state.changed = true;
                } else if both_sides {
                    if self.match_state.time_left <= 0.0 {
                        self.match_state.time_left = WARMUP_COUNTDOWN;
                        self.match_state.changed = true;
                    }
                    self.match_state.time_left -= dt;
                    if self.match_state.time_left <= 0.0 {
                        self.start_round();
                    }
                } else if self.match_state.time_left > 0.0 {
                    self.match_state.time_left = 0.0; // countdown aborted
                    self.match_state.changed = true;
                }
            }
            MatchPhase::Prep => {
                self.match_state.time_left -= dt;
                if self.match_state.time_left <= 0.0 {
                    self.match_state.phase = MatchPhase::Action;
                    self.match_state.time_left = self.rules.round_time;
                    self.match_state.changed = true;
                }
            }
            MatchPhase::Action => {
                if let Some(d) = self.match_state.device.as_mut() {
                    d.fuse_left -= dt;
                    self.match_state.time_left = d.fuse_left;
                } else {
                    self.match_state.time_left -= dt;
                }
            }
            MatchPhase::RoundEnd => {
                self.match_state.time_left -= dt;
                if self.match_state.time_left <= 0.0 {
                    self.start_round();
                }
            }
            MatchPhase::MatchOver => {}
        }
    }

    /// Starts the next round: swap sides at half-time, reset the map,
    /// respawn and restock everyone, preparation phase.
    fn start_round(&mut self) {
        let ms = &mut self.match_state;
        let played = ms.score[0] as u32 + ms.score[1] as u32;
        if ms.round > 0 && played == self.rules.rounds_to_win.saturating_sub(1) as u32 {
            ms.attacking_squad ^= 1;
            ms.sides_swapped = true;
        }
        ms.round = ms.round.saturating_add(1);
        ms.phase = MatchPhase::Prep;
        ms.time_left = self.rules.prep_time;
        ms.device = None;
        ms.changed = true;
        for i in 0..self.players.len() {
            let side = self.side_of_squad(self.players[i].squad);
            self.players[i].team = side;
        }
        self.reset_map();
        for i in 0..self.players.len() {
            self.respawn(i);
        }
    }

    /// Plant / defuse with USE in objective modes, otherwise reinforcement.
    pub(super) fn update_use_action(&mut self, i: usize, cmd: &MoveInput, remote: bool) {
        let holding = cmd.buttons & buttons::USE != 0 && !remote && self.players[i].alive;
        if self.rules.mode == GameMode::Defuse
            && self.match_state.phase == MatchPhase::Action
            && holding
        {
            let p = &self.players[i];
            let side = p.team;
            let pos = p.state.pos;
            if side == 0 && self.match_state.device.is_none() {
                if let Some(site) = self.site_at(pos) {
                    let need = self.rules.plant_time;
                    if self.progress(i, action::PLANT, need) {
                        self.match_state.device = Some(Device {
                            site,
                            pos,
                            fuse_left: self.rules.device_fuse,
                        });
                        self.match_state.time_left = self.rules.device_fuse;
                        self.match_state.changed = true;
                    }
                    return;
                }
            }
            if side == 1 {
                if let Some(d) = self.match_state.device {
                    if d.pos.distance(&pos) <= DEFUSE_REACH {
                        let need = self.rules.defuse_time;
                        if self.progress(i, action::DEFUSE, need) {
                            let squad = self.players[i].squad;
                            self.end_round(squad, reason::DEFUSED);
                        }
                        return;
                    }
                }
            }
        }
        if self.players[i].action != action::REINFORCE {
            self.players[i].action = action::NONE;
        }
        self.update_reinforcing(i, cmd, remote);
        let p = &mut self.players[i];
        if p.reinforce_target.is_some() {
            p.action = action::REINFORCE;
        } else if p.action == action::REINFORCE {
            p.action = action::NONE;
        }
    }

    /// Accumulates progress of a hold action; true when it completes.
    fn progress(&mut self, i: usize, kind: u8, need: f32) -> bool {
        let dt = self.dt;
        let p = &mut self.players[i];
        p.reinforce_target = None;
        if p.action == kind {
            p.reinforce_progress += dt;
        } else {
            p.action = kind;
            p.reinforce_progress = dt;
        }
        if p.reinforce_progress >= need {
            p.action = action::NONE;
            p.reinforce_progress = 0.0;
            true
        } else {
            false
        }
    }

    /// Objective site containing a feet position.
    pub fn site_at(&self, pos: Vec3) -> Option<u8> {
        self.map
            .objectives
            .iter()
            .position(|o| {
                let b = &o.bounds;
                pos.x >= b.min.x
                    && pos.x <= b.max.x
                    && pos.y >= b.min.y
                    && pos.y <= b.max.y
                    && pos.z >= b.min.z - 0.05
                    && pos.z <= b.max.z
            })
            .map(|i| i as u8)
    }

    /// Checks win conditions; called at the end of a tick.
    pub(super) fn check_round_end(&mut self) {
        let mode = self.rules.mode;
        if self.match_state.phase != MatchPhase::Action {
            return;
        }
        if !mode.has_rounds() {
            let limit = self.rules.score_limit;
            let ms = &self.match_state;
            if let Some(squad) = (0..2u8).find(|&s| ms.score[s as usize] >= limit) {
                self.finish_match(squad, reason::SCORE_LIMIT);
            } else if self.rules.time_limit > 0.0 && ms.time_left <= 0.0 {
                let squad = if ms.score[1] > ms.score[0] { 1 } else { 0 };
                self.finish_match(squad, reason::TIME_UP);
            }
            return;
        }
        let attackers = self.match_state.attacking_squad;
        let defenders = attackers ^ 1;
        if let Some(d) = self.match_state.device {
            if d.fuse_left <= 0.0 {
                self.explosions.push((d.pos, 6.0));
                self.end_round(attackers, reason::DETONATED);
                return;
            }
        }
        let alive = |side: u8| self.players.iter().any(|p| p.alive && p.team == side);
        let (att_alive, def_alive) = (alive(0), alive(1));
        if !def_alive {
            self.end_round(attackers, reason::ELIMINATED);
        } else if !att_alive && self.match_state.device.is_none() {
            self.end_round(defenders, reason::ELIMINATED);
        } else if self.match_state.device.is_none() && self.match_state.time_left <= 0.0 {
            self.end_round(defenders, reason::TIME_UP);
        }
    }

    fn end_round(&mut self, winner: u8, why: u8) {
        let ms = &mut self.match_state;
        ms.score[winner as usize] = ms.score[winner as usize].saturating_add(1);
        ms.last_result = Some((winner, why));
        ms.device = None;
        ms.changed = true;
        self.events.push(GameEvent::RoundEnd {
            winner,
            reason: why,
        });
        if ms.score[winner as usize] >= self.rules.rounds_to_win {
            ms.phase = MatchPhase::MatchOver;
            ms.time_left = 0.0;
        } else {
            ms.phase = MatchPhase::RoundEnd;
            ms.time_left = self.rules.round_end_time;
        }
    }

    fn finish_match(&mut self, winner: u8, why: u8) {
        let ms = &mut self.match_state;
        ms.phase = MatchPhase::MatchOver;
        ms.last_result = Some((winner, why));
        ms.time_left = 0.0;
        ms.changed = true;
    }

    /// Deathmatch scoring: a kill scores for the killer's squad.
    pub(super) fn score_kill(&mut self, killer: u16, victim: u16) {
        if self.rules.mode.has_rounds()
            || self.match_state.phase != MatchPhase::Action
            || killer == victim
        {
            return;
        }
        if let Some(squad) = self.player(killer).map(|p| p.squad) {
            let s = &mut self.match_state.score[squad as usize];
            *s = s.saturating_add(1);
            self.match_state.changed = true;
        }
    }
}

/// Restrictions per phase and side (shared with clients through the
/// replicated match state): attackers hold position during preparation;
/// weapons are cold during preparation and after the match.
pub fn phase_restrictions(mode: GameMode, phase: MatchPhase, side: u8) -> (bool, bool) {
    tfps_sim::weapon::phase_restrictions(mode.has_rounds(), phase as u8, side)
}
