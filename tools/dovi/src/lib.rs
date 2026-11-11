//! The Dolby Vision metadata step of the converter (libsbdts): one RPU (an HEVC UNSPEC62 NAL unit, its two-byte
//! header and emulation prevention included, as it stands in the stream) rewritten for a single-layer profile 8.1
//! stream, through libdovi's own conversion (mode 2: the enhancement layer's part dropped, the base layer's mapping
//! kept, the display metadata untouched). Nothing is decoded or kept between calls.
use dolby_vision::rpu::dovi_rpu::DoviRpu;

/// The RPU in `src` for profile 8.1, written to `dst`. Its size, or -1: not an RPU, not one of profile 7 or 8 (5 and
/// the others never pass for an HDR10 base), or larger than `cap`.
///
/// # Safety
/// `src` and `dst` point to `len` and `cap` bytes that don't overlap.
#[no_mangle]
pub unsafe extern "C" fn sbdovi_to81(src: *const u8, len: usize, dst: *mut u8, cap: usize) -> isize {
    if src.is_null() || dst.is_null() || len < 3 || len > 64 * 1024 {
        return -1;
    }
    let out = (|| {
        let mut rpu = DoviRpu::parse_unspec62_nalu(std::slice::from_raw_parts(src, len)).ok()?;
        if rpu.dovi_profile != 7 && rpu.dovi_profile != 8 {
            return None;
        }
        rpu.convert_with_mode(2).ok()?;
        rpu.write_hevc_unspec62_nalu().ok()
    })();
    match out {
        Some(b) if b.len() <= cap => {
            std::ptr::copy_nonoverlapping(b.as_ptr(), dst, b.len());
            b.len() as isize
        }
        _ => -1,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    const FEL: &[u8] = include_bytes!("../../../tests/dovi/fel_orig.bin");
    const MEL: &[u8] = include_bytes!("../../../tests/dovi/mel_orig.bin");

    fn conv(nal: &[u8], cap: usize) -> isize {
        let mut out = vec![0u8; cap.max(1)];
        unsafe { sbdovi_to81(nal.as_ptr(), nal.len(), out.as_mut_ptr(), cap) }
    }

    #[test]
    fn profile7_becomes_81() {
        for raw in [FEL, MEL] {
            let src = DoviRpu::parse_unspec62_nalu(raw).unwrap();
            assert_eq!(src.dovi_profile, 7);
            let nal = src.write_hevc_unspec62_nalu().unwrap();
            let mut out = vec![0u8; 8192];
            let n = unsafe { sbdovi_to81(nal.as_ptr(), nal.len(), out.as_mut_ptr(), out.len()) };
            assert!(n > 0);
            let got = DoviRpu::parse_unspec62_nalu(&out[..n as usize]).unwrap();
            assert_eq!(got.dovi_profile, 8);
            assert!(got.el_type.is_none());
            assert!(got.header.disable_residual_flag);
            assert_eq!(format!("{:?}", got.vdr_dm_data), format!("{:?}", src.vdr_dm_data));
            if let Ok(dir) = std::env::var("SBDOVI_SAMPLES") {
                let name = if raw.len() == FEL.len() { "fel" } else { "mel" };
                std::fs::write(format!("{dir}/{name}-in.nal"), &nal).unwrap();
                std::fs::write(format!("{dir}/{name}-out.nal"), &out[..n as usize]).unwrap();
            }
        }
    }

    #[test]
    fn bad_input_fails() {
        let nal = DoviRpu::parse_unspec62_nalu(MEL).unwrap().write_hevc_unspec62_nalu().unwrap();
        assert_eq!(conv(&nal, 4), -1);
        assert_eq!(conv(&[0x7c, 1, 0x19, 0, 0, 0, 0, 0], 8192), -1);
        let mut out = [0u8; 16];
        assert_eq!(unsafe { sbdovi_to81(std::ptr::null(), 20, out.as_mut_ptr(), 16) }, -1);
    }
}
