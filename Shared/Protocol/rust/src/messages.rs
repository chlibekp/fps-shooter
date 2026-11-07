//! Reliable message payloads (carried by the reliable channel).

use crate::bytes::{Reader, Writer};
use crate::{DecodeError, MAX_NAME_LEN};

pub mod msg_type {
    pub const PLAYER_JOINED: u8 = 1;
    pub const PLAYER_LEFT: u8 = 2;
    pub const PLAYER_CONN_STATE: u8 = 3;
}

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
}

impl Message {
    pub fn msg_type(&self) -> u8 {
        match self {
            Message::PlayerJoined { .. } => msg_type::PLAYER_JOINED,
            Message::PlayerLeft { .. } => msg_type::PLAYER_LEFT,
            Message::PlayerConnState { .. } => msg_type::PLAYER_CONN_STATE,
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
        ] {
            assert_eq!(
                Message::decode(m.msg_type(), &m.encode_payload()).unwrap(),
                m
            );
        }
    }
}
