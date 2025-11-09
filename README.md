# AWarp

AWarp filter modified from https://github.com/dubhater/vapoursynth-awarpsharp2.


## Parameters

```py
awarp.AWarp(vnode clip, vnode mask[, int[] depth=3, bint mask_first_plane, int[] planes=[0, 1, 2]])
```

- clip: Clip to process. Any format with either integer sample type of 8-16 bit depth or float sample type of 32 bit depth is supported. Clip can either have the same dimensions as `mask`, or four times the size of `mask` in each dimension. The latter is useful for improving subpixel interpolation quality. Upsampled clip must be top-left aligned.

- mask: Edge mask. Must have the same format as `clip`.

- depth: Strength of the warping. Negative values result in warping in opposite direction, i.e. will blur the image instead of sharpening. If `clip` has subsampled chroma, the `depth` for chroma planes will be halved by default.

- mask_first_plane: If True, the `mask`'s first plane will be used as the mask for warping all planes. The mask will be bilinearly resized if necessary. Defaults to True for YUV color family, False otherwise.

- planes: Specifies which planes will be processed. Any unprocessed planes will be simply copied, except when `clip` has four times the size of `mask`, in which case the unprocessed planes will contain uninitialised memory.


## Compilation

```
meson build
ninja -C build
ninja -C build install
```
