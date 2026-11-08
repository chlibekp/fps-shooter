//! Reliable message payloads (carried by the reliable channel).

use crate::bytes::{Reader, Writer};
use crate::{DecodeError, MAX_NAME_LEN};

pub mod msg_type {
    pub const PLAYER_JOINED: u8 = 1;
    pub const PLAYER_LEFT: u8 = 2;
    pub const PLAYER_CONN_STATE: u8 = 3;
    pub const HIT_CONFIRM: u8 = 4;
    pub const DAMAGED: u8 = 5;
    pub const KILL: u8 = 6;
    pub const CELLS_DESTROYED: u8 = 7;
    pub const EXPLOSION: u8 = 8;
    pub const FLASH: u8 = 9;
    pub const SPOTTED: u8 = 10;
    pub const SOLID_ADDED: u8 = 11;
    pub const SOLID_REINFORCED: u8 = 12;
    pub const MATCH_STATE: u8 = 13;
    pub const MAP_RESET: u8 = 14;
}

/// Maximum cell ids per CellsDestroyed message (fits the 400-byte per-packet
/// reliable budget the server uses).
pub const MAX_CELLS_PER_MESSAGE: usize = 90;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum ConnState {
    Connected = 0,
    /// No packets for a while; may still recover.
    Interrupted = 1,
    /// Connection lost; slot reserved for reconnect.
    Reconnecting = 2,
}

impl ConnState {
    pub fn from_u8(v: u8) -> Result<Self, DecodeError> {
        match v {
            0 => Ok(Self::Connected),
            1 => Ok(Self::Interrupted),
            2 => Ok(Self::Reconnecting),
            _ => Err(DecodeError::Invalid("bad conn state")),
        }
    }
}

/// Replicated match progress (see PROTOCOL.md, MatchState).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct MatchInfo {
    /// 0 deathmatch, 1 elimination, 2 defuse.
    pub mode: u8,
    /// 0 warmup, 1 preparation, 2 action, 3 round end, 4 match over.
    pub phase: u8,
    pub round: u8,
    /// Per squad: rounds won (kills in deathmatch).
    pub score: [u8; 2],
    /// Squad playing attackers (side 0) this round.
    pub attacking_squad: u8,
    /// Seconds left in the phase (or on the device fuse) x 10.
    pub time_left_ds: u16,
    /// Rounds to win (score limit in deathmatch).
    pub rounds_to_win: u8,
    /// Objective site holding the planted device, 255 = none.
    pub device_site: u8,
    pub device_pos_mm: [i32; 3],
    /// Winner squad of the last round / the match, 255 = none.
    pub last_winner: u8,
    /// Why (1 eliminated, 2 time up, 3 detonated, 4 defused, 5 score limit).
    pub last_reason: u8,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Message {
    /// S -> C. A player is present in the match (also sent for every existing
    /// player right after connecting).
    PlayerJoined {
        player_id: u16,
        team: u8,
        name: String,
    },
    /// S -> C. `reason` is a `DisconnectReason`.
    PlayerLeft { player_id: u16, reason: u8 },
    /// S -> C. Network status of a player (for the scoreboard / lobby).
    PlayerConnState { player_id: u16, state: ConnState },
    /// S -> shooter. One of your shots hit `victim` (hit marker).
    HitConfirm {
        victim: u16,
        damage: u8,
        headshot: bool,
        killed: bool,
    },
    /// S -> victim. You were damaged; `from_mm` is the attacker's position
    /// (for the direction indicator).
    Damaged {
        attacker: u16,
        amount: u8,
        health: u8,
        from_mm: [i32; 3],
    },
    /// S -> all. Destructible cells that were destroyed (also sent in full
    /// to joining players). Clients remove them from collision and visuals.
    CellsDestroyed { cells: Vec<u32> },
    /// S -> all. An explosion happened (effects only; damage is server-side).
    Explosion { pos_mm: [i32; 3], radius_cm: u16 },
    /// S -> all. A flashbang went off at `pos_mm`; `intensity` (0..=255) is
    /// how strongly the receiving player is blinded (0 = not affected).
    Flash { pos_mm: [i32; 3], intensity: u8 },
    /// S -> the spotter's team. `target` was seen by `spotter` (drone) at
    /// `pos_mm`; show a marker for `duration_ds` tenths of a second.
    Spotted {
        target: u16,
        spotter: u16,
        pos_mm: [i32; 3],
        duration_ds: u8,
    },
    /// S -> all. A solid was added to the map at runtime (deployable
    /// barricade). `index` must equal the receiver's current solid count;
    /// sent in order, before any CellsDestroyed for its cells, and in full
    /// to joining players.
    SolidAdded {
        index: u16,
        min_mm: [i32; 3],
        max_mm: [i32; 3],
        material: u8,
        destructible: bool,
    },
    /// S -> all. Solid `index` was reinforced (its material is now
    /// Reinforced: bullets and explosives no longer affect it).
    SolidReinforced { index: u16 },
    /// S -> all (and joiners). Game mode and match progress; sent when it
    /// changes. Clients count `time_left_ds` down locally.
    MatchState(MatchInfo),
    /// S -> all. A new round started: rebuild the map from its definition
    /// (destruction, barricades and reinforcements are cleared).
    MapReset,
    /// S -> all. Kill feed.
    Kill {
        killer: u16,
        victim: u16,
        weapon: u8,
        headshot: bool,
    },
}

impl Message {
    pub fn msg_type(&self) -> u8 {
        match self {
            Message::PlayerJoined { .. } => msg_type::PLAYER_JOINED,
            Message::PlayerLeft { .. } => msg_type::PLAYER_LEFT,
            Message::PlayerConnState { .. } => msg_type::PLAYER_CONN_STATE,
            Message::HitConfirm { .. } => msg_type::HIT_CONFIRM,
            Message::Damaged { .. } => msg_type::DAMAGED,
            Message::Kill { .. } => msg_type::KILL,
            Message::CellsDestroyed { .. } => msg_type::CELLS_DESTROYED,
            Message::Explosion { .. } => msg_type::EXPLOSION,
            Message::Flash { .. } => msg_type::FLASH,
            Message::Spotted { .. } => msg_type::SPOTTED,
            Message::SolidAdded { .. } => msg_type::SOLID_ADDED,
            Message::SolidReinforced { .. } => msg_type::SOLID_REINFORCED,
            Message::MatchState(_) => msg_type::MATCH_STATE,
            Message::MapReset => msg_type::MAP_RESET,
        }
    }

    pub fn encode_payload(&self) -> Vec<u8> {
        let mut v = Vec::with_capacity(32);
        let mut w = Writer::new(&mut v);
        match self {
            Message::PlayerJoined {
                player_id,
                team,
                name,
            } => {
                w.u16(*player_id);
                w.u8(*team);
                w.str8(name, MAX_NAME_LEN);
            }
            Message::PlayerLeft { player_id, reason } => {
                w.u16(*player_id);
                w.u8(*reason);
            }
            Message::PlayerConnState { player_id, state } => {
                w.u16(*player_id);
                w.u8(*state as u8);
            }
            Message::HitConfirm {
                victim,
                damage,
                headshot,
                killed,
            } => {
                w.u16(*victim);
                w.u8(*damage);
                w.u8((*headshot as u8) | ((*killed as u8) << 1));
            }
            Message::Damaged {
                attacker,
                amount,
                health,
                from_mm,
            } => {
                w.u16(*attacker);
                w.u8(*amount);
                w.u8(*health);
                for v in from_mm {
                    w.i32(*v);
                }
            }
            Message::CellsDestroyed { cells } => {
                let n = cells.len().min(MAX_CELLS_PER_MESSAGE);
                w.u8(n as u8);
                for c in &cells[..n] {
                    w.u32(*c);
                }
            }
            Message::Explosion { pos_mm, radius_cm } => {
                for v in pos_mm {
                    w.i32(*v);
                }
                w.u16(*radius_cm);
            }
            Message::Flash { pos_mm, intensity } => {
                for v in pos_mm {
                    w.i32(*v);
                }
                w.u8(*intensity);
            }
            Message::SolidAdded {
                index,
                min_mm,
                max_mm,
                material,
                destructible,
            } => {
                w.u16(*index);
                for v in min_mm.iter().chain(max_mm.iter()) {
                    w.i32(*v);
                }
                w.u8(*material);
                w.u8(*destructible as u8);
            }
            Message::SolidReinforced { index } => w.u16(*index),
            Message::MatchState(m) => {
                w.u8(m.mode);
                w.u8(m.phase);
                w.u8(m.round);
                w.u8(m.score[0]);
                w.u8(m.score[1]);
                w.u8(m.attacking_squad);
                w.u16(m.time_left_ds);
                w.u8(m.rounds_to_win);
                w.u8(m.device_site);
                for v in m.device_pos_mm {
                    w.i32(v);
                }
                w.u8(m.last_winner);
                w.u8(m.last_reason);
            }
            Message::MapReset => {}
            Message::Spotted {
                target,
                spotter,
                pos_mm,
                duration_ds,
            } => {
                w.u16(*target);
                w.u16(*spotter);
                for v in pos_mm {
                    w.i32(*v);
                }
                w.u8(*duration_ds);
            }
            Message::Kill {
                killer,
                victim,
                weapon,
                headshot,
            } => {
                w.u16(*killer);
                w.u16(*victim);
                w.u8(*weapon);
                w.u8(*headshot as u8);
            }
        }
        v
    }

    pub fn decode(ty: u8, payload: &[u8]) -> Result<Self, DecodeError> {
        let mut r = Reader::new(payload);
        Ok(match ty {
            msg_type::PLAYER_JOINED => Message::PlayerJoined {
                player_id: r.u16()?,
                team: r.u8()?,
                name: r.str8(MAX_NAME_LEN)?,
            },
            msg_type::PLAYER_LEFT => Message::PlayerLeft {
                player_id: r.u16()?,
                reason: r.u8()?,
            },
            msg_type::PLAYER_CONN_STATE => Message::PlayerConnState {
                player_id: r.u16()?,
                state: ConnState::from_u8(r.u8()?)?,
            },
            msg_type::HIT_CONFIRM => {
                let victim = r.u16()?;
                let damage = r.u8()?;
                let f = r.u8()?;
                Message::HitConfirm {
                    victim,
                    damage,
                    headshot: f & 1 != 0,
                    killed: f & 2 != 0,
                }
            }
            msg_type::DAMAGED => Message::Damaged {
                attacker: r.u16()?,
                amount: r.u8()?,
                health: r.u8()?,
                from_mm: [r.i32()?, r.i32()?, r.i32()?],
            },
            msg_type::KILL => Message::Kill {
                killer: r.u16()?,
                victim: r.u16()?,
                weapon: r.u8()?,
                headshot: r.u8()? != 0,
            },
            msg_type::CELLS_DESTROYED => {
                let n = r.u8()? as usize;
                if n > MAX_CELLS_PER_MESSAGE {
                    return Err(DecodeError::Invalid("too many cells"));
                }
                let mut cells = Vec::with_capacity(n);
                for _ in 0..n {
                    cells.push(r.u32()?);
                }
                Message::CellsDestroyed { cells }
            }
            msg_type::EXPLOSION => Message::Explosion {
                pos_mm: [r.i32()?, r.i32()?, r.i32()?],
                radius_cm: r.u16()?,
            },
            msg_type::FLASH => Message::Flash {
                pos_mm: [r.i32()?, r.i32()?, r.i32()?],
                intensity: r.u8()?,
            },
            msg_type::SOLID_ADDED => Message::SolidAdded {
                index: r.u16()?,
                min_mm: [r.i32()?, r.i32()?, r.i32()?],
                max_mm: [r.i32()?, r.i32()?, r.i32()?],
                material: r.u8()?,
                destructible: r.u8()? != 0,
            },
            msg_type::SOLID_REINFORCED => Message::SolidReinforced { index: r.u16()? },
            msg_type::MATCH_STATE => Message::MatchState(MatchInfo {
                mode: r.u8()?,
                phase: r.u8()?,
                round: r.u8()?,
                score: [r.u8()?, r.u8()?],
                attacking_squad: r.u8()?,
                time_left_ds: r.u16()?,
                rounds_to_win: r.u8()?,
                device_site: r.u8()?,
                device_pos_mm: [r.i32()?, r.i32()?, r.i32()?],
                last_winner: r.u8()?,
                last_reason: r.u8()?,
            }),
            msg_type::MAP_RESET => Message::MapReset,
            msg_type::SPOTTED => Message::Spotted {
                target: r.u16()?,
                spotter: r.u16()?,
                pos_mm: [r.i32()?, r.i32()?, r.i32()?],
                duration_ds: r.u8()?,
            },
            _ => return Err(DecodeError::Invalid("unknown message type")),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn roundtrip() {
        for m in [
            Message::PlayerJoined {
                player_id: 3,
                team: 1,
                name: "Viper".into(),
            },
            Message::PlayerLeft {
                player_id: 3,
                reason: 2,
            },
            Message::PlayerConnState {
                player_id: 3,
                state: ConnState::Reconnecting,
            },
            Message::HitConfirm {
                victim: 4,
                damage: 108,
                headshot: true,
                killed: true,
            },
            Message::Damaged {
                attacker: 2,
                amount: 36,
                health: 64,
                from_mm: [1, -2, 3],
            },
            Message::Kill {
                killer: 2,
                victim: 4,
                weapon: 1,
                headshot: false,
            },
            Message::CellsDestroyed {
                cells: vec![0, 7, 65_000, 1_000_000],
            },
            Message::Explosion {
                pos_mm: [1, -2, 3],
                radius_cm: 300,
            },
            Message::Flash {
                pos_mm: [4, 5, -6],
                intensity: 200,
            },
            Message::Spotted {
                target: 7,
                spotter: 2,
                pos_mm: [1, 2, 3],
                duration_ds: 40,
            },
            Message::SolidAdded {
                index: 105,
                min_mm: [1, -2, 0],
                max_mm: [1_080, 950, 2_200],
                material: 2,
                destructible: true,
            },
            Message::SolidReinforced { index: 12 },
            Message::MatchState(MatchInfo {
                mode: 2,
                phase: 2,
                round: 3,
                score: [1, 2],
                attacking_squad: 1,
                time_left_ds: 1234,
                rounds_to_win: 4,
                device_site: 1,
                device_pos_mm: [1, -2, 3],
                last_winner: 0,
                last_reason: 4,
            }),
            Message::MapReset,
        ] {
            assert_eq!(
                Message::decode(m.msg_type(), &m.encode_payload()).unwrap(),
                m
            );
        }
    }
}
