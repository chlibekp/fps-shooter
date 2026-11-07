//! Deterministic math helpers.
//!
//! Everything here must produce bit-identical results to the C++ mirror in
//! `Client/Source/TacticalFPS/Private/NetCore/TfpsMath.cpp`. Only IEEE-754
//! basic operations (`+ - * /`, `sqrt`) are used; no libm transcendental
//! functions, because their results differ between platforms and C runtimes.

/// Simple 3-component vector, meters, Z-up.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Vec3 = Vec3 { x: 0.0, y: 0.0, z: 0.0 };

    #[inline]
    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    #[inline]
    pub fn get(&self, axis: usize) -> f32 {
        match axis {
            0 => self.x,
            1 => self.y,
            _ => self.z,
        }
    }

    #[inline]
    pub fn set(&mut self, axis: usize, v: f32) {
        match axis {
            0 => self.x = v,
            1 => self.y = v,
            _ => self.z = v,
        }
    }

    pub fn distance(&self, o: &Vec3) -> f32 {
        let dx = self.x - o.x;
        let dy = self.y - o.y;
        let dz = self.z - o.z;
        (dx * dx + dy * dy + dz * dz).sqrt()
    }
}

/// Number of discrete yaw steps in a full turn (16-bit quantization).
pub const YAW_STEPS: u32 = 65536;

/// Converts a quantized yaw (0..65535 = 0..360 degrees) into (sin, cos)
/// deterministically.
///
/// The quadrant is selected with integer math; inside a quadrant a Taylor
/// polynomial in Horner form is evaluated (max error ~6e-8 on [0, pi/2]).
pub fn yaw_sin_cos(yaw_q: u16) -> (f32, f32) {
    let quadrant = (yaw_q >> 14) & 3;
    let rem = (yaw_q & 0x3FFF) as f32; // 0..16383, exactly representable
    // (pi / 2) / 16384
    let x = rem * 9.587_379_9e-5_f32;
    let x2 = x * x;
    let s = x
        * (1.0
            + x2 * (-0.166_666_67
                + x2 * (0.008_333_334
                    + x2 * (-0.000_198_412_7 + x2 * (2.755_731_9e-6 + x2 * -2.505_210_8e-8)))));
    let c = 1.0
        + x2 * (-0.5
            + x2 * (0.041_666_668
                + x2 * (-0.001_388_888_9
                    + x2 * (2.480_158_7e-5 + x2 * (-2.755_731_9e-7 + x2 * 2.087_675_7e-9)))));
    match quadrant {
        0 => (s, c),
        1 => (c, -s),
        2 => (-s, -c),
        _ => (-c, s),
    }
}

/// Quantizes yaw in degrees (any range) to 16 bits.
pub fn quantize_yaw_deg(yaw_deg: f32) -> u16 {
    let turns = yaw_deg / 360.0;
    let frac = turns - turns.floor();
    let q = (frac * YAW_STEPS as f32 + 0.5) as u32;
    (q % YAW_STEPS) as u16
}

pub fn dequantize_yaw_deg(yaw_q: u16) -> f32 {
    yaw_q as f32 * (360.0 / YAW_STEPS as f32)
}

/// Pitch is stored as i16 where +-32767 = +-90 degrees.
pub fn quantize_pitch_deg(pitch_deg: f32) -> i16 {
    let p = pitch_deg.clamp(-90.0, 90.0);
    (p * (32767.0 / 90.0)).round() as i16
}

pub fn dequantize_pitch_deg(pitch_q: i16) -> f32 {
    pitch_q.max(-32767) as f32 * (90.0 / 32767.0)
}

/// 64-bit FNV-1a, used to fingerprint map geometry.
pub struct Fnv1a64(u64);

impl Default for Fnv1a64 {
    fn default() -> Self {
        Self(0xcbf2_9ce4_8422_2325)
    }
}

impl Fnv1a64 {
    pub fn write(&mut self, bytes: &[u8]) {
        for b in bytes {
            self.0 ^= *b as u64;
            self.0 = self.0.wrapping_mul(0x0000_0100_0000_01b3);
        }
    }
    pub fn write_f32(&mut self, v: f32) {
        self.write(&v.to_bits().to_le_bytes());
    }
    pub fn finish(&self) -> u64 {
        self.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sin_cos_accuracy() {
        let mut max_err = 0.0f64;
        for q in (0..=65535u32).step_by(7) {
            let (s, c) = yaw_sin_cos(q as u16);
            let a = q as f64 / 65536.0 * std::f64::consts::TAU;
            max_err = max_err.max((s as f64 - a.sin()).abs());
            max_err = max_err.max((c as f64 - a.cos()).abs());
        }
        assert!(max_err < 5e-7, "max error {max_err}");
    }

    #[test]
    fn yaw_quantization_roundtrip() {
        for deg in [0.0f32, 45.0, 90.0, 180.0, 270.0, 359.99, -90.0, 720.0 + 10.0] {
            let q = quantize_yaw_deg(deg);
            let back = dequantize_yaw_deg(q);
            let norm = deg.rem_euclid(360.0);
            let diff = (back - norm).abs().min(360.0 - (back - norm).abs());
            assert!(diff < 0.01, "{deg} -> {q} -> {back}");
        }
        assert_eq!(quantize_yaw_deg(90.0), 16384);
    }

    #[test]
    fn pitch_quantization() {
        assert_eq!(quantize_pitch_deg(90.0), 32767);
        assert_eq!(quantize_pitch_deg(-200.0), -32767);
        assert!((dequantize_pitch_deg(quantize_pitch_deg(12.5)) - 12.5).abs() < 0.01);
    }
}
