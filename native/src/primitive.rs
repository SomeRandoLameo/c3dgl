//! Shared GL topology assembly. PICA consumes triangles with preserved GL winding.
use alloc::vec::Vec;

#[derive(Clone, Copy)]
pub(crate) struct Plan {
    pub mode: u32,
    pub consumed: usize,
    pub output: usize,
}

impl Plan {
    pub fn new(mode: u32, count: usize) -> Result<Self, u32> {
        let (consumed, triangles) = match mode {
            4 => {
                let n = count / 3 * 3;
                (n, n / 3)
            }
            5 | 6 | 9 => {
                if count < 3 {
                    (0, 0)
                } else {
                    (count, count - 2)
                }
            }
            7 => {
                let n = count / 4 * 4;
                (n, n / 4 * 2)
            }
            8 => {
                let n = count / 2 * 2;
                if n < 4 {
                    (0, 0)
                } else {
                    (n, n - 2)
                }
            }
            _ => return Err(0x0500),
        };
        
        let output = triangles
            .checked_mul(3)
            .filter(|n| *n <= u32::MAX as usize)
            .ok_or(0x0501u32)?;
        
        Ok(Self {
            mode,
            consumed,
            output,
        })
    }
    
    pub fn triangles(self, mut emit: impl FnMut([usize; 3])) {
        match self.mode {
            4 => {
                for i in (0..self.consumed).step_by(3) {
                    emit([i, i + 1, i + 2]);
                }
            }
            5 => {
                for i in 0..self.consumed.saturating_sub(2) {
                    emit(if i % 2 == 0 {
                        [i, i + 1, i + 2]
                    } else {
                        [i + 1, i, i + 2]
                    });
                }
            }
            6 | 9 => {
                for i in 1..self.consumed.saturating_sub(1) {
                    emit([0, i, i + 1]);
                }
            }
            7 => {
                for i in (0..self.consumed).step_by(4) {
                    emit([i, i + 1, i + 2]);
                    emit([i, i + 2, i + 3]);
                }
            }
            8 => {
                for i in (0..self.consumed.saturating_sub(2)).step_by(2) {
                    emit([i, i + 1, i + 3]);
                    emit([i, i + 3, i + 2]);
                }
            }
            _ => {}
        }
    }
    
    pub fn indices(self, mut map: impl FnMut(usize) -> u16) -> Result<Vec<u16>, u32> {
        let mut out = Vec::new();
        out.try_reserve_exact(self.output).map_err(|_| 0x0505u32)?;
        self.triangles(|t| out.extend(t.map(&mut map)));
        
        Ok(out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    fn sequence(mode: u32, count: usize) -> Vec<u16> {
        Plan::new(mode, count)
            .unwrap()
            .indices(|i| i as u16)
            .unwrap()
    }
    
    #[test]
    fn topology_order_and_incomplete_tails() {
        assert_eq!(sequence(4, 7), [0, 1, 2, 3, 4, 5]);
        assert_eq!(sequence(5, 5), [0, 1, 2, 2, 1, 3, 2, 3, 4]);
        assert_eq!(sequence(6, 5), [0, 1, 2, 0, 2, 3, 0, 3, 4]);
        assert_eq!(sequence(7, 9), [0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7]);
        assert_eq!(sequence(8, 7), [0, 1, 3, 0, 3, 2, 2, 3, 5, 2, 5, 4]);
        assert_eq!(sequence(9, 5), sequence(6, 5));
        for mode in 4..=9 {
            for count in 0..3 {
                assert!(sequence(mode, count).is_empty());
            }
        }
        assert!(sequence(7, 3).is_empty());
        assert!(sequence(8, 3).is_empty());
        assert!(Plan::new(1, 3).is_err());
        assert!(Plan::new(5, usize::MAX).is_err());
    }
    
    #[test]
    fn strip_faces_keep_winding_and_degenerate_parity() {
        let v = [[0i32, 0], [1, 0], [0, 1], [1, 1], [0, 2], [1, 2]];
        for mode in [5, 8] {
            Plan::new(mode, 6).unwrap().triangles(|t| {
                let [a, b, c] = t.map(|i| v[i]);
                assert!((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]) > 0);
            });
        }
        let indices = [0, 1, 2, 2, 3, 4];
        assert_eq!(
            Plan::new(5, 6).unwrap().indices(|i| indices[i]).unwrap(),
            [0, 1, 2, 2, 1, 2, 2, 2, 3, 3, 2, 4]
        );
    }
    
    #[test]
    fn output_matches_consumed_indices_at_limit() {
        for mode in 4..=9 {
            let p = Plan::new(mode, 4096).unwrap();
            let v = p.indices(|i| i as u16).unwrap();
            assert_eq!(v.len(), p.output);
            assert!(v.iter().all(|i| (*i as usize) < p.consumed));
            assert!(p.consumed <= 4096);
        }
    }
}
