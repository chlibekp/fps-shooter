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
        ] {
            assert_eq!(
                Message::decode(m.msg_type(), &m.encode_payload()).unwrap(),
                m
            );
        }
    }
}
