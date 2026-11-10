//! TacticalFPS wire protocol.
//!
//! The byte-level specification is `Shared/Protocol/PROTOCOL.md`. The C++
//! client mirror lives in `Client/Source/TacticalFPS/*/NetCore/TfpsProtocol.*`.
//! Any change to the wire format must bump [`PROTOCOL_VERSION`].

pub mod ack;
pub mod bytes;
pub mod input;
pub mod messages;
pub mod packet;
pub mod reliable;
pub mod snapshot;

pub use bytes::{Reader, Writer};

/// 'T','F' little-endian.
pub const MAGIC: u16 = 0x4654;
pub const PROTOCOL_VERSION: u16 = 9;
/// Conservative MTU-safe payload size (IPv6 minimum MTU 1280 minus headers).
pub const MAX_PACKET_SIZE: usize = 1200;
/// Handshake requests are padded to this size so the server never answers
/// with more bytes than it received (no amplification).
pub const HANDSHAKE_PACKET_SIZE: usize = 256;
pub const MAX_NAME_LEN: usize = 24;
pub const MAX_MAP_NAME_LEN: usize = 64;

#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum DecodeError {
    #[error("packet truncated")]
    Truncated,
    #[error("bad magic")]
    BadMagic,
    #[error("protocol version mismatch (peer {0})")]
    BadVersion(u16),
    #[error("unknown packet type {0}")]
    BadType(u8),
    #[error("missing snapshot baseline {0}")]
    MissingBaseline(u32),
    #[error("invalid packet: {0}")]
    Invalid(&'static str),
}
