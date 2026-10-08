"""Build a high-resolution night-sky texture for the Citix sky dome.

Source: NASA/Goddard Space Flight Center Scientific Visualization Studio,
"Deep Star Maps 2020" (starmap_2020_4k_gal.exr) - equirectangular, public domain.
It ships as linear HDR with an alpha channel, so it is composited onto black, sRGB
encoded for an emissive texture, and given a soft star bloom so the stars read as
"shiny" through mip filtering instead of disappearing.

Run:  python build_sky.py <input.exr> <output.png> [width] [height]
"""
import sys
import numpy as np
import cv2


def _channel(channels, wanted):
    """OpenEXR channel lookup: names may be plain ('R') or layered ('rgba.R')."""
    for name, channel in channels.items():
        if name.split(".")[-1].upper() == wanted:
            return np.asarray(channel.pixels, dtype=np.float32)
    return None


def read_exr(path):
    """Read an EXR into a float32 RGB image, compositing alpha over black."""
    import OpenEXR
    handle = OpenEXR.File(path)
    parts = getattr(handle, "parts", None)
    part = parts[0] if parts else handle
    channels = part.channels

    r, g, b = _channel(channels, "R"), _channel(channels, "G"), _channel(channels, "B")
    if r is not None and g is not None and b is not None:
        rgb = np.stack([r, g, b], axis=2)
        alpha = _channel(channels, "A")
        if alpha is not None:
            rgb = rgb * alpha[:, :, None]
        return rgb

    # Otherwise the file may store one interleaved group (NASA's star maps ship as a single
    # 'RGB' channel of shape HxWx3).
    for channel in channels.values():
        pixels = np.asarray(channel.pixels, dtype=np.float32)
        if pixels.ndim == 3 and pixels.shape[2] >= 3:
            if pixels.shape[2] >= 4:
                pixels[:, :, :3] *= pixels[:, :, 3:4]
            return pixels[:, :, :3]
    raise RuntimeError("no RGB channels in %s" % path)


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    width = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
    height = int(sys.argv[4]) if len(sys.argv) > 4 else 2048

    img = read_exr(src)
    if img is None:
        print("FAILED to read", src)
        return 1
    print("source:", img.shape, img.dtype)

    # EXR is float; may carry alpha for compositing. Handle 3 or 4 channels.
    if img.dtype == np.uint16:
        img = img.astype(np.float32) / 65535.0
    if img.ndim == 3 and img.shape[2] == 4:
        rgb = img[:, :, :3]
        alpha = img[:, :, 3:4]
        img = rgb * alpha
    elif img.ndim == 2:
        img = np.repeat(img[:, :, None], 3, axis=2)
    rgb = np.clip(img.astype(np.float32), 0.0, None)

    # Down to the shipping size first, so the bloom is applied at final resolution.
    if rgb.shape[1] != width or rgb.shape[0] != height:
        rgb = cv2.resize(rgb, (width, height), interpolation=cv2.INTER_AREA)

    # Linear -> sRGB for an 8-bit emissive texture (UE decodes it back to linear).
    srgb = np.power(np.clip(rgb, 0.0, 1.0), 1.0 / 2.2)

    # Star bloom: take the bright part, blur it, and add it back. This is what makes the
    # stars survive mip filtering and read as shiny points rather than sub-pixel noise.
    bright = np.clip((srgb - 0.18) * (1.0 / 0.82), 0.0, 1.0)
    glow = cv2.GaussianBlur(bright, (0, 0), 2.0) * 1.35
    out = np.clip(srgb + glow, 0.0, 1.0)

    bgr = (out[:, :, ::-1] * 255.0 + 0.5).astype(np.uint8)
    if not cv2.imwrite(dst, bgr):
        print("FAILED to write", dst)
        return 1

    print("wrote", dst, bgr.shape,
          "mean=%.4f p99=%.4f max=%.3f" % (out.mean(), np.percentile(out, 99.0), out.max()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
