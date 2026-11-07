//! Client input bundle (unreliable body kind 1).
//!
//! ```text
//! u32 ack_snapshot_tick   newest snapshot fully decoded by the client (0 = none)
//! u32 newest_seq          command sequence of cmds[0]
//! u8  count               1..=MAX_INPUT_REDUNDANCY
//! repeated count: u16 buttons, u16 yaw_q, i16 pitch_q   (cmds[i] has seq newest_seq - i)
//! ```
//! Sending the last N commands in every packet makes input delivery robust to
//! packet loss without retransmission delays.

use crate::bytes::{Reader, Writer};
use crate::DecodeError;
use tfps_sim::MoveInput;

pub const MAX_INPUT_REDUNDANCY: usize = 16;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct InputBundle {
    pub ack_snapshot_tick: u32,
    pub newest_seq: u32,
    pub count: u8,
    /// `cmds[0]` is the newest.
    pub cmds: [MoveInput; MAX_INPUT_REDUNDANCY],
}

impl Default for InputBundle {
    fn default() -> Self {
        Self {
            ack_snapshot_tick: 0,
            newest_seq: 0,
            count: 0,
            cmds: [MoveInput::default(); MAX_INPUT_REDUNDANCY],
        }
    }
}

impl InputBundle {
    pub fn encode(&self, w: &mut Writer) {
        w.u32(self.ack_snapshot_tick);
        w.u32(self.newest_seq);
        w.u8(self.count);
        for c in &self.cmds[..self.count as usize] {
            w.u16(c.buttons);
            w.u16(c.yaw_q);
            w.i16(c.pitch_q);
        }
    }

    pub fn decode(r: &mut Reader) -> Result<Self, DecodeError> {
        let mut b = InputBundle {
            ack_snapshot_tick: r.u32()?,
            newest_seq: r.u32()?,
            count: r.u8()?,
            ..Default::default()
        };
        if b.count == 0 || b.count as usize > MAX_INPUT_REDUNDANCY {
            return Err(DecodeError::Invalid("bad input count"));
        }
        if (b.count as u32) > b.newest_seq {
            return Err(DecodeError::Invalid("input seq underflow"));
        }
        for i in 0..b.count as usize {
            b.cmds[i] = MoveInput {
                buttons: r.u16()?,
                yaw_q: r.u16()?,
                pitch_q: r.i16()?,
            };
        }
        Ok(b)
    }

    /// Iterates `(seq, cmd)` oldest first.
    pub fn iter_oldest_first(&self) -> impl Iterator<Item = (u32, MoveInput)> + '_ {
        (0..self.count as usize)
            .rev()
            .map(move |i| (self.newest_seq - i as u32, self.cmds[i]))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn roundtrip() {
        let mut b = InputBundle {
            ack_snapshot_tick: 5,
            newest_seq: 100,
            count: 3,
            ..Default::default()
        };
        b.cmds[0] = MoveInput {
            buttons: 1,
            yaw_q: 2,
            pitch_q: -3,
        };
        b.cmds[2] = MoveInput {
            buttons: 9,
            yaw_q: 65535,
            pitch_q: 32767,
        };
        let mut v = Vec::new();
        b.encode(&mut Writer::new(&mut v));
        assert_eq!(v.len(), 9 + 3 * 6);
        let d = InputBundle::decode(&mut Reader::new(&v)).unwrap();
        assert_eq!(d, b);
        let seqs: Vec<u32> = d.iter_oldest_first().map(|(s, _)| s).collect();
        assert_eq!(seqs, vec![98, 99, 100]);
    }
    #[test]
    fn rejects_bad_counts() {
        let v = [0u8, 0, 0, 0, 1, 0, 0, 0, 0];
        assert!(InputBundle::decode(&mut Reader::new(&v)).is_err());
        let v = [
            0u8, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        ];
        assert!(InputBundle::decode(&mut Reader::new(&v)).is_err());
    }
}
