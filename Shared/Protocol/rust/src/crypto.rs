//! Packet encryption: X25519 key agreement during the handshake, then every
//! `Data` packet is sealed with ChaCha20-Poly1305 (RFC 8439).
//!
//! Layout of a sealed `Data` packet:
//!
//! ```text
//! cleartext header (DATA_HEADER_LEN bytes; authenticated as AAD)
//! ciphertext of the rest of the packet
//! u8[16] Poly1305 tag
//! u32    packet counter (little endian; the AEAD nonce, never reused)
//! ```
//!
//! Each direction has its own key, derived from the shared secret (see
//! `derive_session_keys`); the nonce is the counter zero-extended to 96 bits.
//! A sliding window rejects replayed packets. The C++ client core implements
//! the same (`TfpsCrypto`), and the golden vectors check both agree.
//!
//! There are no certificates: this protects against eavesdropping and against
//! forging or replaying packets by anyone who did not take part in the
//! handshake, not against an active man in the middle.

use chacha20::cipher::{KeyIvInit, StreamCipher};
use chacha20poly1305::aead::{AeadInPlace, KeyInit};
use chacha20poly1305::{ChaCha20Poly1305, Key, Nonce, Tag};

/// Bytes a sealed packet adds: tag and counter.
pub const SEAL_OVERHEAD: usize = 16 + 4;
/// Cleartext header of a `Data` packet: magic, version, type, session key,
/// seq, ack, ack bits.
pub const DATA_HEADER_LEN: usize = 2 + 2 + 1 + 8 + 2 + 2 + 4;

/// An X25519 key pair for one handshake.
pub struct KeyPair {
    secret: x25519_dalek::StaticSecret,
    pub public: [u8; 32],
}

impl KeyPair {
    pub fn generate() -> Self {
        let mut bytes = [0u8; 32];
        rand::fill(&mut bytes);
        Self::from_secret(bytes)
    }

    pub fn from_secret(bytes: [u8; 32]) -> Self {
        let secret = x25519_dalek::StaticSecret::from(bytes);
        let public = x25519_dalek::PublicKey::from(&secret).to_bytes();
        Self { secret, public }
    }

    /// X25519 shared secret with the peer's public key.
    pub fn shared(&self, peer_public: &[u8; 32]) -> [u8; 32] {
        self.secret
            .diffie_hellman(&x25519_dalek::PublicKey::from(*peer_public))
            .to_bytes()
    }
}

/// (client-to-server, server-to-client) keys: the first 32 bytes of the
/// ChaCha20 keystream keyed by the shared secret, under the nonces
/// "tfps-c2s-key" and "tfps-s2c-key".
pub fn derive_session_keys(shared: &[u8; 32]) -> ([u8; 32], [u8; 32]) {
    let derive = |label: &[u8; 12]| {
        let mut k = [0u8; 32];
        let mut c = chacha20::ChaCha20::new(shared.into(), label.into());
        c.apply_keystream(&mut k);
        k
    };
    (derive(b"tfps-c2s-key"), derive(b"tfps-s2c-key"))
}

fn nonce(counter: u32) -> Nonce {
    let mut n = [0u8; 12];
    n[..4].copy_from_slice(&counter.to_le_bytes());
    Nonce::from(n)
}

/// One side of an encrypted session.
pub struct PacketCipher {
    send: ChaCha20Poly1305,
    recv: ChaCha20Poly1305,
    send_counter: u32,
    /// Highest counter accepted and a bitmap of the 64 before it.
    recv_top: Option<u32>,
    recv_window: u64,
}

impl PacketCipher {
    /// `client` selects which derived key sends and which receives.
    pub fn new(shared: &[u8; 32], client: bool) -> Self {
        let (c2s, s2c) = derive_session_keys(shared);
        let (send, recv) = if client { (c2s, s2c) } else { (s2c, c2s) };
        Self {
            send: ChaCha20Poly1305::new(Key::from_slice(&send)),
            recv: ChaCha20Poly1305::new(Key::from_slice(&recv)),
            send_counter: 0,
            recv_top: None,
            recv_window: 0,
        }
    }

    /// Encrypts `packet[DATA_HEADER_LEN..]` in place and appends tag and counter.
    pub fn seal(&mut self, packet: &mut Vec<u8>) {
        let counter = self.send_counter;
        self.send_counter = self.send_counter.wrapping_add(1);
        let split = DATA_HEADER_LEN.min(packet.len());
        let (head, body) = packet.split_at_mut(split);
        let tag = self
            .send
            .encrypt_in_place_detached(&nonce(counter), head, body)
            .expect("ChaCha20-Poly1305 cannot fail for packet sizes");
        packet.extend_from_slice(&tag);
        packet.extend_from_slice(&counter.to_le_bytes());
    }

    /// Verifies and decrypts a sealed packet in place; returns the plaintext
    /// length (header plus body), or None if forged, truncated or replayed.
    pub fn open(&mut self, packet: &mut [u8]) -> Option<usize> {
        if packet.len() < DATA_HEADER_LEN + SEAL_OVERHEAD {
            return None;
        }
        let n = packet.len();
        let counter = u32::from_le_bytes(packet[n - 4..].try_into().ok()?);
        if !self.fresh(counter) {
            return None;
        }
        let tag = Tag::clone_from_slice(&packet[n - 20..n - 4]);
        let (head, rest) = packet.split_at_mut(DATA_HEADER_LEN);
        let body = &mut rest[..n - 20 - DATA_HEADER_LEN];
        self.recv
            .decrypt_in_place_detached(&nonce(counter), head, body, &tag)
            .ok()?;
        self.accept(counter);
        Some(n - SEAL_OVERHEAD)
    }

    fn fresh(&self, counter: u32) -> bool {
        match self.recv_top {
            None => true,
            Some(top) if counter > top => true,
            Some(top) => {
                let back = top - counter;
                back < 64 && self.recv_window & (1u64 << back) == 0
            }
        }
    }

    fn accept(&mut self, counter: u32) {
        match self.recv_top {
            Some(top) if counter <= top => self.recv_window |= 1u64 << (top - counter),
            Some(top) => {
                let shift = counter - top;
                self.recv_window = if shift >= 64 {
                    0
                } else {
                    self.recv_window << shift
                } | 1;
                self.recv_top = Some(counter);
            }
            None => {
                self.recv_top = Some(counter);
                self.recv_window = 1;
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pair() -> (PacketCipher, PacketCipher) {
        let a = KeyPair::from_secret([1; 32]);
        let b = KeyPair::from_secret([2; 32]);
        assert_eq!(a.shared(&b.public), b.shared(&a.public));
        (
            PacketCipher::new(&a.shared(&b.public), true),
            PacketCipher::new(&b.shared(&a.public), false),
        )
    }

    #[test]
    fn sealed_packets_round_trip_and_forgeries_and_replays_fail() {
        let (mut client, mut server) = pair();
        let plain: Vec<u8> = (0..100u8).collect();
        let mut p = plain.clone();
        client.seal(&mut p);
        assert_eq!(p.len(), plain.len() + SEAL_OVERHEAD);
        assert_eq!(
            p[..DATA_HEADER_LEN],
            plain[..DATA_HEADER_LEN],
            "header stays clear"
        );
        assert_ne!(p[DATA_HEADER_LEN..100], plain[DATA_HEADER_LEN..]);
        let mut copy = p.clone();
        assert_eq!(server.open(&mut p), Some(plain.len()));
        assert_eq!(p[..plain.len()], plain[..]);
        assert_eq!(server.open(&mut copy), None, "replay rejected");
        let mut q = plain.clone();
        client.seal(&mut q);
        q[3] ^= 1; // header is authenticated
        assert_eq!(server.open(&mut q), None);
        // The server cannot open its own direction's packets (separate keys).
        let mut r = plain.clone();
        server.seal(&mut r);
        let mut r2 = r.clone();
        assert_eq!(server.open(&mut r), None);
        assert_eq!(client.open(&mut r2), Some(plain.len()));
    }

    #[test]
    fn replay_window_accepts_reordered_packets_once() {
        let (mut client, mut server) = pair();
        let packets: Vec<Vec<u8>> = (0..5)
            .map(|_| {
                let mut p = vec![7u8; 40];
                client.seal(&mut p);
                p
            })
            .collect();
        for i in [3usize, 0, 4, 1, 2] {
            let mut p = packets[i].clone();
            assert!(server.open(&mut p).is_some(), "packet {i}");
        }
        let mut again = packets[1].clone();
        assert!(server.open(&mut again).is_none());
    }
}
