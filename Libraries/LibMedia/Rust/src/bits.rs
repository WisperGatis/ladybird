// MSB-first bit reader matching LibMedia/BitReader.h. Reads past the end set an overrun flag instead of failing,
// so callers check `overrun()` once after a run of reads.
pub struct BitReader<'a> {
    data: &'a [u8],
    bit_position: usize,
    overrun: bool,
}

impl<'a> BitReader<'a> {
    pub fn new(data: &'a [u8]) -> Self {
        Self { data, bit_position: 0, overrun: false }
    }

    pub fn read_bits(&mut self, count: u32) -> u32 {
        let mut value = 0u32;
        for _ in 0..count {
            let byte_index = self.bit_position / 8;
            let bit = match self.data.get(byte_index) {
                Some(byte) => (byte >> (7 - (self.bit_position % 8))) & 1,
                None => {
                    self.overrun = true;
                    0
                }
            };
            value = (value << 1) | bit as u32;
            self.bit_position += 1;
        }
        value
    }

    pub fn read_bit(&mut self) -> bool {
        self.read_bits(1) != 0
    }

    pub fn overrun(&self) -> bool {
        self.overrun
    }
}
