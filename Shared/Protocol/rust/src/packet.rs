//! Packet envelopes: handshake packets and the connected `Data` packet header.

use crate::bytes::{Reader, Writer};
use crate::{
    DecodeError, HANDSHAKE_PACKET_SIZE, MAGIC, MAX_MAP_NAME_LEN, MAX_NAME_LEN, PROTOCOL_VERSION,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum PacketType {
    ConnectRequest = 1,
    Challenge = 2,
    ChallengeResponse = 3,
    ConnectAccepted = 4,
    ConnectRejected = 5,
    Data = 6,
    Disconnect = 7,
}

impl PacketType {
    pub fn from_u8(v: u8) -> Option<Self> {
        Some(match v {
            1 => Self::ConnectRequest,
            2 => Self::Challenge,
            3 => Self::ChallengeResponse,
            4 => Self::ConnectAccepted,
            5 => Self::ConnectRejected,
            6 => Self::Data,
            7 => Self::Disconnect,
            _ => return None,
        })
    }
}

pub const HEADER_SIZE: usize = 5;

pub fn write_header(w: &mut Writer, ty: PacketType) {
    w.u16(MAGIC);
    w.u16(PROTOCOL_VERSION);
    w.u8(ty as u8);
}

/// Parsed common header. Version is returned instead of rejected so the
/// server can answer old clients with `ConnectRejected(VersionMismatch)`.
pub struct Header {
    pub version: u16,
    pub ty: PacketType,
}

pub fn read_header<'a>(data: &'a [u8]) -> Result<(Header, Reader<'a>), DecodeError> {
    let mut r = Reader::new(data);
    if r.u16()? != MAGIC {
        return Err(DecodeError::BadMagic);
    }
    let version = r.u16()?;
    let t = r.u8()?;
    let ty = PacketType::from_u8(t).ok_or(DecodeError::BadType(t))?;
    Ok((Header { version, ty }, r))
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum RejectReason {
    ServerFull = 1,
    VersionMismatch = 2,
    InvalidChallenge = 3,
    InvalidName = 4,
    Unauthorized = 5,
    Unknown = 255,
}

impl RejectReason {
    pub fn from_u8(v: u8) -> Self {
        match v {
            1 => Self::ServerFull,
            2 => Self::VersionMismatch,
            3 => Self::InvalidChallenge,
            4 => Self::InvalidName,
            5 => Self::Unauthorized,
            _ => Self::Unknown,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum DisconnectReason {
    ClientQuit = 1,
    Timeout = 2,
    Kicked = 3,
    ServerShutdown = 4,
    ProtocolError = 5,
    Unknown = 255,
}

impl DisconnectReason {
    pub fn from_u8(v: u8) -> Self {
        match v {
            1 => Self::ClientQuit,
            2 => Self::Timeout,
            3 => Self::Kicked,
            4 => Self::ServerShutdown,
            5 => Self::ProtocolError,
            _ => Self::Unknown,
        }
    }
}

/// C -> S. Step 1 of the handshake.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ConnectRequest {
    pub client_nonce: u64,
}

impl ConnectRequest {
    pub fn encode(&self, out: &mut Vec<u8>) {
        let mut w = Writer::new(out);
        write_header(&mut w, PacketType::ConnectRequest);
        w.u64(self.client_nonce);
        w.pad_to(HANDSHAKE_PACKET_SIZE);
    }
    pub fn decode(r: &mut Reader, total_len: usize) -> Result<Self, DecodeError> {
        if total_len < HANDSHAKE_PACKET_SIZE {
            return Err(DecodeError::Invalid("handshake not padded"));
        }
        Ok(Self {
            client_nonce: r.u64()?,
        })
    }
}

/// S -> C. The cookie is a keyed hash of (client address, nonce, time
/// bucket); the server keeps no state for unverified clients.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Challenge {
    pub client_nonce: u64,
    pub cookie: u64,
}

impl Challenge {
    pub fn encode(&self, out: &mut Vec<u8>) {
        let mut w = Writer::new(out);
        write_header(&mut w, PacketType::Challenge);
        w.u64(self.client_nonce);
        w.u64(self.cookie);
    }
    pub fn decode(r: &mut Reader) -> Result<Self, DecodeError> {
        Ok(Self {
            client_nonce: r.u64()?,
            cookie: r.u64()?,
        })
    }
}

/// C -> S. Step 3: echoes the cookie, proving the client owns its address.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ChallengeResponse {
    pub client_nonce: u64,
    pub cookie: u64,
    /// Token from a previous `ConnectAccepted` to resume a session, or 0.
    pub reconnect_token: u64,
    /// Match access token (lobby invite system, Phase 6). 0 = none.
    pub join_token: u64,
    pub name: String,
    /// Requested armour class (0 medium, 1 light, 2 heavy). Sits in what
    /// older clients sent as padding (zero = medium).
    pub armor: u8,
    /// Primary weapon attachments (tfps_sim::weapon::attachment).
    pub attachments: u8,
    /// Requested primary weapon id (0 = the server's default; the server
    /// falls back to the default for unknown ids and pistols).
    pub primary: u8,
    /// Client's X25519 public key for this session (see `crypto`).
    pub client_public: [u8; 32],
}

impl ChallengeResponse {
    pub fn encode(&self, out: &mut Vec<u8>) {
        let mut w = Writer::new(out);
        write_header(&mut w, PacketType::ChallengeResponse);
        w.u64(self.client_nonce);
        w.u64(self.cookie);
        w.u64(self.reconnect_token);
        w.u64(self.join_token);
        w.str8(&self.name, MAX_NAME_LEN);
        w.u8(self.armor);
        w.u8(self.attachments);
        w.u8(self.primary);
        w.bytes(&self.client_public);
        w.pad_to(HANDSHAKE_PACKET_SIZE);
    }
    pub fn decode(r: &mut Reader, total_len: usize) -> Result<Self, DecodeError> {
        if total_len < HANDSHAKE_PACKET_SIZE {
            return Err(DecodeError::Invalid("handshake not padded"));
        }
        Ok(Self {
            client_nonce: r.u64()?,
            cookie: r.u64()?,
            reconnect_token: r.u64()?,
            join_token: r.u64()?,
            name: r.str8(MAX_NAME_LEN)?,
            armor: r.u8()?.min(2),
            attachments: r.u8()? & 0x0F,
            primary: r.u8()?,
            client_public: r.take(32)?.try_into().expect("32 bytes"),
        })
    }
}

/// S -> C. Session established.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ConnectAccepted {
    pub client_nonce: u64,
    /// Must be included in every subsequent packet of this session.
    pub session_key: u64,
    pub player_id: u16,
    pub tick_rate: u16,
    /// A snapshot is sent every `snapshot_interval` ticks.
    pub snapshot_interval: u8,
    pub server_tick: u32,
    /// Secret allowing this player to resume the session after a disconnect.
    pub reconnect_token: u64,
    pub map_hash: u64,
    pub map_name: String,
    /// Fingerprint of the weapon definitions (client must match).
    pub weapons_hash: u64,
    /// Weapon ids in the player's two slots.
    pub loadout: [u8; 2],
    /// Server's X25519 public key for this session (see `crypto`).
    pub server_public: [u8; 32],
}

impl ConnectAccepted {
    pub fn encode(&self, out: &mut Vec<u8>) {
        let mut w = Writer::new(out);
        write_header(&mut w, PacketType::ConnectAccepted);
        w.u64(self.client_nonce);
        w.u64(self.session_key);
        w.u16(self.player_id);
        w.u16(self.tick_rate);
        w.u8(self.snapshot_interval);
        w.u32(self.server_tick);
        w.u64(self.reconnect_token);
        w.u64(self.map_hash);
        w.str8(&self.map_name, MAX_MAP_NAME_LEN);
        w.u64(self.weapons_hash);
        w.u8(self.loadout[0]);
        w.u8(self.loadout[1]);
        w.bytes(&self.server_public);
    }
    pub fn decode(r: &mut Reader) -> Result<Self, DecodeError> {
        Ok(Self {
            client_nonce: r.u64()?,
            session_key: r.u64()?,
            player_id: r.u16()?,
            tick_rate: r.u16()?,
            snapshot_interval: r.u8()?,
            server_tick: r.u32()?,
            reconnect_token: r.u64()?,
            map_hash: r.u64()?,
            map_name: r.str8(MAX_MAP_NAME_LEN)?,
            weapons_hash: r.u64()?,
            loadout: [r.u8()?, r.u8()?],
            server_public: r.take(32)?.try_into().expect("32 bytes"),
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ConnectRejected {
    pub client_nonce: u64,
    pub reason: RejectReason,
}

impl ConnectRejected {
    pub fn encode(&self, out: &mut Vec<u8>) {
        let mut w = Writer::new(out);
        write_header(&mut w, PacketType::ConnectRejected);
        w.u64(self.client_nonce);
        w.u8(self.reason as u8);
    }
    pub fn decode(r: &mut Reader) -> Result<Self, DecodeError> {
        Ok(Self {
            client_nonce: r.u64()?,
            reason: RejectReason::from_u8(r.u8()?),
        })
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Disconnect {
    pub session_key: u64,
    pub reason: DisconnectReason,
}

impl Disconnect {
    pub fn encode(&self, out: &mut Vec<u8>) {
        let mut w = Writer::new(out);
        write_header(&mut w, PacketType::Disconnect);
        w.u64(self.session_key);
        w.u8(self.reason as u8);
    }
    pub fn decode(r: &mut Reader) -> Result<Self, DecodeError> {
        Ok(Self {
            session_key: r.u64()?,
            reason: DisconnectReason::from_u8(r.u8()?),
        })
    }
}

/// Header of a connected `Data` packet. Followed by the reliable message
/// section (see `reliable.rs`) and one optional unreliable body.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Default)]
pub struct DataHeader {
    pub session_key: u64,
    pub seq: u16,
    pub ack: u16,
    pub ack_bits: u32,
}

impl DataHeader {
    pub fn encode(&self, w: &mut Writer) {
        write_header(w, PacketType::Data);
        w.u64(self.session_key);
        w.u16(self.seq);
        w.u16(self.ack);
        w.u32(self.ack_bits);
    }
    pub fn decode(r: &mut Reader) -> Result<Self, DecodeError> {
        Ok(Self {
            session_key: r.u64()?,
            seq: r.u16()?,
            ack: r.u16()?,
            ack_bits: r.u32()?,
        })
    }
}

/// Kind byte of the unreliable body at the end of a `Data` packet.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(u8)]
pub enum BodyKind {
    None = 0,
    /// C -> S, see `input.rs`.
    Input = 1,
    /// S -> C, see `snapshot.rs`.
    Snapshot = 2,
}

impl BodyKind {
    pub fn from_u8(v: u8) -> Result<Self, DecodeError> {
        match v {
            0 => Ok(Self::None),
            1 => Ok(Self::Input),
            2 => Ok(Self::Snapshot),
            _ => Err(DecodeError::Invalid("unknown body kind")),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handshake_roundtrip_and_padding() {
        let mut buf = Vec::new();
        ConnectRequest { client_nonce: 42 }.encode(&mut buf);
        assert_eq!(buf.len(), HANDSHAKE_PACKET_SIZE);
        let (h, mut r) = read_header(&buf).unwrap();
        assert_eq!(h.ty, PacketType::ConnectRequest);
        assert_eq!(
            ConnectRequest::decode(&mut r, buf.len())
                .unwrap()
                .client_nonce,
            42
        );

        let cr = ChallengeResponse {
            client_nonce: 1,
            cookie: 2,
            reconnect_token: 3,
            join_token: 4,
            name: "Operator".into(),
            armor: 0,
            attachments: 0,
            primary: 0,
            client_public: [0; 32],
        };
        buf.clear();
        cr.encode(&mut buf);
        assert_eq!(buf.len(), HANDSHAKE_PACKET_SIZE);
        let (_, mut r) = read_header(&buf).unwrap();
        assert_eq!(ChallengeResponse::decode(&mut r, buf.len()).unwrap(), cr);

        let acc = ConnectAccepted {
            client_nonce: 1,
            session_key: 0xdead_beef,
            player_id: 7,
            tick_rate: 128,
            snapshot_interval: 2,
            server_tick: 1000,
            reconnect_token: 99,
            map_hash: 0x1234,
            map_name: "test_arena".into(),
            weapons_hash: 0x9999,
            loadout: [1, 4],
            server_public: [0xa5; 32],
        };
        buf.clear();
        acc.encode(&mut buf);
        assert!(buf.len() <= HANDSHAKE_PACKET_SIZE);
        let (_, mut r) = read_header(&buf).unwrap();
        assert_eq!(ConnectAccepted::decode(&mut r).unwrap(), acc);
    }

    #[test]
    fn rejects_garbage() {
        assert!(read_header(&[1, 2, 3]).is_err());
        assert_eq!(
            read_header(&[0x54, 0x46, 1, 0, 99]).err(),
            Some(DecodeError::BadType(99))
        );
        let mut buf = Vec::new();
        ConnectRequest { client_nonce: 42 }.encode(&mut buf);
        buf.truncate(100);
        let (_, mut r) = read_header(&buf).unwrap();
        assert!(ConnectRequest::decode(&mut r, buf.len()).is_err());
    }

    #[test]
    fn long_names_are_truncated_on_char_boundary() {
        let mut buf = Vec::new();
        let name = "é".repeat(30); // 60 bytes
        ChallengeResponse {
            client_nonce: 1,
            cookie: 2,
            reconnect_token: 0,
            join_token: 0,
            name,
            armor: 0,
            attachments: 0,
            primary: 0,
            client_public: [0; 32],
        }
        .encode(&mut buf);
        let (_, mut r) = read_header(&buf).unwrap();
        let d = ChallengeResponse::decode(&mut r, buf.len()).unwrap();
        assert_eq!(d.name.len(), 24);
    }
}
