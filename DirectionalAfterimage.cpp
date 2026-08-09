/*
    Directional Afterimage
*/

#include "DirectionalAfterimage.h"

#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"

#include <math.h>
#include <string.h>

#define DAI_CURRENT_CHECKOUT_ID 1000

#define DAI_ECHO_CHECKOUT_ID(dir, index) \
    (2000 + ((dir) * MAX_ECHOES) + ((index) - 1))

// settings

struct DAI_Settings
{
    PF_FpLong angle_deg;
    A_long trail_count;
    A_long delay_frames;

    PF_FpLong drift_distance;
    PF_FpLong decay;

    PF_Boolean bidirectional;

    A_long fade_curve;
    A_long blend_mode;

    PF_FpLong trail_opacity;

    PF_Boolean hide_original;
};

// parameters

static PF_Err ReadSettings(
    PF_InData* in_data,
    DAI_Settings* s)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef p;

    // direction
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_DIRECTION,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->angle_deg = FIX_2_FLOAT(p.u.ad.value);

    PF_CHECKIN_PARAM(in_data, &p);

    // echo count
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_TRAIL_COUNT,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->trail_count = p.u.sd.value;

    PF_CHECKIN_PARAM(in_data, &p);

    // delay
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_DELAY_FRAMES,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->delay_frames = p.u.sd.value;

    PF_CHECKIN_PARAM(in_data, &p);

    // drift distance
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_DRIFT_DISTANCE,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->drift_distance = p.u.fs_d.value;

    PF_CHECKIN_PARAM(in_data, &p);

    // decay
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_DECAY,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->decay = p.u.fs_d.value / 100.0;

    PF_CHECKIN_PARAM(in_data, &p);

    // bidirectional
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_BIDIRECTIONAL,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->bidirectional = p.u.bd.value;

    PF_CHECKIN_PARAM(in_data, &p);

    // fade curve
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_FADE_CURVE,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->fade_curve = p.u.pd.value;

    PF_CHECKIN_PARAM(in_data, &p);

    // blend mode
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_BLEND_MODE,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->blend_mode = p.u.pd.value;

    PF_CHECKIN_PARAM(in_data, &p);

    // trail opacity
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_TRAIL_OPACITY,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->trail_opacity =
        p.u.fs_d.value / 100.0;

    PF_CHECKIN_PARAM(in_data, &p);

    // hide original
    AEFX_CLR_STRUCT(p);
    ERR(PF_CHECKOUT_PARAM(
        in_data,
        DAI_HIDE_ORIGINAL,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &p));

    s->hide_original = p.u.bd.value;

    PF_CHECKIN_PARAM(in_data, &p);

    return err;
}

static PF_FpLong EchoOpacity(
    const DAI_Settings& s,
    A_long index)
{
    if (s.trail_count <= 0)
        return 0.0;

    PF_FpLong opacity;

    if (s.fade_curve == FADE_CURVE_LINEAR)
    {
        PF_FpLong t =
            (PF_FpLong)(index - 1) /
            (PF_FpLong)s.trail_count;

        opacity =
            (1.0 - t) *
            s.trail_opacity;
    }
    else
    {
        opacity =
            pow(
                s.decay,
                (PF_FpLong)(index - 1)
            ) *
            s.trail_opacity;
    }

    if (opacity < 0.0)
        opacity = 0.0;

    if (opacity > 1.0)
        opacity = 1.0;

    return opacity;
}

template <typename PixelT>
static void ClearWorldT(
    PF_EffectWorld* world)
{
    if (!world || !world->data)
        return;

    for (A_long y = 0;
        y < world->height;
        ++y)
    {
        PixelT* row =
            reinterpret_cast<PixelT*>(
                reinterpret_cast<char*>(world->data) +
                y * world->rowbytes);

        memset(
            row,
            0,
            sizeof(PixelT) * world->width);
    }
}

template <typename PixelT>
static void CompositeTranslatedT(
    PF_EffectWorld* src,
    PF_EffectWorld* dst,
    A_long offset_x,
    A_long offset_y,
    PF_FpLong opacity,
    PF_FpLong max_channel)
{
    if (!src || !dst)
        return;

    if (!src->data || !dst->data)
        return;

    if (opacity <= 0.0)
        return;

    if (opacity > 1.0)
        opacity = 1.0;

    for (A_long dst_y = 0;
        dst_y < dst->height;
        ++dst_y)
    {
        A_long src_y =
            dst_y - offset_y;

        if (src_y < 0 ||
            src_y >= src->height)
        {
            continue;
        }

        PixelT* dst_row =
            reinterpret_cast<PixelT*>(
                reinterpret_cast<char*>(dst->data) +
                dst_y * dst->rowbytes);

        PixelT* src_row =
            reinterpret_cast<PixelT*>(
                reinterpret_cast<char*>(src->data) +
                src_y * src->rowbytes);

        for (A_long dst_x = 0;
            dst_x < dst->width;
            ++dst_x)
        {
            A_long src_x =
                dst_x - offset_x;

            if (src_x < 0 ||
                src_x >= src->width)
            {
                continue;
            }

            PixelT* s =
                &src_row[src_x];

            PixelT* d =
                &dst_row[dst_x];

            PF_FpLong sa =
                ((PF_FpLong)s->alpha) /
                max_channel;

            if (sa <= 0.0)
                continue;

            PF_FpLong sr =
                ((PF_FpLong)s->red) /
                max_channel;

            PF_FpLong sg =
                ((PF_FpLong)s->green) /
                max_channel;

            PF_FpLong sb =
                ((PF_FpLong)s->blue) /
                max_channel;

            PF_FpLong da =
                ((PF_FpLong)d->alpha) /
                max_channel;

            PF_FpLong dr =
                ((PF_FpLong)d->red) /
                max_channel;

            PF_FpLong dg =
                ((PF_FpLong)d->green) /
                max_channel;

            PF_FpLong db =
                ((PF_FpLong)d->blue) /
                max_channel;

            PF_FpLong a =
                sa * opacity;

            if (a <= 0.0)
                continue;

            /*
                normal alpha-over
            */

            PF_FpLong inv_a =
                1.0 - a;

            PF_FpLong out_a =
                a +
                da * inv_a;

            PF_FpLong out_r =
                sr * a +
                dr * inv_a;

            PF_FpLong out_g =
                sg * a +
                dg * inv_a;

            PF_FpLong out_b =
                sb * a +
                db * inv_a;

            if (out_a > 1.0)
                out_a = 1.0;

            if (out_r > 1.0)
                out_r = 1.0;

            if (out_g > 1.0)
                out_g = 1.0;

            if (out_b > 1.0)
                out_b = 1.0;

            if (out_a < 0.0)
                out_a = 0.0;

            if (out_r < 0.0)
                out_r = 0.0;

            if (out_g < 0.0)
                out_g = 0.0;

            if (out_b < 0.0)
                out_b = 0.0;

            d->alpha =
                static_cast<decltype(d->alpha)>(
                    out_a * max_channel);

            d->red =
                static_cast<decltype(d->red)>(
                    out_r * max_channel);

            d->green =
                static_cast<decltype(d->green)>(
                    out_g * max_channel);

            d->blue =
                static_cast<decltype(d->blue)>(
                    out_b * max_channel);
        }
    }
}

// bit depth

static A_long GetBytesPerPixel(
    PF_EffectWorld* world)
{
    if (!world ||
        world->width <= 0)
    {
        return 4;
    }

    return world->rowbytes /
        world->width;
}

// clear dispatcher

static PF_Err ClearWorld(
    PF_EffectWorld* world)
{
    if (!world)
        return PF_Err_NONE;

    A_long bpp =
        GetBytesPerPixel(world);

    if (bpp >= 12)
    {
        ClearWorldT<PF_PixelFloat>(
            world);
    }
    else if (bpp >= 6)
    {
        ClearWorldT<PF_Pixel16>(
            world);
    }
    else
    {
        ClearWorldT<PF_Pixel8>(
            world);
    }

    return PF_Err_NONE;
}

// composite dispatcher

static PF_Err CompositeTranslated(
    PF_EffectWorld* src,
    PF_EffectWorld* dst,
    A_long offset_x,
    A_long offset_y,
    PF_FpLong opacity)
{
    if (!src || !dst)
        return PF_Err_NONE;

    A_long bpp =
        GetBytesPerPixel(dst);

    if (bpp >= 12)
    {
        CompositeTranslatedT<PF_PixelFloat>(
            src,
            dst,
            offset_x,
            offset_y,
            opacity,
            1.0);
    }
    else if (bpp >= 6)
    {
        CompositeTranslatedT<PF_Pixel16>(
            src,
            dst,
            offset_x,
            offset_y,
            opacity,
            DAI_MAX_CHAN16);
    }
    else
    {
        CompositeTranslatedT<PF_Pixel8>(
            src,
            dst,
            offset_x,
            offset_y,
            opacity,
            DAI_MAX_CHAN8);
    }

    return PF_Err_NONE;
}

// global setup

static PF_Err GlobalSetup(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* [],
    PF_LayerDef*)
{
    out_data->my_version =
        PF_VERSION(
            MAJOR_VERSION,
            MINOR_VERSION,
            BUG_VERSION,
            STAGE_VERSION,
            BUILD_VERSION);

    out_data->out_flags =
        PF_OutFlag_DEEP_COLOR_AWARE |
        PF_OutFlag_WIDE_TIME_INPUT |
        PF_OutFlag_NON_PARAM_VARY;

    out_data->out_flags2 =
        PF_OutFlag2_FLOAT_COLOR_AWARE |
        PF_OutFlag2_SUPPORTS_SMART_RENDER |
        PF_OutFlag2_SUPPORTS_THREADED_RENDERING;

    return PF_Err_NONE;
}

// parameter setup

static PF_Err ParamsSetup(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* [],
    PF_LayerDef*)
{
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE(
        "Direction",
        0,
        DIRECTION_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(
        "Echo Count",
        1,
        MAX_ECHOES,
        1,
        MAX_ECHOES,
        6,
        TRAIL_COUNT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(
        "Delay (frames)",
        1,
        10,
        1,
        10,
        2,
        DELAY_FRAMES_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Drift Distance (px)",
        0,
        2000,
        0,
        500,
        25,
        PF_Precision_TENTHS,
        0,
        0,
        DRIFT_DISTANCE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Decay (%)",
        0,
        100,
        0,
        100,
        75,
        PF_Precision_INTEGER,
        0,
        0,
        DECAY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        "Bidirectional",
        "Mirror opposite direction",
        0,
        0,
        BIDIRECTIONAL_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Fade Curve",
        2,
        FADE_CURVE_EXPONENTIAL,
        "Exponential|Linear",
        FADE_CURVE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(
        "Blend Mode",
        2,
        BLEND_MODE_NORMAL,
        "Normal|Add",
        BLEND_MODE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(
        "Trail Opacity (%)",
        0,
        100,
        0,
        100,
        100,
        PF_Precision_INTEGER,
        0,
        0,
        TRAIL_OPACITY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX(
        "Hide Original",
        "Output echoes only",
        0,
        0,
        HIDE_ORIGINAL_DISK_ID);

    out_data->num_params =
        DAI_NUM_PARAMS;

    return err;
}

static PF_Err PreRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_PreRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    DAI_Settings s;

    ERR(ReadSettings(
        in_data,
        &s));

    if (err)
        return err;

    PF_RenderRequest request =
        extra->input->output_request;

    PF_CheckoutResult result;

    AEFX_CLR_STRUCT(result);

    ERR(extra->cb->checkout_layer(
        in_data->effect_ref,
        DAI_INPUT,
        DAI_CURRENT_CHECKOUT_ID,
        &request,
        in_data->current_time,
        in_data->time_step,
        in_data->time_scale,
        &result));

    if (err)
        return err;

    extra->output->result_rect =
        result.result_rect;

    extra->output->max_result_rect =
        result.max_result_rect;

    A_long directions =
        s.bidirectional ? 2 : 1;

    for (A_long d = 0;
        d < directions;
        ++d)
    {
        for (A_long i = 1;
            i <= s.trail_count;
            ++i)
        {
            AEFX_CLR_STRUCT(result);

            A_long echo_time =
                in_data->current_time -
                (
                    i *
                    s.delay_frames *
                    in_data->time_step
                    );

            ERR(extra->cb->checkout_layer(
                in_data->effect_ref,
                DAI_INPUT,
                DAI_ECHO_CHECKOUT_ID(d, i),
                &request,
                echo_time,
                in_data->time_step,
                in_data->time_scale,
                &result));

            if (err)
                return err;

            PF_LRect expanded =
                result.max_result_rect;

            A_long max_drift =
                static_cast<A_long>(
                    ceil(
                        fabs(
                            s.drift_distance) *
                        s.trail_count));

            expanded.left -= max_drift;
            expanded.right += max_drift;
            expanded.top -= max_drift;
            expanded.bottom += max_drift;


            PF_LRect clipped =
                expanded;

            if (clipped.left <
                request.rect.left)
            {
                clipped.left =
                    request.rect.left;
            }

            if (clipped.top <
                request.rect.top)
            {
                clipped.top =
                    request.rect.top;
            }

            if (clipped.right >
                request.rect.right)
            {
                clipped.right =
                    request.rect.right;
            }

            if (clipped.bottom >
                request.rect.bottom)
            {
                clipped.bottom =
                    request.rect.bottom;
            }

            if (clipped.left <
                extra->output->result_rect.left)
            {
                extra->output->result_rect.left =
                    clipped.left;
            }

            if (clipped.top <
                extra->output->result_rect.top)
            {
                extra->output->result_rect.top =
                    clipped.top;
            }

            if (clipped.right >
                extra->output->result_rect.right)
            {
                extra->output->result_rect.right =
                    clipped.right;
            }

            if (clipped.bottom >
                extra->output->result_rect.bottom)
            {
                extra->output->result_rect.bottom =
                    clipped.bottom;
            }

            if (expanded.left <
                extra->output->max_result_rect.left)
            {
                extra->output->max_result_rect.left =
                    expanded.left;
            }

            if (expanded.top <
                extra->output->max_result_rect.top)
            {
                extra->output->max_result_rect.top =
                    expanded.top;
            }

            if (expanded.right >
                extra->output->max_result_rect.right)
            {
                extra->output->max_result_rect.right =
                    expanded.right;
            }

            if (expanded.bottom >
                extra->output->max_result_rect.bottom)
            {
                extra->output->max_result_rect.bottom =
                    expanded.bottom;
            }
        }
    }

    extra->output->solid = 0;
    extra->output->pre_render_data = NULL;

    return err;
}

static PF_Err SmartRender(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_SmartRenderExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    DAI_Settings s;

    ERR(ReadSettings(
        in_data,
        &s));

    if (err)
        return err;

    PF_EffectWorld* current_world =
        NULL;

    PF_EffectWorld* echo_worlds
        [2][MAX_ECHOES];

    // array
    for (A_long d = 0;
        d < 2;
        ++d)
    {
        for (A_long i = 0;
            i < MAX_ECHOES;
            ++i)
        {
            echo_worlds[d][i] =
                NULL;
        }
    }

    ERR(extra->cb->checkout_layer_pixels(
        in_data->effect_ref,
        DAI_CURRENT_CHECKOUT_ID,
        &current_world));

    if (err)
        return err;

    A_long directions =
        s.bidirectional ? 2 : 1;

    for (A_long d = 0;
        d < directions;
        ++d)
    {
        for (A_long i = 1;
            i <= s.trail_count;
            ++i)
        {
            PF_EffectWorld* world =
                NULL;

            ERR(extra->cb->checkout_layer_pixels(
                in_data->effect_ref,
                DAI_ECHO_CHECKOUT_ID(d, i),
                &world));

            if (err)
                return err;

            echo_worlds[d][i - 1] =
                world;
        }
    }

    PF_EffectWorld* output_world =
        NULL;

    ERR(extra->cb->checkout_output(
        in_data->effect_ref,
        &output_world));

    if (err)
        return err;

    if (!output_world)
        return PF_Err_NONE;

    ERR(ClearWorld(
        output_world));

    if (err)
        return err;

    // Direction vector.

    PF_FpLong radians =
        s.angle_deg *
        PF_PI /
        180.0;

    PF_FpLong base_dx =
        cos(radians);

    PF_FpLong base_dy =
        sin(radians);

    // ECHOES

    for (A_long d = 0;
        d < directions;
        ++d)
    {
        PF_FpLong dx =
            base_dx;

        PF_FpLong dy =
            base_dy;

        if (d == 1)
        {
            dx = -dx;
            dy = -dy;
        }

        for (A_long i =
            s.trail_count;
            i >= 1;
            --i)
        {
            PF_EffectWorld* echo =
                echo_worlds[d][i - 1];

            if (!echo)
                continue;

            PF_FpLong opacity =
                EchoOpacity(
                    s,
                    i);

            if (opacity <= 0.0)
                continue;


            PF_FpLong exact_x =
                dx *
                s.drift_distance *
                (PF_FpLong)i;

            PF_FpLong exact_y =
                dy *
                s.drift_distance *
                (PF_FpLong)i;

            A_long offset_x;

            A_long offset_y;

            if (exact_x >= 0.0)
                offset_x =
                (A_long)floor(
                    exact_x + 0.5);
            else
                offset_x =
                (A_long)ceil(
                    exact_x - 0.5);

            if (exact_y >= 0.0)
                offset_y =
                (A_long)floor(
                    exact_y + 0.5);
            else
                offset_y =
                (A_long)ceil(
                    exact_y - 0.5);

            ERR(CompositeTranslated(
                echo,
                output_world,
                offset_x,
                offset_y,
                opacity));

            if (err)
                return err;
        }
    }

    if (!s.hide_original &&
        current_world)
    {
        ERR(CompositeTranslated(
            current_world,
            output_world,
            0,
            0,
            1.0));

        if (err)
            return err;
    }

    return PF_Err_NONE;
}

// -----------------------------------------------------------------------------
// Legacy render
// -----------------------------------------------------------------------------

static PF_Err Render(
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* [],
    PF_LayerDef* output)
{
    return PF_Err_NONE;
}

extern "C"
DllExport PF_Err EffectMain(
    PF_Cmd cmd,
    PF_InData* in_data,
    PF_OutData* out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void* extra)
{
    PF_Err err =
        PF_Err_NONE;

    try
    {
        switch (cmd)
        {
        case PF_Cmd_GLOBAL_SETUP:

            err =
                GlobalSetup(
                    in_data,
                    out_data,
                    params,
                    output);

            break;

        case PF_Cmd_PARAMS_SETUP:

            err =
                ParamsSetup(
                    in_data,
                    out_data,
                    params,
                    output);

            break;

        case PF_Cmd_RENDER:

            err =
                Render(
                    in_data,
                    out_data,
                    params,
                    output);

            break;

        case PF_Cmd_SMART_PRE_RENDER:

            err =
                PreRender(
                    in_data,
                    out_data,
                    reinterpret_cast<
                    PF_PreRenderExtra*
                    >(extra));

            break;

        case PF_Cmd_SMART_RENDER:

            err =
                SmartRender(
                    in_data,
                    out_data,
                    reinterpret_cast<
                    PF_SmartRenderExtra*
                    >(extra));

            break;

        default:

            break;
        }
    }
    catch (PF_Err& thrown_err)
    {
        err = thrown_err;
    }

    return err;
}