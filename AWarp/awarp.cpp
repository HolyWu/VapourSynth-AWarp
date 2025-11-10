/*
    MIT License

    Copyright (c) 2025 HolyWu

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
*/

#include <algorithm>
#include <memory>
#include <string>
#include <type_traits>

#include <VapourSynth4.h>
#include <VSHelper4.h>

using namespace std::string_literals;

struct AWarpData final {
    VSNode* node, * mask;
    const VSVideoInfo* vi;
    int depth_h[3], depth_v[3];
    bool process[3];
    int SMAGL, SMAG, peak;
    void (*filter)(const VSFrame* src, const VSFrame* mask, VSFrame* dst, const AWarpData* VS_RESTRICT d, const VSAPI* vsapi) noexcept;
};

template<typename pixel_t>
static void filter(const VSFrame* src, const VSFrame* mask, VSFrame* dst, const AWarpData* VS_RESTRICT d, const VSAPI* vsapi) noexcept {
    using scalar_t = std::conditional_t<std::is_integral_v<pixel_t>, int, float>;

    for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
        if (d->process[plane]) {
            const int width = vsapi->getFrameWidth(dst, plane);
            const int height = vsapi->getFrameHeight(dst, plane);
            const ptrdiff_t srcStride = vsapi->getStride(src, plane) / sizeof(pixel_t);
            const ptrdiff_t maskStride = vsapi->getStride(mask, plane) / sizeof(pixel_t);
            const ptrdiff_t dstStride = vsapi->getStride(dst, plane) / sizeof(pixel_t);
            auto srcp = reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(src, plane));
            auto maskp = reinterpret_cast<const pixel_t*>(vsapi->getReadPtr(mask, plane));
            pixel_t* VS_RESTRICT dstp = reinterpret_cast<pixel_t*>(vsapi->getWritePtr(dst, plane));

            const int xLimitMax = (width - 1) * d->SMAG;
            int yLimitMin, yLimitMax;
            scalar_t up, down, left, right;

            auto warp = [&](const int x) noexcept {
                if constexpr (std::is_integral_v<pixel_t>) {
                    int h = (((left - right) << 7) * d->depth_h[plane]) >> 16;
                    int v = (((up - down) << 7) * d->depth_v[plane]) >> 16;

                    v = std::clamp(v, yLimitMin, yLimitMax);

                    int remainderH = h;
                    int remainderV = v;

                    if (d->SMAGL) {
                        remainderH <<= d->SMAGL;
                        remainderV <<= d->SMAGL;
                    }

                    remainderH &= 127;
                    remainderV &= 127;

                    h >>= 7 - d->SMAGL;
                    v >>= 7 - d->SMAGL;

                    h += x << d->SMAGL;

                    if (!(h < xLimitMax && !(h < 0)))
                        remainderH = 0;

                    h = std::clamp(h, 0, xLimitMax);

                    int s0 = srcp[v * srcStride + h] * (128 - remainderH);
                    int s1 = srcp[(v + 1) * srcStride + h] * (128 - remainderH);

                    s0 += srcp[v * srcStride + h + 1] * remainderH;
                    s1 += srcp[(v + 1) * srcStride + h + 1] * remainderH;

                    s0 = (s0 + 64) >> 7;
                    s1 = (s1 + 64) >> 7;

                    s0 *= 128 - remainderV;
                    s1 *= remainderV;

                    const int s = (s0 + s1 + 64) >> 7;

                    return std::clamp(s, 0, d->peak);
                } else {
                    int h = static_cast<int>((left - right) * 255 + 0.5f);
                    int v = static_cast<int>((up - down) * 255 + 0.5f);

                    h = ((h << 7) * d->depth_h[plane]) >> 16;
                    v = ((v << 7) * d->depth_v[plane]) >> 16;

                    v = std::clamp(v, yLimitMin, yLimitMax);

                    int remainderH = h;
                    int remainderV = v;

                    if (d->SMAGL) {
                        remainderH <<= d->SMAGL;
                        remainderV <<= d->SMAGL;
                    }

                    remainderH &= 127;
                    remainderV &= 127;

                    h >>= 7 - d->SMAGL;
                    v >>= 7 - d->SMAGL;

                    h += x << d->SMAGL;

                    if (!(h < xLimitMax && !(h < 0)))
                        remainderH = 0;

                    h = std::clamp(h, 0, xLimitMax);

                    float s0 = srcp[v * srcStride + h] * (128 - remainderH);
                    float s1 = srcp[(v + 1) * srcStride + h] * (128 - remainderH);

                    s0 += srcp[v * srcStride + h + 1] * remainderH;
                    s1 += srcp[(v + 1) * srcStride + h + 1] * remainderH;

                    s0 /= 128;
                    s1 /= 128;

                    s0 *= 128 - remainderV;
                    s1 *= remainderV;

                    const float s = (s0 + s1) / 128;

                    return s;
                }
            };

            for (int y = 0; y < height; y++) {
                yLimitMin = -y * 128;
                yLimitMax = (height - y) * 128 - 129;

                auto above = (y == 0) ? maskp + maskStride : maskp - maskStride;
                auto below = (y == height - 1) ? maskp - maskStride : maskp + maskStride;

                int x = 0;
                up = above[x];
                down = below[x];
                left = maskp[x + 1];
                right = maskp[x + 1];
                dstp[x] = warp(x);

                for (x = 1; x < width - 1; x++) {
                    up = above[x];
                    down = below[x];
                    left = maskp[x - 1];
                    right = maskp[x + 1];
                    dstp[x] = warp(x);
                }

                x = width - 1;
                up = above[x];
                down = below[x];
                left = maskp[x - 1];
                right = maskp[x - 1];
                dstp[x] = warp(x);

                srcp += srcStride * d->SMAG;
                maskp += maskStride;
                dstp += dstStride;
            }
        }
    }
}

static const VSFrame* VS_CC aWarpGetFrame(int n, int activationReason, void* instanceData, [[maybe_unused]] void** frameData, VSFrameContext* frameCtx,
                                          VSCore* core, const VSAPI* vsapi) {
    auto d = static_cast<const AWarpData*>(instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
        vsapi->requestFrameFilter(n, d->mask, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        auto src = vsapi->getFrameFilter(n, d->node, frameCtx);
        auto mask = vsapi->getFrameFilter(n, d->mask, frameCtx);
        VSFrame* dst;

        if (vsapi->getFrameWidth(src, 0) == vsapi->getFrameWidth(mask, 0)) {
            const VSFrame* fr[] = { d->process[0] ? nullptr : src, d->process[1] ? nullptr : src, d->process[2] ? nullptr : src };
            const int pl[] = { 0, 1, 2 };
            dst = vsapi->newVideoFrame2(&d->vi->format, d->vi->width, d->vi->height, fr, pl, src, core);
        } else {
            dst = vsapi->newVideoFrame(&d->vi->format, d->vi->width, d->vi->height, src, core);
        }

        d->filter(src, mask, dst, d, vsapi);

        vsapi->freeFrame(src);
        vsapi->freeFrame(mask);
        return dst;
    }

    return nullptr;
}

static void VS_CC aWarpFree(void* instanceData, [[maybe_unused]] VSCore* core, const VSAPI* vsapi) {
    auto d = static_cast<AWarpData*>(instanceData);
    vsapi->freeNode(d->node);
    vsapi->freeNode(d->mask);
    delete d;
}

static void VS_CC aWarpCreate(const VSMap* in, VSMap* out, [[maybe_unused]] void* userData, VSCore* core, const VSAPI* vsapi) {
    auto d = std::make_unique<AWarpData>();

    try {
        d->node = vsapi->mapGetNode(in, "clip", 0, nullptr);
        d->mask = vsapi->mapGetNode(in, "mask", 0, nullptr);
        d->vi = vsapi->getVideoInfo(d->mask);
        auto clipVI = vsapi->getVideoInfo(d->node);
        int err;

        if (!vsh::isConstantVideoFormat(d->vi) ||
            (d->vi->format.sampleType == stInteger && d->vi->format.bitsPerSample > 16) ||
            (d->vi->format.sampleType == stFloat && d->vi->format.bitsPerSample != 32))
            throw "only constant format 8-16 bit integer and 32 bit float input supported"s;

        if (!vsh::isSameVideoFormat(&d->vi->format, &clipVI->format))
            throw "both clips must have the same format"s;

        if ((d->vi->width != clipVI->width || d->vi->height != clipVI->height) &&
            (d->vi->width * 4 != clipVI->width || d->vi->height * 4 != clipVI->height))
            throw "clip can either have the same dimensions as mask, or four times the size of mask in each dimension"s;

        if (d->vi->numFrames != clipVI->numFrames)
            throw "both clips must have the same number of frames"s;

        const int numDepthH = vsapi->mapNumElements(in, "depth_h");
        if (numDepthH > d->vi->format.numPlanes)
            throw "depth_h has more values specified than there are planes"s;

        const int numDepthV = vsapi->mapNumElements(in, "depth_v");
        if (numDepthV > d->vi->format.numPlanes)
            throw "depth_v has more values specified than there are planes"s;

        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            if (plane < numDepthH)
                d->depth_h[plane] = vsapi->mapGetIntSaturated(in, "depth_h", plane, nullptr);
            else if (plane == 0)
                d->depth_h[0] = 3;
            else if (plane == 1)
                d->depth_h[1] = d->depth_h[0] >> d->vi->format.subSamplingW;
            else
                d->depth_h[2] = d->depth_h[1];

            if (plane < numDepthV)
                d->depth_v[plane] = vsapi->mapGetIntSaturated(in, "depth_v", plane, nullptr);
            else if (plane < numDepthH)
                d->depth_v[plane] = d->depth_h[plane];
            else if (plane == 0)
                d->depth_v[0] = 3;
            else if (plane == 1)
                d->depth_v[1] = d->depth_v[0] >> d->vi->format.subSamplingH;
            else
                d->depth_v[2] = d->depth_v[1];

            if (d->depth_h[plane] < -128 || d->depth_h[plane] > 127)
                throw "depth_h must be between -128 and 127 (inclusive)"s;

            if (d->depth_v[plane] < -128 || d->depth_v[plane] > 127)
                throw "depth_v must be between -128 and 127 (inclusive)"s;
        }

        bool mask_first_plane = !!vsapi->mapGetInt(in, "mask_first_plane", 0, &err);
        if (err)
            mask_first_plane = d->vi->format.colorFamily == cfYUV;

        const int m = vsapi->mapNumElements(in, "planes");

        for (int i = 0; i < 3; i++)
            d->process[i] = (m <= 0);

        for (int i = 0; i < m; i++) {
            const int n = vsapi->mapGetIntSaturated(in, "planes", i, nullptr);

            if (n < 0 || n >= d->vi->format.numPlanes)
                throw "plane index out of range"s;

            if (d->process[n])
                throw "plane specified twice"s;

            d->process[n] = true;
        }

        if (d->vi->format.bytesPerSample == 1)
            d->filter = filter<uint8_t>;
        else if (d->vi->format.bytesPerSample == 2)
            d->filter = filter<uint16_t>;
        else
            d->filter = filter<float>;

        for (int plane = 0; plane < d->vi->format.numPlanes; plane++) {
            d->depth_h[plane] <<= (d->vi->format.sampleType == stInteger) ? 16 - d->vi->format.bitsPerSample : 8;
            d->depth_v[plane] <<= (d->vi->format.sampleType == stInteger) ? 16 - d->vi->format.bitsPerSample : 8;
        }

        if (mask_first_plane &&
            (d->process[1] || d->process[2]) &&
            (d->vi->format.subSamplingW > 0 || d->vi->format.subSamplingH > 0)) {
            auto args = vsapi->createMap();
            vsapi->mapConsumeNode(args, "clips", d->mask, maReplace);
            const int64_t planes[] = { 0, 0, 0 };
            vsapi->mapSetIntArray(args, "planes", planes, 3);
            vsapi->mapSetInt(args, "colorfamily", cfYUV, maReplace);
            vsapi->mapSetNode(args, "prop_src", d->node, maReplace);

            auto ret = vsapi->invoke(vsapi->getPluginByID(VSH_STD_PLUGIN_ID, core), "ShufflePlanes", args);
            if (vsapi->mapGetError(ret)) {
                vsapi->mapSetError(out, vsapi->mapGetError(ret));
                vsapi->freeMap(ret);
                vsapi->freeMap(args);
                vsapi->freeNode(d->node);
                return;
            }

            vsapi->clearMap(args);
            vsapi->mapConsumeNode(args, "clip", vsapi->mapGetNode(ret, "clip", 0, nullptr), maReplace);
            vsapi->mapSetInt(args,
                             "format",
                             vsapi->queryVideoFormatID(d->vi->format.colorFamily,
                                                       d->vi->format.sampleType,
                                                       d->vi->format.bitsPerSample,
                                                       d->vi->format.subSamplingW,
                                                       d->vi->format.subSamplingH,
                                                       core),
                             maReplace);
            vsapi->freeMap(ret);

            ret = vsapi->invoke(vsapi->getPluginByID(VSH_RESIZE_PLUGIN_ID, core), "Bilinear", args);
            if (vsapi->mapGetError(ret)) {
                vsapi->mapSetError(out, vsapi->mapGetError(ret));
                vsapi->freeMap(ret);
                vsapi->freeMap(args);
                vsapi->freeNode(d->node);
                return;
            }

            d->mask = vsapi->mapGetNode(ret, "clip", 0, nullptr);
            vsapi->freeMap(ret);
            vsapi->freeMap(args);
        }

        d->SMAGL = (d->vi->width == clipVI->width) ? 0 : 2;
        d->SMAG = 1 << d->SMAGL;

        if (d->vi->format.sampleType == stInteger)
            d->peak = (1 << d->vi->format.bitsPerSample) - 1;
    } catch (const std::string& error) {
        vsapi->mapSetError(out, ("AWarp: " + error).c_str());
        vsapi->freeNode(d->node);
        vsapi->freeNode(d->mask);
        return;
    }

    VSFilterDependency deps[] = { {d->node, rpStrictSpatial}, {d->mask, rpStrictSpatial} };
    vsapi->createVideoFilter(out, "AWarp", d->vi, aWarpGetFrame, aWarpFree, fmParallel, deps, 2, d.get(), core);
    d.release();
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit2(VSPlugin* plugin, const VSPLUGINAPI* vspapi) {
    vspapi->configPlugin("com.holywu.awarp",
                         "awarp",
                         "AWarp filter from AWarpSharp2",
                         VS_MAKE_VERSION(1, 0),
                         VAPOURSYNTH_API_VERSION,
                         0,
                         plugin);

    vspapi->registerFunction("AWarp",
                             "clip:vnode;mask:vnode;depth_h:int[]:opt;depth_v:int[]:opt;mask_first_plane:int:opt;planes:int[]:opt;",
                             "clip:vnode;",
                             aWarpCreate,
                             nullptr,
                             plugin);
}
