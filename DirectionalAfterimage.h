#ifndef DIRECTIONAL_AFTERIMAGE_H
#define DIRECTIONAL_AFTERIMAGE_H

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"

#define PLUGIN_NAME     "Directional Afterimage"
#define PLUGIN_MATCH    "ADBE Directional Afterimage"
#define PLUGIN_CATEGORY "Stylize"

#define MAJOR_VERSION   1
#define MINOR_VERSION   1
#define BUG_VERSION     0
#define STAGE_VERSION   PF_Stage_DEVELOP
#define BUILD_VERSION   1

#define MAX_ECHOES      20

enum {
    DAI_INPUT = 0,
    DAI_DIRECTION,
    DAI_TRAIL_COUNT,
    DAI_DELAY_FRAMES,
    DAI_DRIFT_DISTANCE,
    DAI_DECAY,
    DAI_BIDIRECTIONAL,
    DAI_FADE_CURVE,
    DAI_BLEND_MODE,
    DAI_TRAIL_OPACITY,
    DAI_HIDE_ORIGINAL,
    DAI_NUM_PARAMS
};

enum {
    DIRECTION_DISK_ID = 1,
    TRAIL_COUNT_DISK_ID,
    DELAY_FRAMES_DISK_ID,
    DRIFT_DISTANCE_DISK_ID,
    DECAY_DISK_ID,
    BIDIRECTIONAL_DISK_ID,
    FADE_CURVE_DISK_ID,
    BLEND_MODE_DISK_ID,
    TRAIL_OPACITY_DISK_ID,
    HIDE_ORIGINAL_DISK_ID
};

enum {
    FADE_CURVE_EXPONENTIAL = 1,
    FADE_CURVE_LINEAR = 2
};

enum {
    BLEND_MODE_NORMAL = 1,
    BLEND_MODE_ADD = 2
};
#define CURRENT_CHECKOUT_ID 1

#define ECHO_CHECKOUT_ID(dir, i) \
    (1 + (dir) * MAX_ECHOES + (i))


#ifndef PF_PI
#define PF_PI 3.14159265358979323846
#endif


// per-bit-depth pixel range
#define DAI_MAX_CHAN8    255.0
#define DAI_MAX_CHAN16   32768.0


extern "C" {
    DllExport PF_Err EffectMain(
        PF_Cmd          cmd,
        PF_InData* in_data,
        PF_OutData* out_data,
        PF_ParamDef* params[],
        PF_LayerDef* output,
        void* extra);
}

#endif