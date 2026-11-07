//! Little-endian byte writer/reader. Mirrored by `TfpsBytes.h` in C++.

use crate::DecodeError;

/// Appends little-endian values to a `Vec<u8>` (callers reuse the vector to
/// avoid allocations).
pub struct Writer<'a> {
    pub buf: &'a mut Vec<u8>,
}

impl<'a> Writer<'a> {
    pub fn new(buf: &'a mut Vec<u8>) -> Self {
        Self { buf }
    }
    #[inline]
    pub fn len(&self) -> usize {
        self.buf.len()
    }
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.buf.is_empty()
    }
    #[inline]
    pub fn u8(&mut self, v: u8) {
        self.buf.push(v);
    }
    #[inline]
    pub fn i8(&mut self, v: i8) {
        self.buf.push(v as u8);
    }
    #[inline]
    pub fn u16(&mut self, v: u16) {
        self.buf.extend_from_slice(&v.to_le_bytes());
    }
    #[inline]
    pub fn i16(&mut self, v: i16) {
        self.buf.extend_from_slice(&v.to_le_bytes());
    }
    #[inline]
    pub fn u32(&mut self, v: u32) {
        self.buf.extend_from_slice(&v.to_le_bytes());
    }
    #[inline]
    pub fn i32(&mut self, v: i32) {
        self.buf.extend_from_slice(&v.to_le_bytes());
    }
    #[inline]
    pub fn u64(&mut self, v: u64) {
        self.buf.extend_from_slice(&v.to_le_bytes());
    }
    #[inline]
    pub fn f32(&mut self, v: f32) {
        self.u32(v.to_bits());
    }
    #[inline]
    pub fn bytes(&mut self, v: &[u8]) {
        self.buf.extend_from_slice(v);
    }
    /// u8 length prefix + UTF-8 bytes, truncated to `max` bytes on a char boundary.
    pub fn str8(&mut self, s: &str, max: usize) {
        let mut end = s.len().min(max).min(255);
        while !s.is_char_boundary(end) {
            end -= 1;
        }
        self.u8(end as u8);
        self.bytes(&s.as_bytes()[..end]);
    }
    /// Zero-pads to `total` bytes.
    pub fn pad_to(&mut self, total: usize) {
        if self.buf.len() < total {
            self.buf.resize(total, 0);
        }
    }
    /// Overwrites a u16 previously written at `at`.
    pub fn patch_u16(&mut self, at: usize, v: u16) {
        self.buf[at..at + 2].copy_from_slice(&v.to_le_bytes());
    }
    pub fn patch_u8(&mut self, at: usize, v: u8) {
        self.buf[at] = v;
    }
}

#[derive(Clone)]
pub struct Reader<'a> {
    data: &'a [u8],
    pos: usize,
}

macro_rules! read_le {
    ($name:ident, $t:ty, $n:expr) => {
        #[inline]
        pub fn $name(&mut self) -> Result<$t, DecodeError> {
            let b = self.take($n)?;
            let mut a = [0u8; $n];
            a.copy_from_slice(b);
            Ok(<$t>::from_le_bytes(a))
        }
    };
}

impl<'a> Reader<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self { data, pos: 0 }
    }
    #[inline]
    pub fn remaining(&self) -> usize {
        self.data.len() - self.pos
    }
    #[inline]
    pub fn position(&self) -> usize {
        self.pos
    }
    #[inline]
    pub fn take(&mut self, n: usize) -> Result<&'a [u8], DecodeError> {
        if self.remaining() < n {
            return Err(DecodeError::Truncated);
        }
        let s = &self.data[self.pos..self.pos + n];
        self.pos += n;
        Ok(s)
    }
    #[inline]
    pub fn u8(&mut self) -> Result<u8, DecodeError> {
        Ok(self.take(1)?[0])
    }
    #[inline]
    pub fn i8(&mut self) -> Result<i8, DecodeError> {
        Ok(self.u8()? as i8)
    }
    read_le!(u16, u16, 2);
    read_le!(i16, i16, 2);
    read_le!(u32, u32, 4);
    read_le!(i32, i32, 4);
    read_le!(u64, u64, 8);
    #[inline]
    pub fn f32(&mut self) -> Result<f32, DecodeError> {
        Ok(f32::from_bits(self.u32()?))
    }
    /// Reads a finite f32 (rejects NaN/inf, which a malicious peer could send).
    pub fn f32_finite(&mut self) -> Result<f32, DecodeError> {
        let v = self.f32()?;
        if v.is_finite() {
            Ok(v)
        } else {
            Err(DecodeError::Invalid("non-finite float"))
        }
    }
    pub fn str8(&mut self, max: usize) -> Result<String, DecodeError> {
        let len = self.u8()? as usize;
        if len > max {
            return Err(DecodeError::Invalid("string too long"));
        }
        let b = self.take(len)?;
        std::str::from_utf8(b)
            .map(|s| s.to_owned())
            .map_err(|_| DecodeError::Invalid("invalid utf-8"))
    }
}
