//! C ABI for the C++ viewer. Each function is `extern "C"`, catches panics at
//! the boundary, and uses only plain types. The matching header is
//! `indra/llimagej2crust/vkstorm_ffi.h`; keep the two in step.
//!
//! Rung one: JPEG 2000 decoding via `vkstorm-j2c`.

use std::ffi::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;

/// Result of a header read. All fields zero on failure.
#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct VksJ2cMetadata {
    pub width: u32,
    pub height: u32,
    pub components: u32,
}

/// An owned decoded image. Opaque to C++; read through the accessors and
/// release with `vks_j2c_decoded_free`.
pub struct VksJ2cDecoded {
    inner: vkstorm_j2c::Decoded,
    comment: Option<std::ffi::CString>,
}

/// Decoder description for the engine-info display. Static, NUL-terminated.
#[unsafe(no_mangle)]
pub extern "C" fn vks_j2c_engine_info() -> *const c_char {
    static INFO: &[u8] = b"hayro-jpeg2000 0.4.1 (Rust)\0";
    INFO.as_ptr().cast()
}

/// Parse the main header. Returns 1 on success and fills `out`, 0 on failure.
///
/// # Safety
/// `data` must point to `len` readable bytes; `out` must be a valid pointer.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vks_j2c_metadata(data: *const u8, len: usize, out: *mut VksJ2cMetadata) -> i32 {
    if data.is_null() || out.is_null() {
        return 0;
    }
    let bytes = unsafe { slice::from_raw_parts(data, len) };
    let r = catch_unwind(AssertUnwindSafe(|| vkstorm_j2c::metadata(bytes)));
    match r {
        Ok(Ok(m)) => {
            unsafe { *out = VksJ2cMetadata { width: m.width, height: m.height, components: m.components } };
            1
        }
        _ => 0,
    }
}

/// Decode at `discard_level` using at most `max_bytes` of `data` (0 = all),
/// keeping at most `max_channels` channels from `first_channel`. Returns an
/// owned image or null on failure.
///
/// # Safety
/// `data` must point to `len` readable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vks_j2c_decode(
    data: *const u8,
    len: usize,
    max_bytes: usize,
    discard_level: u32,
    first_channel: u32,
    max_channels: u32,
) -> *mut VksJ2cDecoded {
    if data.is_null() {
        return std::ptr::null_mut();
    }
    let bytes = unsafe { slice::from_raw_parts(data, len) };
    let r = catch_unwind(AssertUnwindSafe(|| {
        let inner = vkstorm_j2c::decode(bytes, max_bytes, discard_level, first_channel, max_channels)?;
        let comment = vkstorm_j2c::comment(bytes).and_then(|c| std::ffi::CString::new(c).ok());
        Ok::<_, vkstorm_j2c::Error>(VksJ2cDecoded { inner, comment })
    }));
    match r {
        Ok(Ok(d)) => Box::into_raw(Box::new(d)),
        _ => std::ptr::null_mut(),
    }
}

/// Dimensions and channel count of a decoded image.
///
/// # Safety
/// `d` must come from `vks_j2c_decode` and not have been freed.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vks_j2c_decoded_size(d: *const VksJ2cDecoded, width: *mut u32, height: *mut u32, channels: *mut u32, components: *mut u32) {
    let d = unsafe { &*d };
    unsafe {
        if !width.is_null() { *width = d.inner.width; }
        if !height.is_null() { *height = d.inner.height; }
        if !channels.is_null() { *channels = d.inner.channels; }
        if !components.is_null() { *components = d.inner.components; }
    }
}

/// Copy the interleaved, bottom-up 8-bit samples into `dst` (exactly
/// `width * height * channels` bytes). Returns 1 on success, 0 if `dst_len`
/// is too small.
///
/// # Safety
/// `d` must be live; `dst` must point to `dst_len` writable bytes.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vks_j2c_decoded_copy(d: *const VksJ2cDecoded, dst: *mut u8, dst_len: usize) -> i32 {
    let d = unsafe { &*d };
    if dst.is_null() || dst_len < d.inner.data.len() {
        return 0;
    }
    unsafe { std::ptr::copy_nonoverlapping(d.inner.data.as_ptr(), dst, d.inner.data.len()) };
    1
}

/// The COM marker comment, NUL-terminated, valid until the image is freed;
/// null when absent.
///
/// # Safety
/// `d` must be live.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vks_j2c_decoded_comment(d: *const VksJ2cDecoded) -> *const c_char {
    let d = unsafe { &*d };
    d.comment.as_ref().map_or(std::ptr::null(), |c| c.as_ptr())
}

/// Release a decoded image. Null is ignored.
///
/// # Safety
/// `d` must come from `vks_j2c_decode` and not be used afterwards.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn vks_j2c_decoded_free(d: *mut VksJ2cDecoded) {
    if !d.is_null() {
        drop(unsafe { Box::from_raw(d) });
    }
}
