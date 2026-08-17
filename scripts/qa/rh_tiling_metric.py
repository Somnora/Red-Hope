"""Measure how hard a surface repeats: the autocorrelation repeat-peak.

  uv run --with numpy --with pillow python scripts/qa/rh_tiling_metric.py \
      --crop 600x400+520+560 shots/before_00.png shots/after_00.png

Reports, per image, the strongest self-similarity peak at a non-zero lag. A
perfectly tiled surface scores near 1.0 (the patch one period over is identical);
uncorrelated grain scores near 0.

WHY THIS SCRIPT EXISTS, AND WHY IT DETRENDS

The 2026-08-14 pass measured this by hand and recorded:

    shipped, t=35 s          mean 36.42   repeat peak 0.439
    shipped, t=70 s          mean 62.18   repeat peak 0.493
    macro variation added    mean 40.09   repeat peak 0.484

The first two rows are the SAME BUILD. Sun movement alone moved the metric by
0.054, which is the same order as any improvement worth shipping - so the raw
metric cannot tell a fix from a cloud passing. Two defences here:

  1. Detrend. A large-sigma blur is subtracted before correlating, which removes
     the smooth lighting gradient and leaves only surface texture. Sun angle
     changes the gradient; it does not change whether the rock at p matches the
     rock at p+period.
  2. Normalise to unit variance after detrending, so overall exposure cancels.

Still pin the capture time anyway (RH.Cam + a fixed RH.Snapshot delay). Belt and
braces: a metric that is robust to lighting is not a licence to compare shots
taken at different times of day.

The reported peak EXCLUDES a small radius around zero lag, since every image
correlates with itself at lag 0 and near-zero lags just measure blur.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image


def parse_crop(spec):
    """WxH+X+Y, the ImageMagick geometry form."""
    if not spec:
        return None
    try:
        wh, x, y = spec.split("+")
        w, h = wh.lower().split("x")
        return int(x), int(y), int(w), int(h)
    except Exception:
        raise SystemExit("bad --crop %r; want WxH+X+Y e.g. 600x400+520+560" % spec)


def gaussian_blur(a, sigma):
    """Separable Gaussian via FFT-free convolution; sigma in pixels."""
    radius = max(1, int(sigma * 3))
    k = np.exp(-0.5 * (np.arange(-radius, radius + 1) / sigma) ** 2)
    k /= k.sum()
    pad = ((radius, radius), (0, 0))
    b = np.pad(a, pad, mode="reflect")
    b = np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 0, b)
    b = np.pad(b, ((0, 0), (radius, radius)), mode="reflect")
    b = np.apply_along_axis(lambda m: np.convolve(m, k, mode="valid"), 1, b)
    return b


def repeat_peak(gray, exclude=8, detrend_sigma=24.0):
    """Normalised autocorrelation peak at non-zero lag, plus where it sits."""
    a = gray.astype(np.float64)
    mean = float(a.mean())

    # 1. detrend: kill the lighting gradient, keep the texture
    a = a - gaussian_blur(a, detrend_sigma)

    # 2. window, so the patch edges do not manufacture correlation
    wy = np.hanning(a.shape[0])[:, None]
    wx = np.hanning(a.shape[1])[None, :]
    a = a * (wy * wx)

    # 3. normalise to unit variance: exposure cancels
    a = a - a.mean()
    sd = a.std()
    if sd < 1e-9:
        return mean, 0.0, (0, 0), 0.0
    a = a / sd

    # 4. autocorrelation by FFT, normalised so zero lag == 1
    F = np.fft.rfft2(a)
    ac = np.fft.irfft2(F * np.conj(F), s=a.shape)
    ac = np.fft.fftshift(ac)
    ac = ac / ac.max()

    cy, cx = ac.shape[0] // 2, ac.shape[1] // 2
    yy, xx = np.ogrid[: ac.shape[0], : ac.shape[1]]
    far = ((yy - cy) ** 2 + (xx - cx) ** 2) >= exclude ** 2

    masked = np.where(far, ac, -np.inf)
    idx = np.unravel_index(np.argmax(masked), masked.shape)
    peak = float(masked[idx])
    off = (int(idx[1] - cx), int(idx[0] - cy))

    # a second, coarser read: mean of the top 0.1% of far lags. A single peak can
    # be luck; a whole population of high lags is a genuine lattice.
    vals = ac[far]
    top = float(np.mean(np.sort(vals)[-max(1, vals.size // 1000):]))
    return mean, peak, off, top


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("images", nargs="+")
    ap.add_argument("--crop", default=None, help="WxH+X+Y region to measure")
    ap.add_argument("--exclude", type=int, default=8, help="lag radius to ignore around zero")
    ap.add_argument("--detrend", type=float, default=24.0, help="blur sigma removed as lighting")
    ap.add_argument("--report", default=None)
    a = ap.parse_args()

    box = parse_crop(a.crop)
    lines = ["%-38s %8s %8s %10s %8s" % ("image", "mean", "peak", "offset", "top0.1%")]
    for path in a.images:
        if not os.path.exists(path):
            lines.append("%-38s MISSING" % os.path.basename(path))
            continue
        im = Image.open(path).convert("L")
        if box:
            x, y, w, h = box
            if x + w > im.size[0] or y + h > im.size[1]:
                lines.append("%-38s CROP OUTSIDE IMAGE %s" % (os.path.basename(path), im.size))
                continue
            im = im.crop((x, y, x + w, y + h))
        g = np.asarray(im)
        mean, peak, off, top = repeat_peak(g, exclude=a.exclude, detrend_sigma=a.detrend)
        lines.append("%-38s %8.2f %8.3f %10s %8.3f" % (
            os.path.basename(path), mean, peak, "%+d,%+d" % off, top))

    out = "\n".join(lines)
    print(out)
    if a.report:
        with open(a.report, "w") as f:
            f.write(out + "\n")


if __name__ == "__main__":
    main()
